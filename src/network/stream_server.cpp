 //

// Created by cyn on 9/20/26.

//


#include "../include/network/stream_server.h"

#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>

StreamServer::StreamServer(const uint16_t port) : m_port(port) {}

StreamServer::~StreamServer() {
    stop();
}

bool StreamServer::start() {
    m_serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverFd < 0) {
        std::cerr << "[StreamServer] Не удалось создать сокет\n";
        return false;
    }

    int opt = 1;
    setsockopt(m_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Таймаут на accept, чтобы acceptThread не висел при остановке сервера
    timeval tv{.tv_sec = 1, .tv_usec = 0};
    setsockopt(m_serverFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(m_port);

    if (bind(m_serverFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "[StreamServer] Ошибка привязки к порту " << m_port << "\n";
        close(m_serverFd);
        m_serverFd = -1;
        return false;
    }

    if (listen(m_serverFd, 4) < 0) {
        std::cerr << "[StreamServer] Ошибка listen\n";
        close(m_serverFd);
        m_serverFd = -1;
        return false;
    }

    m_running = true;
    m_acceptThread = std::thread(&StreamServer::acceptLoop, this);
    m_workerThread = std::thread(&StreamServer::sendWorkerLoop, this);

    std::cout << "[StreamServer] Сервер терминала запущен на порту " << m_port << "\n";
    return true;
}

void StreamServer::stop() {
    if (!m_running) return;
    m_running = false;

    m_cv.notify_all();

    if (m_acceptThread.joinable()) m_acceptThread.join();
    if (m_workerThread.joinable()) m_workerThread.join();

    if (m_serverFd >= 0) {
        shutdown(m_serverFd, SHUT_RDWR);
        close(m_serverFd);
        m_serverFd = -1;
    }

    std::lock_guard<std::mutex> lock(m_clientsMutex);
    for (const int fd : m_clientSockets) {
        close(fd);
    }
    m_clientSockets.clear();
}

void StreamServer::broadcastAlarmFrame(const uint16_t channelId, const cv::Mat& rawFrame) {
    if (!m_running || rawFrame.empty()) return;

    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        if (m_clientSockets.empty()) return; // Не жмём JPEG, если никто не слушает
    }

    std::vector<uint8_t> buffer;
    if (const std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 75}; !cv::imencode(".jpg", rawFrame, buffer, params)) return;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_packetQueue.size() >= MAX_QUEUE_SIZE) {
            m_packetQueue.erase(m_packetQueue.begin());
        }
        m_packetQueue.push_back({.channelId = channelId, .jpegData = std::move(buffer)});
    }
    m_cv.notify_one();
}

void StreamServer::acceptLoop() {
    while (m_running) {
        sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = accept(m_serverFd, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientFd < 0) {
            // Либо таймаут (раз в 1 сек), либо реальная ошибка — крутимся дальше
            continue;
        }

        // Защищаем сокет клиента от зависания при медленном канале
        timeval sendTv{.tv_sec = 0, .tv_usec = 200000}; // 200 мс
        setsockopt(clientFd, SOL_SOCKET, SO_SNDTIMEO, &sendTv, sizeof(sendTv));

        std::lock_guard<std::mutex> lock(m_clientsMutex);
        m_clientSockets.push_back(clientFd);
        std::cout << "[StreamServer] Подключен терминал оператора (FD: " << clientFd << ")\n";
    }
}

void StreamServer::sendWorkerLoop() {
    while (m_running) {
        OutgoingPacket packet;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_cv.wait(lock, [this] { return !m_packetQueue.empty() || !m_running; });
            if (!m_running) break;

            packet = std::move(m_packetQueue.front());
            m_packetQueue.erase(m_packetQueue.begin());
        }

        FramePacketHeader header{};
        header.channelId = htons(packet.channelId);
        header.reserved = 0;
        header.payloadSize = htonl(static_cast<uint32_t>(packet.jpegData.size()));

        {
            std::vector<int> failedSockets;
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            for (const int clientFd : m_clientSockets) {
                // 1. Отправляем 8 байт заголовка
                if (const ssize_t sentHdr = send(clientFd, &header, sizeof(header), MSG_NOSIGNAL); sentHdr != sizeof(header)) {
                    failedSockets.push_back(clientFd);
                    continue;
                }

                // 2. Отправляем JPEG тело кадра
                if (const ssize_t sentPayload = send(clientFd, packet.jpegData.data(), packet.jpegData.size(), MSG_NOSIGNAL); sentPayload != static_cast<ssize_t>(packet.jpegData.size())) {
                    failedSockets.push_back(clientFd);
                }
            }

            // Очищаем отвалившиеся клиенты
            for (const int deadFd : failedSockets) {
                close(deadFd);
                std::erase(m_clientSockets, deadFd);
                std::cout << "[StreamServer] Отключен терминал оператора (FD: " << deadFd << ")\n";
            }
        }
    }
}