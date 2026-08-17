/**
 * @file LogSink.h
 * @brief 把引擎的 LogCallback 接到 MFC 的日志面板上。
 *
 * 引擎的日志回调「可能来自产生消息的那个线程」，而 Win32 控件只能在建它的那个
 * 线程里碰。所以回调只做两件事：把消息复制进一个加锁的队列，然后 PostMessage 叫
 * 醒主窗口 —— `PostMessage` 是少数几个明确允许跨线程调用的 API 之一。
 *
 * 队列而不是「PostMessage 带一个 new 出来的字符串」：窗口还没建起来的时候也会有
 * 日志（引擎创建阶段那几条），那时没有 HWND 可投递，消息只能先攒着。攒在队列里
 * 的会在面板出现后的第一次 Drain() 里补上，用指针投递的话它们只能丢掉。
 *
 * 对照 qt_viewer/LogBridge.h：Qt 那边用跨线程排队的信号槽，一个道理的两种写法。
 */
#ifndef CADGEOM_MFC_VIEWER_LOGSINK_H
#define CADGEOM_MFC_VIEWER_LOGSINK_H

#include <afxmt.h>
#include <afxwin.h>

#include <cadgeom/CadGeom.h>

#include <deque>

/// @brief 主窗口收到它就去 Drain() 一次日志队列。
#define WM_CADGEOM_LOG (WM_APP + 1)

/// @brief 一条日志。
struct LogRecord {
    int level{static_cast<int>(cadgeom::LogLevel::Info)};
    CString text;
};

/// @brief 单例日志槽。线程安全，先于窗口存在。
class LogSink {
public:
    static LogSink& Instance();

    /// @brief 交给 EngineDesc::logCallback 的那个函数指针。
    static void Callback(cadgeom::LogLevel level, const char* message, void* userData);

    /// @brief 告诉它去叫醒谁。传 null 表示窗口没了，之后只攒不叫。
    void SetWakeWindow(HWND wnd);

    /// @brief 取走一条；队列空时返回 false。
    bool Pop(LogRecord& out);

    /// @brief 宿主自己写一条（「场景已清空」这类不是引擎发的消息）。
    void Push(cadgeom::LogLevel level, const CString& text);

private:
    LogSink() = default;

    CCriticalSection lock_;
    std::deque<LogRecord> queue_;
    HWND wake_{nullptr};
};

#endif // CADGEOM_MFC_VIEWER_LOGSINK_H
