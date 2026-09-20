#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <vector>
#include <iomanip>
#include <csignal>
#include <atomic>
#include <algorithm>

#include "../include/hardware/i2c_bus.h"
#include "../include/hardware/pca9685.h"
#include "../include/hardware/servo.h"
#include "../include/hardware/camera_stream.h"
#include "../include/npu/npu_model.h"
#include "../include/npu/yolo_postprocess.h"
#include "../include/npu/patchcore_engine.h"
#include "../include/network/stream_server.h"

// Глобальные флаги и дескрипторы для безопасного завершения
std::atomic<bool> g_running{true};
std::shared_ptr<PCA9685> g_pca = nullptr;
std::unique_ptr<Servo> g_gate = nullptr;

void sigHandler(int) {
    std::cout << "\n[SYSTEM] Получен сигнал остановки (SIGINT/SIGTERM). Закрытие шторки...\n";
    g_running = false;
    if (g_gate) {
        g_gate->close();
    }
}

int main() {
    std::signal(SIGINT, sigHandler);
    std::signal(SIGTERM, sigHandler);

    try {
        std::cout << "[SYSTEM] Запуск аппаратного рубежа ВСМ (РЕЖИМ ЭМУЛЯЦИИ ДАТЧИКА)...\n";

        // =====================================================================
        // 1. АППАРАТНАЯ ЧАСТЬ (I2C-7 для PCA9685 / Сервошторки)
        // =====================================================================
        auto bus7 = std::make_shared<I2CBus>("/dev/i2c-7");
        g_pca = std::make_shared<PCA9685>(bus7, 0x61);
        g_pca->init(50.0f);

        g_gate = std::make_unique<Servo>(g_pca, 15);
        g_gate->close(); // Исходное состояние: закрыто (ждём состав)
        std::cout << "[HARDWARE] Защитная шторка установлена в положение CLOSE (0°).\n";

        // =====================================================================
        // 2. СЕТЕВОЙ СЕРВЕР ТЕРМИНАЛА ОПЕРАТОРА
        // =====================================================================
        StreamServer streamServer(8888);
        if (!streamServer.start()) {
            std::cerr << "[NETWORK] Не удалось запустить StreamServer на порту 8888!\n";
            return 1;
        }

        // =====================================================================
        // 3. ИНИЦИАЛИЗАЦИЯ НЕЙРОСЕТЕЙ НА NPU (Ascend 310B)
        // =====================================================================
        std::cout << "[NPU] Загрузка модели yolov8n.om (Верхний контур)...\n";
        NpuModel yoloModel(0);
        if (!yoloModel.load("/tmp/hsm-barrier/models/yolov8n.om")) {
            std::cerr << "[NPU] Ошибка загрузки models/yolov8n.om!\n";
            return 1;
        }

        std::cout << "[NPU] Загрузка модели patchcore.om (Нижний контур)...\n";
        PatchcoreEngine patchcore(0);
        if (!patchcore.load("/tmp/hsm-barrier/models/patchcore.om")) {
            std::cerr << "[NPU] Ошибка загрузки models/patchcore.om!\n";
            return 1;
        }

        // =====================================================================
        // 4. ОПТИЧЕСКИЕ ПОТОКИ (Камера 0 - Верх, Камера 2 - Низ)
        // =====================================================================
        std::cout << "[CAMERA] Подключение к /dev/video0 (Столб / Крыша)...\n";
        CameraStream topCam(0, 640, 640);
        if (!topCam.open()) {
            std::cerr << "[CAMERA] Ошибка: не удалось открыть /dev/video0!\n";
            return 1;
        }
        topCam.start();

        std::cout << "[CAMERA] Подключение к /dev/video2 (Нижний короб)...\n";
        CameraStream bottomCam(2, 640, 480);
        if (!bottomCam.open()) {
            std::cerr << "[CAMERA] Предупреждение: /dev/video2 недоступна!\n";
        } else {
            bottomCam.start();
        }

        std::cout << "[PIPELINE] Комплекс активен. Запущен таймер ожидания (5 сек до входа состава)...\n";

        cv::Mat frameTop;
        cv::Mat frameBottom;
        std::vector<float> yoloOutput;
        yoloOutput.reserve(84 * 8400);

        auto lastTopAlert = std::chrono::steady_clock::now();
        auto lastBottomAlert = std::chrono::steady_clock::now();

        constexpr float ANOMALY_THRESHOLD = 0.80f;

        // Фильтры дребезга
        int topPersonStreak = 0;
        constexpr int TOP_STREAK_TRIGGER = 2;

        int bottomAnomalyStreak = 0;
        constexpr int BOTTOM_STREAK_TRIGGER = 2;

        // Эмуляция состава: отсчет 5 секунд от запуска
        const auto appStartTime = std::chrono::steady_clock::now();
        bool isTrainPresent = false;

        while (g_running) {
            // --- 0. ИМИТАЦИЯ ТОФ-ДАТЧИКА ПО ВРЕМЕНИ ---
            if (!isTrainPresent) {
                auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - appStartTime).count();

                if (elapsedSec >= 5) {
                    std::cout << "🚂 [GATE] Открываем сервошторку короба днища...\n";
                    g_gate->open();
                    isTrainPresent = true;
                }
            }

            // --- КОНТУР 1: ВЕРХ (КРЫША / ЛЮДИ) ---
            if (topCam.getLatestFrame(frameTop)) {
                std::vector<float> inputTensor = fastPreprocessToNCHW(frameTop, 640, 640);

                if (yoloModel.execute(inputTensor, yoloOutput)) {
                    auto detections = postprocessYOLOv8(yoloOutput, frameTop.cols, frameTop.rows, 0.45f, 0.50f);

                   int personCount = 0;
                    for (const auto& det : detections) {
                        if (det.classId == 0) personCount++;
                    }

                    if (personCount > 0) {
                        topPersonStreak = std::min(topPersonStreak + 1, 10);
                    } else {
                        topPersonStreak = 0;
                    }

                    if (personCount > 0 && topPersonStreak >= TOP_STREAK_TRIGGER) {
                        auto now = std::chrono::steady_clock::now();
                        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTopAlert).count() > 400) {
                            lastTopAlert = now;
                            std::cout << "🚨 [ALERT ВЕРХ] Подтверждено присутствие человека! (" << personCount << " чел)\n";
                            streamServer.broadcastAlarmFrame(1, frameTop);
                        }
                    }
                }
            }

            // --- КОНТУР 2: НИЗ (ДНИЩЕ / АНОМАЛИИ) ---
            // Сканирует только после того, как "поезд заехал" (через 5 сек)
            if (isTrainPresent && bottomCam.isRunning() && bottomCam.getLatestFrame(frameBottom)) {
                const AnomalyResult anomaly = patchcore.detect(frameBottom, ANOMALY_THRESHOLD);

                if (anomaly.isAnomaly) {
                    bottomAnomalyStreak = std::min(bottomAnomalyStreak + 1, 10);
                } else {
                    bottomAnomalyStreak = 0;
                }

                if (anomaly.isAnomaly && bottomAnomalyStreak >= BOTTOM_STREAK_TRIGGER) {
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBottomAlert).count() > 400) {
                        lastBottomAlert = now;
                        std::cout << "🚨 [ALERT НИЗ] Подтверждена аномалия днища! Score: "
                                  << std::fixed << std::setprecision(3) << anomaly.anomalyScore
                                  << " (Streak: " << bottomAnomalyStreak << ")\n";
                        streamServer.broadcastAlarmFrame(2, frameBottom);
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        // Корректная остановка при выходе по Ctrl+C
        std::cout << "\n[SYSTEM] Завершение работы. Закрываем шторку и модули...\n";
        if (g_gate) g_gate->close();
        topCam.stop();
        bottomCam.stop();
        streamServer.stop();

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Системная ошибка: " << e.what() << '\n';
        if (g_gate) g_gate->close();
        return 1;
    }

    std::cout << "[SYSTEM] Комплекс успешно обесточен.\n";
    return 0;
}