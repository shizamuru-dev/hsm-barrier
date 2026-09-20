#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <netinet/in.h>

#pragma pack(push, 1)
struct FramePacketHeader {
    uint16_t channelId;    // 1 - Top/Roof (YOLO), 2 - Bottom/Undercarriage (PatchCore)
    uint16_t reserved;     // 0x0000
    uint32_t payloadSize;  // Размер JPEG буфера
};
#pragma pack(pop)

class StreamServer {
public:
    explicit StreamServer(uint16_t port = 8088);
    ~StreamServer();

    StreamServer(const StreamServer&) = delete;
    StreamServer& operator=(const StreamServer&) = delete;

    bool start();
    void stop();

    // Неблокирующая постановка чистого кадра в очередь отправки
    void broadcastAlarmFrame(uint16_t channelId, const cv::Mat& rawFrame);

private:
    void acceptLoop();
    void sendWorkerLoop();

    uint16_t m_port;
    int m_serverFd{-1};
    std::atomic<bool> m_running{false};

    std::thread m_acceptThread;
    std::thread m_workerThread;

    std::mutex m_clientsMutex;
    std::vector<int> m_clientSockets;

    struct OutgoingPacket {
        uint16_t channelId;
        std::vector<uint8_t> jpegData;
    };

    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::vector<OutgoingPacket> m_packetQueue;
    static constexpr size_t MAX_QUEUE_SIZE = 5;
};