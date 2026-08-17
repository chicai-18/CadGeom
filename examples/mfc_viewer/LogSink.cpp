/**
 * @file LogSink.cpp
 * @brief LogSink 的实现。
 */
#include "LogSink.h"

#include "Utf8.h"

LogSink& LogSink::Instance() {
    static LogSink sink;
    return sink;
}

void LogSink::Callback(cadgeom::LogLevel level, const char* message, void* /*userData*/) {
    // message 只在这次调用期间有效，所以这里必须复制一份。
    Instance().Push(level, FromUtf8(message));
}

void LogSink::SetWakeWindow(HWND wnd) {
    CSingleLock guard(&lock_, TRUE);
    wake_ = wnd;
}

void LogSink::Push(cadgeom::LogLevel level, const CString& text) {
    HWND wake = nullptr;
    {
        CSingleLock guard(&lock_, TRUE);
        LogRecord record;
        record.level = static_cast<int>(level);
        record.text = text;
        queue_.push_back(record);
        // 攒太多说明没人来取（面板还没建起来，或者引擎正刷屏）。丢最老的，别把
        // 内存吃光 —— 面板本身也只留最后几千行。
        while (queue_.size() > 8000) {
            queue_.pop_front();
        }
        wake = wake_;
    }
    // 叫醒动作放在锁外面：PostMessage 只是往队列里塞，但没必要占着锁做。
    if (wake) {
        ::PostMessage(wake, WM_CADGEOM_LOG, 0, 0);
    }
}

bool LogSink::Pop(LogRecord& out) {
    CSingleLock guard(&lock_, TRUE);
    if (queue_.empty()) {
        return false;
    }
    out = queue_.front();
    queue_.pop_front();
    return true;
}
