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
    /// Adopts a picked face as the work plane — the move that makes
    /// "click a face, draw on it, extrude" work.
    virtual CgResult SetWorkPlaneFromPick(const PickResult& pick) = 0;

    // -- Input --------------------------------------------------------------

    /// Returns true when the engine consumed the event, so a host embedding the
    /// viewport in its own UI knows whether to keep processing it.
    virtual bool OnMouseEvent(const MouseEvent& e) = 0;
    virtual bool OnKeyEvent(const KeyEvent& e) = 0;

    /// Which sub-elements picking may return in this viewport. See PickFilter.
    virtual void SetPickFilter(uint32_t filter) = 0;
    virtual uint32_t GetPickFilter() const = 0;

    /// One-off pick at a pixel, independent of the active tool.
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
