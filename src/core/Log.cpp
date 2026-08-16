#include "core/Log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace cadgeom::core {
namespace {

std::mutex g_sinkMutex;
LogCallback g_callback = nullptr;
void* g_userData = nullptr;
std::atomic<LogLevel> g_level{LogLevel::Info};

} // namespace

void Logger::SetSink(LogCallback callback, void* userData) noexcept {
    std::lock_guard<std::mutex> lock(g_sinkMutex);
    g_callback = callback;
    g_userData = userData;
}

void Logger::SetLevel(LogLevel level) noexcept {
    g_level.store(level, std::memory_order_relaxed);
}

LogLevel Logger::GetLevel() noexcept {
    return g_level.load(std::memory_order_relaxed);
}

bool Logger::IsEnabled(LogLevel level) noexcept {
    const LogLevel current = g_level.load(std::memory_order_relaxed);
    return current != LogLevel::Off && level >= current;
}

void Logger::Write(LogLevel level, const char* fmt, ...) noexcept {
    if (!IsEnabled(level)) {
        return;
    }

    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    const int written = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (written < 0) {
        return;
    }
    buffer[sizeof(buffer) - 1] = '\0';

    // Held across the call so the sink cannot be swapped out from under it —
    // the callback's contract promises serialized delivery.
    std::lock_guard<std::mutex> lock(g_sinkMutex);
    if (g_callback) {
        g_callback(level, buffer, g_userData);
    }
}

} // namespace cadgeom::core
