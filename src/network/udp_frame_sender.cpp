//
// Created by cyn on 9/20/26.
//

#include "../../include/network/udp_frame_sender.h"
#include <iostream>
#include <vector>

UdpFrameSender::UdpFrameSender(std::string targetIp, int targetPort)
    : m_ip(std::move(targetIp)), m_port(targetPort) {}

UdpFrameSender::~UdpFrameSender() {
    m_running = false;
    m_cv.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
    if (m_sockFd >= 0) {
        close(m_sockFd);
    }
}

bool UdpFrameSender::init() {
    m_sockFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_sockFd < 0) {
        std::cerr << "[UDP:" << m_port << "] Ошибка создания сокета\n";
        return false;
    }

    m_destAddr.sin_family = AF_INET;
    m_destAddr.sin_port = htons(m_port);
    if (inet_pton(AF_INET, m_ip.c_str(), &m_destAddr.sin_addr) <= 0) {
        std::cerr << "[UDP:" << m_port << "] Неверный IP адрес: " << m_ip << "\n";
        return false;
    }

    m_running = true;
    m_worker = std::thread(&UdpFrameSender::workerLoop, this);
    return true;
}

void UdpFrameSender::sendAlarmFrame(const cv::Mat& rawFrame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingFrame = rawFrame.clone();
    m_hasFrame = true;
    m_cv.notify_one();
}

void UdpFrameSender::workerLoop() {
    while (m_running) {
        cv::Mat frameToSend;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_hasFrame || !m_running; });

            if (!m_running) break;

            frameToSend = m_pendingFrame;
            m_hasFrame = false;
        }

        if (frameToSend.empty()) continue;

        // Кодируем сырой кадр в JPEG с качеством 70% (размер ~25-40 КБ, укладывается в один UDP-пакет)
        std::vector<uchar> jpegBuf;
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 70};
        cv::imencode(".jpg", frameToSend, jpegBuf, params);

        // Отправка чистого бинарника JPEG в сокет
        if (jpegBuf.size() <= 65507) { // Максимальный размер данных в одном UDP-пакете
            sendto(m_sockFd, jpegBuf.data(), jpegBuf.size(), 0,
                   reinterpret_cast<sockaddr*>(&m_destAddr), sizeof(m_destAddr));
        } else {
            std::cerr << "[UDP:" << m_port << "] Кадр слишком большой для одиночного UDP!\n";
        }
    }
}