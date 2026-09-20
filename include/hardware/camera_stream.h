#pragma once

#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <atomic>


class CameraStream {
public:
    explicit CameraStream(int deviceIndex, int width = 640, int height = 480);
    ~CameraStream();

    CameraStream(const CameraStream&) = delete;
    CameraStream& operator=(const CameraStream&) = delete;

    bool open();
    void start();
    void stop();

    // Забирает последний актуальный кадр
    bool getLatestFrame(cv::Mat& outFrame);

    [[nodiscard]] bool isRunning() const { return m_running; }

private:
    void captureLoop();

    int m_deviceIndex;
    int m_width;
    int m_height;

    cv::VideoCapture m_cap;
    cv::Mat m_latestFrame;
    std::mutex m_frameMutex;
    
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_hasNewFrame{false};
    std::thread m_workerThread;
};