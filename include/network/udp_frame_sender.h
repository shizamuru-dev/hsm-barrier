#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <arpa/inet.h>

class UdpFrameSender {
public:
    UdpFrameSender(std::string targetIp, int targetPort);
    ~UdpFrameSender();

    bool init();
    void sendAlarmFrame(const cv::Mat& rawFrame);

private:
    void workerLoop();

    std::string m_ip;
    int m_port;
    int m_sockFd{-1};
    sockaddr_in m_destAddr{};

    std::atomic<bool> m_running{false};
    std::thread m_worker;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    cv::Mat m_pendingFrame;
    bool m_hasFrame{false};
};