/**
 * @file CadGeomWidget.h
 * @brief 把一个 IViewport 嵌进 QWidget —— 「宿主拥有窗口和消息循环」那一半
 *        （docs/architecture.md §4.4）。
 *
 * 与 examples/glfw_viewer 的分工正好相反：那边用 SurfaceKind::Glfw，窗口是引擎
 * 开的，键盘鼠标也由引擎自己收；这边用 SurfaceKind::NativeWin32，引擎只拿到一个
 * HWND 去建 VkSurfaceKHR，**一条系统消息都不碰**。于是这个控件要做三件事：
 *
 *   1. 让 Qt 交出一个真实的 HWND，并且别再往上面画东西；
 *   2. 把 Qt 的鼠标 / 键盘 / 尺寸变化翻译成 MouseEvent / KeyEvent / Resize；
 *   3. 每帧调用一次 IViewport::Render()（Tick 由宿主统一做，见 MainWindow）。
 *
 * 第 1 件事是 Qt 侧唯一的窍门：`WA_NativeWindow` 换来 HWND，`WA_PaintOnScreen`
 * 加上 `paintEngine()` 返回 null 让 Qt 放弃这块区域的双缓冲绘制，否则 Qt 的
 * backing store 会盖在 Vulkan 交换链画出来的画面上，表现为闪烁或者纯灰。
 */
#ifndef CADGEOM_QT_VIEWER_CADGEOMWIDGET_H
#define CADGEOM_QT_VIEWER_CADGEOMWIDGET_H

#include <cadgeom/CadGeom.h>

#include <QWidget>

/// @brief 一个视口 = 一个控件。开第二个只是再 new 一个它（视图 → 新建视口）。
class CadGeomWidget : public QWidget {
    Q_OBJECT

public:
    /// @param engine 引擎，控件不持有它；引擎必须活得比控件久。
    /// @param desc   视口描述；`surface` 字段会被本控件覆盖成自己的 HWND 和尺寸。
    CadGeomWidget(cadgeom::ICadEngine& engine, const cadgeom::ViewportDesc& desc,
                  QWidget* parent = nullptr);
    ~CadGeomWidget() override;

    /// @return 视口指针；还没建出来（或建失败）时为 null。
    cadgeom::IViewport* viewport() const { return viewport_; }

    /// @brief 画一帧。**必须在宿主调过 ICadEngine::Tick() 之后调用** —— 没有 Tick
    ///        的一帧画的是上一帧的场景。
    void renderFrame();

    /// @brief 光标最后停留的位置，**设备像素**、左上原点，也就是引擎认的那套坐标。
    /// @note 「把光标下的面设成工作平面」这类菜单动作要靠它：菜单里没有鼠标位置。
    void lastCursorPixel(double& x, double& y) const;

    /// @brief 控件当前的设备像素尺寸（高 DPI 下不等于 width()/height()）。
    void devicePixelSize(uint32_t& width, uint32_t& height) const;

    /// Qt 的绘制引擎在这里被彻底关掉，见文件头。
    QPaintEngine* paintEngine() const override { return nullptr; }
    QSize sizeHint() const override { return QSize(960, 640); }

signals:
    /// @brief 视口建成了（第一次显示时才会发生 —— 在那之前没有 HWND）。
    void viewportReady();
    /// @brief 视口没建起来，`reason` 是 GetLastErrorMessage() 的原文。
    void viewportFailed(const QString& reason);
    /// @brief 用户碰了这个控件；宿主用它决定「当前视口」是哪一个。
    void activated();
    /// @brief 转发过一次输入事件，且引擎吃掉了它 —— 宿主借此立刻刷新状态栏。
    void engineConsumedInput();

protected:
    void showEvent(QShowEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* e) override;
#else
    void enterEvent(QEvent* e) override;
#endif
    void leaveEvent(QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;

private:
    /// @brief 第一次显示时建视口：在那之前 winId() 给不出一个已经存在的窗口。
    bool ensureViewport();
    /// @brief 把 Qt 的鼠标事件翻成引擎的，顺手记下光标位置。
    void dispatchMouse(const QPointF& localPos, cadgeom::MouseButton button,
                       cadgeom::MouseAction action, double wheelDelta, Qt::KeyboardModifiers mods);
    void dispatchKey(QKeyEvent* e, cadgeom::KeyAction action);

    cadgeom::ICadEngine& engine_;
    cadgeom::ViewportDesc desc_{};
    cadgeom::IViewport* viewport_{nullptr};
    /// 建过一次就不再重试：失败的原因通常是「这个构建里没有渲染器」，重试只会刷屏。
    bool creationAttempted_{false};
    /// 设备像素坐标，见 lastCursorPixel()。
    double cursorX_{0.0};
    double cursorY_{0.0};
};

#endif // CADGEOM_QT_VIEWER_CADGEOMWIDGET_H
