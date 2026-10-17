#include "../include/logging.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>

namespace {
std::mutex logMutex;
logging::Level minimumLevel() {
    const char* value = std::getenv("HSM_LOG_LEVEL");
    if (!value) return logging::Level::Info;
    std::string level(value);
    if (level == "debug") return logging::Level::Debug;
    if (level == "warning" || level == "warn") return logging::Level::Warning;
    if (level == "error") return logging::Level::Error;
    return logging::Level::Info;
}

const char* levelName(logging::Level level) {
    switch (level) {
        case logging::Level::Debug: return "DEBUG";
        case logging::Level::Warning: return "WARN";
        case logging::Level::Error: return "ERROR";
        default: return "INFO";
    }
}
}

void logging::write(const Level level, const std::string_view component,
                   const std::string_view message) {
    if (static_cast<int>(level) < static_cast<int>(minimumLevel())) return;
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_r(&time, &localTime);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::lock_guard lock(logMutex);
    std::cerr << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << '.'
              << std::setfill('0') << std::setw(3) << millis.count()
              << " [" << levelName(level) << "] [" << component << "] [tid="
              << std::this_thread::get_id() << "] " << message << '\n';
}
