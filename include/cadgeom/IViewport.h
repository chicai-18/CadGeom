// CadGeom — a render target plus the input funnel that drives it.
#ifndef CADGEOM_IVIEWPORT_H
#define CADGEOM_IVIEWPORT_H

#include <cadgeom/Export.h>
#include <cadgeom/Types.h>

namespace cadgeom {

class ICamera;

/// One window (or off-screen surface) showing the scene.
///
/// When the surface is host-owned, the host owns the message loop: it forwards
/// input through OnMouseEvent/OnKeyEvent and size changes through Resize(). The
/// engine never reads the OS event queue in that mode (docs/architecture.md §4.4).
class CADGEOM_API IViewport {
public:
    /// Destroys the swapchain and the surface. Safe to call while other
    /// viewports remain alive.
    virtual void Release() = 0;

    virtual ICamera* GetCamera() = 0;
    virtual const ICamera* GetCamera() const = 0;

    // -- Surface ------------------------------------------------------------

    /// Recreates the swapchain. A zero dimension (minimized window) is accepted
    /// and simply suspends rendering until a non-zero size arrives.
    virtual CgResult Resize(uint32_t width, uint32_t height) = 0;
    virtual void GetSize(uint32_t& width, uint32_t& height) const = 0;

    /// True for a Glfw-backed viewport whose window the user has closed. A
    /// standalone host polls this to decide when to quit.
    virtual bool ShouldClose() const = 0;

    // -- Appearance ---------------------------------------------------------

    virtual void SetBackgroundColor(const Color& color) = 0;
    virtual void SetRenderMode(RenderMode mode) = 0;
    virtual RenderMode GetRenderMode() const = 0;
    virtual void SetGridVisible(bool visible) = 0;
    virtual bool IsGridVisible() const = 0;

    // -- Work plane (§6.1) --------------------------------------------------

    virtual void SetWorkPlane(const WorkPlane& plane) = 0;
    virtual void GetWorkPlane(WorkPlane& out) const = 0;
    /// @brief 把拾取到的面设为工作平面 —— 「点一个面、在上面画、再拉伸」这条路
    ///        就是从这里开始的。
    /// @param pick 命中结果；新平面过 `pick.point`，法线取 `pick.normal`。
    /// @return kind 为 PickKind::None、或法线是零向量时返回 InvalidArgument。
    /// @note 圆、圆弧和矩形会把自己的承载平面作为法线报出来，所以在 M4 的实体做
    ///       出来之前，对着一条平面曲线调用它就已经有意义了。
    virtual CgResult SetWorkPlaneFromPick(const PickResult& pick) = 0;

    // -- Input --------------------------------------------------------------

    /// Returns true when the engine consumed the event, so a host embedding the
    /// viewport in its own UI knows whether to keep processing it.
    virtual bool OnMouseEvent(const MouseEvent& e) = 0;
    virtual bool OnKeyEvent(const KeyEvent& e) = 0;

    /// Which sub-elements picking may return in this viewport. See PickFilter.
    virtual void SetPickFilter(uint32_t filter) = 0;
    virtual uint32_t GetPickFilter() const = 0;

    /// @brief 在某个像素上做一次拾取，与当前工具无关。
    /// @param x,y 视口像素坐标，左上原点、y 向下。
    /// @return 没命中为 false。**这不是错误**，不会写进 GetLastError() —— 宿主
    ///         可以每帧调用它来做悬停高亮，不必担心刷出一串假故障。
    /// @note 容差是屏幕空间的（约 6 像素），所以正交和透视下手感一致。
    virtual bool Pick(double x, double y, uint32_t pickFilter, PickResult& out) const = 0;

    // -- Frame --------------------------------------------------------------

    /// Renders one frame and presents it. Returns CgResult::Ok when a frame was
    /// presented, or DeviceLost when the device needs recreating.
    virtual CgResult Render() = 0;

    /// Writes the last presented frame to a PNG. Path is UTF-8.
    virtual CgResult SaveScreenshot(const char* utf8Path) = 0;

protected:
    virtual ~IViewport() = default;
};

} // namespace cadgeom

#endif // CADGEOM_IVIEWPORT_H
