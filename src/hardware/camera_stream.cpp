//
// Created by cyn on 9/20/26.
//

#include "../../include/hardware/camera_stream.h"

#include <iostream>
#include <chrono>

CameraStream::CameraStream(const int deviceIndex, const int width, const int height)
    : m_deviceIndex(deviceIndex), m_width(width), m_height(height) {}

CameraStream::~CameraStream() {
    stop();
}

bool CameraStream::open() {
    // Открываем через V4L2 бэкенд
    m_cap.open(m_deviceIndex, cv::CAP_V4L2);
    if (!m_cap.isOpened()) {
        std::cerr << "[Camera " << m_deviceIndex << "] Ошибка открытия устройства\n";
        return false;
    }

    // Включаем MJPEG для быстрого потока
    m_cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    m_cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
    m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    m_cap.set(cv::CAP_PROP_AUTOFOCUS, 0); // Отключаем плавающий автофокус

    // Минимизируем внутренний буфер драйвера V4L2 до 1 кадра
    m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

    return true;
}

void CameraStream::start() {
    if (m_running) return;
    m_running = true;
    m_workerThread = std::thread(&CameraStream::captureLoop, this);
}

void CameraStream::stop() {
    if (!m_running) return;
    m_running = false;

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    if (m_cap.isOpened()) {
        m_cap.release();
    }
}

void CameraStream::captureLoop() {
    cv::Mat tempFrame;
    while (m_running) {
        if (!m_cap.read(tempFrame) || tempFrame.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            m_latestFrame = tempFrame.clone();
            m_hasNewFrame = true;
        }
    }
}

bool CameraStream::getLatestFrame(cv::Mat& outFrame) {
    if (!m_hasNewFrame) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_frameMutex);
    outFrame = m_latestFrame.clone();
    m_hasNewFrame = false;
    return true;
}