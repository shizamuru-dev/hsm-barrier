#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <vector>
#include <iomanip>
#include <csignal>
#include <atomic>
#include <algorithm>
#include <filesystem>

#include "../include/hardware/i2c_bus.h"
#include "../include/hardware/pca9685.h"
#include "../include/hardware/servo.h"
#include "../include/hardware/l298n.h"
#include "../include/hardware/camera_stream.h"
#include "../include/npu/npu_model.h"
#include "../include/npu/yolo_postprocess.h"
#include "../include/npu/patchcore_engine.h"
#include "../include/network/stream_server.h"
#include "../include/logging.h"

// Глобальные флаги и дескрипторы для безопасного завершения
std::atomic<bool> g_running{true};
std::shared_ptr<PCA9685> g_pca = nullptr;
std::unique_ptr<Servo> g_gate = nullptr;
std::unique_ptr<L298N> g_leds = nullptr;

void sigHandler(int) {
    std::cout << "\n[SYSTEM] Получен сигнал остановки (SIGINT/SIGTERM). Закрытие шторки...\n";
    g_running = false;
    if (g_leds) {
        g_leds->allOff();
    }
    if (g_gate) {
        g_gate->close();
    }
}

int main() {
    std::signal(SIGINT, sigHandler);
    std::signal(SIGTERM, sigHandler);

    try {
        LOG_INFO("system", "Запуск аппаратного рубежа ВСМ");

        // =====================================================================
        // 1. АППАРАТНАЯ ЧАСТЬ (I2C-7 для PCA9685 / Сервошторки + GPIO для LED)
        // =====================================================================
        auto bus7 = std::make_shared<I2CBus>("/dev/i2c-7");
        g_pca = std::make_shared<PCA9685>(bus7, 0x61);
        g_pca->init(50.0f);

        g_gate = std::make_unique<Servo>(g_pca, 15);
        g_gate->close(); // Исходное состояние: закрыто (ждём состав)
        std::cout << "[HARDWARE] Защитная шторка установлена в положение CLOSE (0°).\n";

        // Инициализация драйвера светодиодов L298N
        constexpr uint8_t LED_IN1 = 8, LED_IN2 = 9, LED_IN3 = 10, LED_IN4 = 11;
        constexpr uint8_t LED_ENA = 0, LED_ENB = 1;

        g_leds = std::make_unique<L298N>(g_pca, LED_IN1, LED_IN2, LED_IN3, LED_IN4,
                                         LED_ENA, LED_ENB);
        if (!g_leds->init()) {
            std::cerr << "[HARDWARE] Предупреждение: не удалось инициализировать светодиоды!\n";
            g_leds = nullptr;
        } else {
            g_leds->allOff(); // Светодиоды выключены при старте
            std::cout << "[HARDWARE] Светодиоды инициализированы через PCA9685 (каналы "
                      << LED_IN1 << ", " << LED_IN2 << ", " << LED_IN3 << ", " << LED_IN4 << ").\n";
        }

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
        if (!yoloModel.load("/root/tmp/hsm-barrier/models/yolov8n.om")) {
            std::cerr << "[NPU] Ошибка загрузки models/yolov8n.om!\n";
            return 1;
        }

        std::cout << "[NPU] Загрузка модели patchcore.om (Нижний контур)...\n";
        PatchcoreEngine patchcore(0);
        if (!patchcore.load("/root/tmp/hsm-barrier/models/patchcore.om")) {
            std::cerr << "[NPU] Ошибка загрузки models/patchcore.om!\n";
            return 1;
        }

        // =====================================================================
        // 4. ОПТИЧЕСКИЕ ПОТОКИ. Имена создаются udev и не зависят от videoN.
        // =====================================================================
        const std::string topPath = "/dev/camera-top";
        const std::string bottomPath = "/dev/camera-bottom";
        if (!std::filesystem::exists(topPath)) {
            LOG_ERROR("camera", "Не найден udev-путь верхней камеры: ", topPath,
                      ". Проверьте правила /etc/udev/rules.d/99-hsm-cameras.rules");
            return 1;
        }
        CameraStream topCam(topPath, 640, 640);
        if (!topCam.open()) {
            LOG_ERROR("camera", "Не удалось открыть верхнюю камеру ", topPath);
            return 1;
        }
        topCam.start();

        CameraStream bottomCam(bottomPath, 640, 480);
        if (!std::filesystem::exists(bottomPath)) {
            LOG_WARN("camera", "Не найден udev-путь нижней камеры: ", bottomPath,
                     ". Контур аномалий отключён");
        } else if (!bottomCam.open()) {
            LOG_WARN("camera", "Нижняя камера недоступна");
        } else {
            bottomCam.start();
        }

        std::cout << "[PIPELINE] Комплекс активен.\n";

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
                    if (g_leds) {
                        g_leds->setIN1(false);
                        g_leds->setIN2(true);
                        g_leds->setIN3(true);
                        g_leds->setIN4(false);
                        std::cout << "💡 [LED] Светодиоды включены.\n";
                    }
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
        if (g_leds) {
            g_leds->allOff();
            std::cout << "[LED] Светодиоды выключены.\n";
        }
        if (g_gate) g_gate->close();
        topCam.stop();
        bottomCam.stop();
        streamServer.stop();

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Системная ошибка: " << e.what() << '\n';
        if (g_leds) g_leds->allOff();
        if (g_gate) g_gate->close();
        return 1;
    }

    std::cout << "[SYSTEM] Комплекс успешно обесточен.\n";
    return 0;
}
