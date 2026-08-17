/**
 * @file LogBridge.cpp
 * @brief LogBridge 的实现。
 */
#include "LogBridge.h"

LogBridge& LogBridge::instance() {
    static LogBridge bridge;
    return bridge;
}

void LogBridge::Callback(cadgeom::LogLevel level, const char* message, void* /*userData*/) {
    // message 只在这次调用期间有效，所以这里必须复制一份 —— QString 正好复制。
    emit instance().message(static_cast<int>(level), QString::fromUtf8(message ? message : ""));
}
