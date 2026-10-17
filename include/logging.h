#pragma once

#include <sstream>
#include <string_view>
#include <utility>

namespace logging {
enum class Level { Debug = 0, Info, Warning, Error };

void write(Level level, std::string_view component, std::string_view message);

template <typename... Args>
void log(Level level, std::string_view component, Args&&... args) {
    std::ostringstream message;
    (message << ... << std::forward<Args>(args));
    write(level, component, message.str());
}
}

#define LOG_DEBUG(component, ...) logging::log(logging::Level::Debug, component, __VA_ARGS__)
#define LOG_INFO(component, ...) logging::log(logging::Level::Info, component, __VA_ARGS__)
#define LOG_WARN(component, ...) logging::log(logging::Level::Warning, component, __VA_ARGS__)
#define LOG_ERROR(component, ...) logging::log(logging::Level::Error, component, __VA_ARGS__)
