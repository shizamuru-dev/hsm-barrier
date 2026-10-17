#pragma once

#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>


class CameraStream {
public:
    explicit CameraStream(std::string devicePath, int width = 640, int height = 480);
    ~CameraStream();

    CameraStream(const CameraStream&) = delete;
    CameraStream& operator=(const CameraStream&) = delete;

    bool open();
    void start();
    void stop();

    // Забирает последний актуальный кадр
    bool getLatestFrame(cv::Mat& outFrame);

    static std::string findDevicePath(const std::string& preferred = "",
                                      const std::string& excluded = "");
    [[nodiscard]] const std::string& devicePath() const { return m_devicePath; }

    [[nodiscard]] bool isRunning() const { return m_running; }

private:
    void captureLoop();

    std::string m_devicePath;
    int m_width;
    int m_height;

    cv::VideoCapture m_cap;
    cv::Mat m_latestFrame;
    std::mutex m_frameMutex;
    
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_hasNewFrame{false};
    std::thread m_workerThread;
};
