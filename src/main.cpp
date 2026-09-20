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
#include "../include/hardware/vl53l0x.h"
#include "../include/hardware/camera_stream.h"
#include "../include/npu/npu_model.h"
#include "../include/npu/yolo_postprocess.h"
#include "../include/npu/patchcore_engine.h"
#include "../include/network/stream_server.h"

std::atomic<bool> g_running{true};
std::shared_ptr<PCA9685> g_pca = nullptr;
std::unique_ptr<Servo> g_gate = nullptr;

void sigHandler(int) {
    std::cout << "\n[SYSTEM] Получен сигнал остановки. Экстренное закрытие шторки...\n";
    g_running = false;
    if (g_gate) {
        g_gate->close();
    }
}

int main() {
    std::signal(SIGINT, sigHandler);
    std::signal(SIGTERM, sigHandler);

    try {
        std::cout << "[SYSTEM] Запуск аппаратного рубежа ВСМ...\n";

        auto bus7 = std::make_shared<I2CBus>("/dev/i2c-7");
        g_pca = std::make_shared<PCA9685>(bus7, 0x61);
        g_pca->init(50.0f);

        g_gate = std::make_unique<Servo>(g_pca, 15);
        g_gate->close(); // Исходное состояние: закрыто от пыли и щебня

        std::cout << "[HARDWARE] Подключение ToF-дальномера VL53L0X на /dev/i2c-6...\n";
        auto bus6 = std::make_shared<I2CBus>("/dev/i2c-6");
        VL53L0X tofSensor(bus6, 0x29); // Дефолтный адрес VL53L0X
        bool tofReady = tofSensor.init();
        if (!tofReady) {
            std::cerr << "[HARDWARE] Внимание: VL53L0X на /dev/i2c-6 не найден! Работаем в форсированном режиме.\n";
            g_gate->open();
        }

        StreamServer streamServer(8088);
        if (!streamServer.start()) {
            std::cerr << "[NETWORK] Не удалось запустить StreamServer на порту 8088!\n";
            return 1;
        }

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

        std::cout << "[PIPELINE] Комплекс активен. Ожидание прохода состава...\n";

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

        bool isTrainPresent = false;
        auto lastTrainSeen = std::chrono::steady_clock::now();

        while (g_running) {
            if (tofReady) {
                int dist = tofSensor.readDistanceMm();
                bool objectInZone = (dist > 0 && dist < 350);

                if (objectInZone) {
                    lastTrainSeen = std::chrono::steady_clock::now();
                    if (!isTrainPresent) {
                        std::cout << "🚂 [GATE] Состав вошел в створ (" << dist << " мм). Открываем сервошторку!\n";
                        g_gate->open();
                        isTrainPresent = true;
                    }
                } else {
                    auto elapsedSinceTrain = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - lastTrainSeen).count();

                    if (isTrainPresent && elapsedSinceTrain > 1500) {
                        std::cout << "💤 [GATE] Состав прошел створ. Закрываем шторку короба.\n";
                        g_gate->close();
                        isTrainPresent = false;
                        bottomAnomalyStreak = 0; // Сброс состояния детекций
                    }
                }
            } else {
                isTrainPresent = true; // Без дальномера держим контур всегда активным
            }

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
                            // Передача сжатого кадра оператору по выделенному каналу 1
                            streamServer.broadcastAlarmFrame(1, frameTop);
                        }
                    }
                }
            }


            if (isTrainPresent && bottomCam.isRunning() && bottomCam.getLatestFrame(frameBottom)) {
                const AnomalyResult anomaly = patchcore.detect(frameBottom, ANOMALY_THRESHOLD);

                if (anomaly.isAnomaly) {
                    bottomAnomalyStreak = std::min(bottomAnomalyStreak + 1, 10);
                } else {
                    bottomAnomalyStreak = 0; // Сбрасываем мгновенно, чтобы не забивать шину
                }

                if (anomaly.isAnomaly && bottomAnomalyStreak >= BOTTOM_STREAK_TRIGGER) {
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBottomAlert).count() > 400) {
                        lastBottomAlert = now;
                        std::cout << "🚨 [ALERT НИЗ] Подтверждена аномалия днища! Score: "
                                  << std::fixed << std::setprecision(3) << anomaly.anomalyScore
                                  << " (Streak: " << bottomAnomalyStreak << ")\n";
                        // Передача сжатого кадра оператору по выделенному каналу 2
                        streamServer.broadcastAlarmFrame(2, frameBottom);
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        // Корректная остановка
        std::cout << "[SYSTEM] Закрываем оптические модули...\n";
        topCam.stop();
        bottomCam.stop();
        streamServer.stop();
        if (g_gate) g_gate->close();

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Системная ошибка: " << e.what() << '\n';
        if (g_gate) g_gate->close();
        return 1;
    }

    std::cout << "[SYSTEM] Комплекс успешно обесточен.\n";
    return 0;
}
