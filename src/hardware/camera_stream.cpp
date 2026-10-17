//
// Created by cyn on 9/20/26.
//

#include "../../include/hardware/camera_stream.h"

#include <iostream>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <vector>
#include "../../include/logging.h"

CameraStream::CameraStream(std::string devicePath, const int width, const int height)
    : m_devicePath(std::move(devicePath)), m_width(width), m_height(height) {}

CameraStream::~CameraStream() {
    stop();
}

bool CameraStream::open() {
    m_cap.open(m_devicePath, cv::CAP_V4L2);
    if (!m_cap.isOpened()) {
        LOG_ERROR("camera", "Не удалось открыть ", m_devicePath);
        return false;
    }

    // Включаем MJPEG для быстрого потока
    m_cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    m_cap.set(cv::CAP_PROP_FRAME_WIDTH, m_width);
    m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, m_height);
    m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    m_cap.set(cv::CAP_PROP_AUTOFOCUS, 0); // Отключаем плавающий автофокус

    // Минимизируем внутренний буфер драйвера V4L2 до 1 кадра
    m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

    return true;
}

std::string CameraStream::findDevicePath(const std::string& preferred,
                                         const std::string& excluded) {
    std::vector<std::string> paths;
    if (!preferred.empty()) paths.push_back(preferred);
    try {
        for (const auto& entry : std::filesystem::directory_iterator("/dev")) {
            const auto path = entry.path().string();
            if (path.starts_with("/dev/video") && path != preferred && path != excluded)
                paths.push_back(path);
        }
    } catch (const std::filesystem::filesystem_error& error) {
        LOG_WARN("camera", "Не удалось перечислить /dev: ", error.what());
    }
    if (!preferred.empty() && paths.size() > 1) {
        std::sort(paths.begin() + 1, paths.end());
    } else {
        std::sort(paths.begin(), paths.end());
    }
    for (const auto& path : paths) {
        cv::VideoCapture probe(path, cv::CAP_V4L2);
        if (probe.isOpened()) {
            probe.release();
            LOG_INFO("camera", "Выбрано устройство ", path);
            return path;
        }
    }
    return {};
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
