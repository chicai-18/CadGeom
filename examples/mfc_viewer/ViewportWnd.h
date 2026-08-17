/**
 * @file ViewportWnd.h
 * @brief 把一个 IViewport 嵌进一个 CWnd —— 「宿主拥有窗口和消息循环」那一半
 *        （docs/architecture.md §4.4）。
 *
 * 与 examples/glfw_viewer 的分工正好相反：那边用 SurfaceKind::Glfw，窗口是引擎
 * 开的，键盘鼠标也由引擎自己收；这边用 SurfaceKind::NativeWin32，引擎只拿到一个
 * HWND 去建 VkSurfaceKHR，**一条系统消息都不碰**。于是这个窗口要做三件事：
 *
 *   1. 提供一个不被别人画的 HWND；
 *   2. 把 WM_ 消息翻译成 MouseEvent / KeyEvent / Resize；
 *   3. 每帧调用一次 IViewport::Render()（Tick 由宿主统一做，见 CMainFrame）。
 *
 * 第 1 件事在 MFC 这边比 Qt 简单得多：Qt 要 `WA_NativeWindow` 换 HWND、要
 * `WA_PaintOnScreen` 加一个返回 null 的 `paintEngine()` 才能让它别再往上面画；
 * 这里注册窗口类时**不给背景刷**、`OnEraseBkgnd` 返回 TRUE 就完了 —— CWnd 本来
 * 就是一个真实的 HWND，本来就没有 backing store 盖在上面。
 */
#ifndef CADGEOM_MFC_VIEWER_VIEWPORTWND_H
#define CADGEOM_MFC_VIEWER_VIEWPORTWND_H

#include <afxwin.h>

#include <cadgeom/CadGeom.h>

class CMainFrame;

/// @brief 一个视口 = 一个子窗口。开第二个只是再 new 一个它（视图 → 新建视口）。
class CViewportWnd : public CWnd {
public:
    /// @param engine 引擎，本窗口不持有它；引擎必须活得比它久。
    /// @param owner  主窗口，用来回报「视口建好了 / 建不起来 / 用户碰了我」。
    /// @param desc   视口描述；`surface` 字段会被覆盖成自己的 HWND 和尺寸。
    CViewportWnd(cadgeom::ICadEngine& engine, CMainFrame* owner,
                 const cadgeom::ViewportDesc& desc);
    ~CViewportWnd() override;

    /// @brief 建这个子窗口。视口本身要等到有了非零尺寸才建（见 EnsureViewport）。
    BOOL CreateIn(CWnd* parent, const CRect& rect, UINT id);

    /// @return 视口指针；还没建出来（或建失败）时为 null。
    cadgeom::IViewport* Viewport() const { return viewport_; }

    /// @brief 第二个及以后的视口。主窗口据此给它一套不同的初始视角（正视 + 隐藏
    ///        线），一眼看出视口之间只共享设备和几何。
    void SetSecondary(bool secondary) { secondary_ = secondary; }
    bool IsSecondary() const { return secondary_; }

    /// @brief 画一帧。**必须在宿主调过 ICadEngine::Tick() 之后调用** —— 没有 Tick
    ///        的一帧画的是上一帧的场景。
    void RenderFrame();

    /// @brief 光标最后停留的位置，客户区像素、左上原点，也就是引擎认的那套坐标。
    /// @note 「把光标下的面设成工作平面」这类菜单动作要靠它：菜单里没有鼠标位置。
    void LastCursorPixel(double& x, double& y) const;

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnDestroy();
    afx_msg void OnSetFocus(CWnd* oldWnd);

    afx_msg void OnLButtonDown(UINT flags, CPoint point);
    afx_msg void OnLButtonUp(UINT flags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT flags, CPoint point);
    afx_msg void OnRButtonDown(UINT flags, CPoint point);
    afx_msg void OnRButtonUp(UINT flags, CPoint point);
    afx_msg void OnRButtonDblClk(UINT flags, CPoint point);
    afx_msg void OnMButtonDown(UINT flags, CPoint point);
    afx_msg void OnMButtonUp(UINT flags, CPoint point);
    afx_msg void OnMButtonDblClk(UINT flags, CPoint point);
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
    afx_msg void OnMouseLeave();
    afx_msg void OnKeyDown(UINT ch, UINT repeat, UINT flags);
    afx_msg void OnKeyUp(UINT ch, UINT repeat, UINT flags);

    DECLARE_MESSAGE_MAP()

private:
    /// @brief 第一次拿到非零尺寸时建视口 —— 交换链要一个有面积的窗口。
    bool EnsureViewport();
    /// @brief 按下时抓住鼠标，抬起时放开；拖到窗口外面也要继续收到移动事件。
    void PressButton(cadgeom::MouseButton button, CPoint point, cadgeom::MouseAction action);
    void ReleaseButton(cadgeom::MouseButton button, CPoint point);
    void DispatchMouse(CPoint point, cadgeom::MouseButton button, cadgeom::MouseAction action,
                       double wheelDelta);
    void DispatchKey(UINT vk, cadgeom::KeyAction action, bool autoRepeat);

    cadgeom::ICadEngine& engine_;
    CMainFrame* owner_{nullptr};
    cadgeom::ViewportDesc desc_{};
    cadgeom::IViewport* viewport_{nullptr};
    /// 建过一次就不再重试：失败的原因通常是「这个构建里没有渲染器」，重试只会刷屏。
    bool creationAttempted_{false};
    bool secondary_{false};
    /// 还按着几个键 —— 全抬起来才 ReleaseCapture。
    int buttonsDown_{0};
    /// 已经要过 WM_MOUSELEAVE 了，别重复要。
    bool trackingLeave_{false};
    double cursorX_{0.0};
    double cursorY_{0.0};
};

#endif // CADGEOM_MFC_VIEWER_VIEWPORTWND_H
