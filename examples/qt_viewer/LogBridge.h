/**
 * @file LogBridge.h
 * @brief 把引擎的 LogCallback 接到 Qt 的信号上。
 *
 * 引擎的日志回调「可能来自产生消息的那个线程」，而 Qt 的控件只能在 GUI 线程里
 * 碰。信号槽正好解决这件事：跨线程发射会自动排队到接收者所在的线程，所以回调里
 * 只管 emit，日志窗口那边照常收。
 */
#ifndef CADGEOM_QT_VIEWER_LOGBRIDGE_H
#define CADGEOM_QT_VIEWER_LOGBRIDGE_H

#include <cadgeom/CadGeom.h>

#include <QObject>
#include <QString>

/// @brief 单例；必须在 GUI 线程里第一次取用，它的线程亲缘决定了排队的去向。
class LogBridge : public QObject {
    Q_OBJECT

public:
    static LogBridge& instance();

    /// @brief 交给 EngineDesc::logCallback 的那个函数指针。
    static void Callback(cadgeom::LogLevel level, const char* message, void* userData);

signals:
    /// @param level cadgeom::LogLevel 的整数值。
    void message(int level, const QString& text);

private:
    LogBridge() = default;
};

#endif // CADGEOM_QT_VIEWER_LOGBRIDGE_H
