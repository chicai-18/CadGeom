#pragma once

#include <cadgeom/IViewport.h>

#include "core/ObjectTracker.h"
#include "interact/OrbitCamera.h"
#include "interact/OverlayBuilder.h"
#include "interact/Picker.h"
#include "interact/ToolContext.h"
#include "render/Overlay.h"
#include "render/Renderer.h"
#include "render/Surface.h"

#include <memory>

namespace cadgeom::api {

class EngineImpl;

/// A surface, a swapchain, a camera and the input that drives it.
///
/// Owned by the engine even when the host releases it early, which is why
/// Release() goes back through the engine rather than deleting `this`.
///
/// Input is split by button, not by priority: middle and right belong to the
/// camera, left and the keyboard go to the active tool first and fall through to
/// the camera when the tool declines (docs/architecture.md §6.2). Splitting it
/// any other way means a tool that wants mouse-move updates for its rubber band
/// also swallows the orbit drag.
class ViewportImpl final : public IViewport {
public:
    ViewportImpl(EngineImpl& engine, std::unique_ptr<render::Surface> surface);
    ~ViewportImpl() override;

    CgResult Initialize(const ViewportDesc& desc);

    // -- IViewport ----------------------------------------------------------

    void Release() override;

    ICamera* GetCamera() override;
    const ICamera* GetCamera() const override;

    CgResult Resize(uint32_t width, uint32_t height) override;
    void GetSize(uint32_t& width, uint32_t& height) const override;
    bool ShouldClose() const override;

    void SetBackgroundColor(const Color& color) override;
    void SetRenderMode(RenderMode mode) override;
    RenderMode GetRenderMode() const override;
    void SetGridVisible(bool visible) override;
    bool IsGridVisible() const override;

    void SetWorkPlane(const WorkPlane& plane) override;
    void GetWorkPlane(WorkPlane& out) const override;
    CgResult SetWorkPlaneFromPick(const PickResult& pick) override;

    bool OnMouseEvent(const MouseEvent& e) override;
    bool OnKeyEvent(const KeyEvent& e) override;

    void SetPickFilter(uint32_t filter) override;
    uint32_t GetPickFilter() const override;

    bool Pick(double x, double y, uint32_t pickFilter, PickResult& out) const override;

    CgResult Render() override;
    CgResult SaveScreenshot(const char* utf8Path) override;

private:
    void OnSurfaceResized(uint32_t width, uint32_t height);
    bool HandleCameraMouse(const MouseEvent& e, double dx, double dy);
    bool HandleCameraKey(const KeyEvent& e);
    /// Keyboard tool switching. Deliberately here and not in the host: for a
    /// Glfw-backed viewport the engine owns the window, so the host never sees
    /// the key at all.
    bool HandleToolKey(const KeyEvent& e);
    /// @brief Ctrl+Z / Ctrl+Y（以及 Ctrl+Shift+Z）。和工具快捷键在这里的理由
    ///        一样：Glfw 视口的窗口归引擎所有，宿主根本收不到这个按键。
    bool HandleEditKey(const KeyEvent& e);
    render::RenderView BuildRenderView() const;

    /// @brief 把像素容差换成拾取用的世界容差模型。
    interact::PickTolerance PickToleranceFor(double pixels) const;

    /// Makes this viewport's context the current one and returns the active
    /// tool, or null when there is none.
    ITool* ActiveTool();

    /// Spacing the grid shader is currently drawing, which is also what grid
    /// snapping rounds to.
    double GridSpacing() const;

    core::ObjectTracker tracker_;
    EngineImpl& engine_;

    std::unique_ptr<render::Surface> surface_;
    render::Renderer renderer_;
    interact::OrbitCamera camera_;

    /// Declared after the camera it refers to.
    interact::ToolContext toolContext_;
    render::OverlayData overlay_;
    interact::OverlayBuilder overlayBuilder_;

    Color background_{0.16f, 0.17f, 0.19f, 1.0f};
    RenderMode renderMode_{RenderMode::ShadedWithEdges};
    bool showGrid_{true};
    WorkPlane workPlane_{DefaultWorkPlane()};
    uint32_t pickFilter_{PickFilter_All};

    double lastMouseX_{0.0};
    double lastMouseY_{0.0};
    bool dragging_{false};
    MouseButton dragButton_{MouseButton::None};
    uint32_t dragMods_{KeyMod_None};
};

} // namespace cadgeom::api
