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

// Флаг для корректного выхода по Ctrl+C без зависания V4L2
std::atomic<bool> g_running{true};

void sigHandler(int) {
    std::cout << "\n[SYSTEM] Получен сигнал остановки. Завершение работы...\n";
    g_running = false;
}

int main() {
    std::signal(SIGINT, sigHandler);
    std::signal(SIGTERM, sigHandler);

    try {
        std::cout << "[SYSTEM] Запуск аппаратного рубежа ВСМ...\n";

        // 1. АППАРАТНАЯ ЧАСТЬ (I2C / PCA9685 / Сервошторка)
        auto bus = std::make_shared<I2CBus>("/dev/i2c-7");
        auto pca = std::make_shared<PCA9685>(bus, 0x61);
        pca->init(50.0f);

        Servo gate(pca, 15);
        std::cout << "[HARDWARE] Открываем защитную шторку нижнего короба...\n";
        if (!gate.open()) {
            std::cerr << "[HARDWARE] Ошибка открытия сервопривода!\n";
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(800));

        // 2. СЕТЕВОЙ СЕРВЕР ТЕРМИНАЛА ОПЕРАТОРА
        StreamServer streamServer(8088);
        if (!streamServer.start()) {
            std::cerr << "[NETWORK] Не удалось запустить StreamServer на порту 8088!\n";
            return 1;
        }

        // 3. ИНИЦИАЛИЗАЦИЯ НЕЙРОСЕТЕЙ НА NPU (Ascend 310B)
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

        // 4. ОПТИЧЕСКИЕ ПОТОКИ (Камера 0 - Верх, Камера 4 - Низ)
        std::cout << "[CAMERA] Подключение к /dev/video0 (Столб)...\n";
        CameraStream topCam(0, 640, 640);
        if (!topCam.open()) {
            std::cerr << "[CAMERA] Ошибка: не удалось открыть /dev/video0!\n";
            return 1;
        }
        topCam.start();

        std::cout << "[CAMERA] Подключение к /dev/video4 (Короб)...\n";
        CameraStream bottomCam(4, 640, 480);
        if (!bottomCam.open()) {
            std::cerr << "[CAMERA] Предупреждение: /dev/video4 недоступна, нижний контур оффлайн!\n";
        } else {
            bottomCam.start();
        }

        std::cout << "[PIPELINE] Комплекс активен. Ожидание прохода состава...\n";

        cv::Mat frameTop;
        cv::Mat frameBottom;
        std::vector<float> yoloOutput;
        yoloOutput.reserve(84 * 8400);

        auto lastTopAlert = std::chrono::steady_clock::now();
        auto lastBottomAlert = std::chrono::steady_clock::now();

        // Порог детекции аномалии
        constexpr float ANOMALY_THRESHOLD = 0.80f;

        // ---------------------------------------------------------------------
        // ПАРАМЕТРЫ ЗАЩИТЫ ОТ ДРЕБЕЗГА (DEBOUNCE FILTER)
        // ---------------------------------------------------------------------
        int topPersonStreak = 0;
        constexpr int TOP_STREAK_TRIGGER = 2; // Сколько кадров подряд должен быть виден человек

        int bottomAnomalyStreak = 0;
        constexpr int BOTTOM_STREAK_TRIGGER = 2; // Сколько кадров подряд должен быть скачок аномалии

        while (g_running) {
            // --- КОНТУР 1: ВЕРХ (КРЫША / ЛЮДИ) ---
            if (topCam.getLatestFrame(frameTop)) {
                std::vector<float> inputTensor = fastPreprocessToNCHW(frameTop, 640, 640);

                if (yoloModel.execute(inputTensor, yoloOutput)) {
                    auto detections = postprocessYOLOv8(yoloOutput, frameTop.cols, frameTop.rows, 0.45f, 0.50f);

                    int personCount = 0;
                    for (const auto& det : detections) {
                        if (det.classId == 0) personCount++;
                    }

                    // Антидребезговый интегратор
                    if (personCount > 0) {
                        topPersonStreak = std::min(topPersonStreak + 1, 10);
                    } else {
                        topPersonStreak = std::max(0, topPersonStreak - 1);
                    }

                    // Срабатывание только при преодолении фильтра
                    if (topPersonStreak >= TOP_STREAK_TRIGGER) {
                        std::cout << "🚨 [ALERT ВЕРХ] Подтверждено присутствие человека! (" << personCount << " чел)\n";

                        auto now = std::chrono::steady_clock::now();
                        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTopAlert).count() > 400) {
                            lastTopAlert = now;
                            streamServer.broadcastAlarmFrame(1, frameTop);
                        }
                    }
                }
            }

            // --- КОНТУР 2: НИЗ (ДНИЩЕ / АНОМАЛИИ) ---
            if (bottomCam.isRunning() && bottomCam.getLatestFrame(frameBottom)) {
                const AnomalyResult anomaly = patchcore.detect(frameBottom, ANOMALY_THRESHOLD);

                // Антидребезговый интегратор
                if (anomaly.isAnomaly) {
                    bottomAnomalyStreak = std::min(bottomAnomalyStreak + 1, 10);
                } else {
                    bottomAnomalyStreak = std::max(0, bottomAnomalyStreak - 1);
                }

                // Срабатывание только при подтверждении на серии кадров
                if (bottomAnomalyStreak >= BOTTOM_STREAK_TRIGGER) {
                    std::cout << "🚨 [ALERT НИЗ] Подтверждена аномалия днища! Score: "
                              << std::fixed << std::setprecision(3) << anomaly.anomalyScore
                              << " (Streak: " << bottomAnomalyStreak << ")\n";

                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBottomAlert).count() > 400) {
                        lastBottomAlert = now;
                        streamServer.broadcastAlarmFrame(2, frameBottom);
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        // Корректная остановка при выходе
        std::cout << "[SYSTEM] Закрываем оптические модули...\n";
        topCam.stop();
        bottomCam.stop();
        streamServer.stop();
        gate.close();

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Системная ошибка: " << e.what() << '\n';
        return 1;
    }

    std::cout << "[SYSTEM] Комплекс успешно обесточен.\n";
    return 0;
}