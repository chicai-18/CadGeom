#pragma once

#include <cadgeom/IViewport.h>

#include "core/ObjectTracker.h"
#include "interact/OrbitCamera.h"
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
/// Input handling is camera navigation only at M1. From M2 the active tool gets
/// first refusal on every event and the camera handles what is left over
/// (docs/architecture.md §6.2).
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
    bool HandleCameraMouse(const MouseEvent& e);
    bool HandleCameraKey(const KeyEvent& e);
    render::RenderView BuildRenderView() const;

    core::ObjectTracker tracker_;
    EngineImpl& engine_;

    std::unique_ptr<render::Surface> surface_;
    render::Renderer renderer_;
    interact::OrbitCamera camera_;

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
