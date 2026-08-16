#include "api/ViewportImpl.h"

#include "api/EngineImpl.h"
#include "core/Error.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>

namespace cadgeom::api {
namespace {

/// Radians per pixel of drag. A quarter of a degree is the figure every CAD
/// package converges on: a full turn takes about a screen width.
constexpr double kOrbitRadiansPerPixel = 0.25 * kDegToRad;

/// How many pixels the smallest grid cell should cover before the grid steps up
/// to the next decade. Below this the lines merge into a haze.
constexpr double kMinPixelsPerGridCell = 18.0;

/// Snaps to a 1-2-5 sequence: the spacings a person reads off a drawing without
/// having to think about it.
double NiceSpacing(double desired) {
    if (!(desired > 0.0)) {
        return 1.0;
    }
    const double magnitude = std::pow(10.0, std::floor(std::log10(desired)));
    const double normalized = desired / magnitude;
    const double step = normalized <= 1.0 ? 1.0 : normalized <= 2.0 ? 2.0
                        : normalized <= 5.0 ? 5.0
                                            : 10.0;
    return step * magnitude;
}

} // namespace

ViewportImpl::ViewportImpl(EngineImpl& engine, std::unique_ptr<render::Surface> surface)
    : engine_(engine), surface_(std::move(surface)) {}

ViewportImpl::~ViewportImpl() {
    // Order matters: the renderer holds a VkSurfaceKHR made from the window, so
    // it has to be gone before the window is.
    renderer_.Shutdown();
    surface_.reset();
}

CgResult ViewportImpl::Initialize(const ViewportDesc& desc) {
    background_ = desc.background;
    showGrid_ = desc.showGrid;

    if (desc.sampleCount > 1) {
        // Not silently ignored: MSAA is an offscreen-target change, and a host
        // that asked for it should know it did not get it.
        CG_WARN("ViewportDesc::sampleCount %u ignored; multisampling arrives with M6",
                desc.sampleCount);
    }

    CgResult r = renderer_.Initialize(engine_.Renderer(), *surface_, desc);
    if (CgFailed(r)) {
        return r;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    renderer_.GetExtent(width, height);
    camera_.SetViewportSize(width, height);
    camera_.SetScene(engine_.GetScene());
    camera_.SetProjection(desc.projection);
    camera_.SetStandardView(StandardView::Isometric);
    camera_.ZoomToFit(nullptr, 1.4);

    render::SurfaceCallbacks callbacks;
    callbacks.onResize = [this](uint32_t w, uint32_t h) { OnSurfaceResized(w, h); };
    callbacks.onMouse = [this](const MouseEvent& e) { OnMouseEvent(e); };
    callbacks.onKey = [this](const KeyEvent& e) { OnKeyEvent(e); };
    surface_->SetCallbacks(std::move(callbacks));

    return CgResult::Ok;
}

void ViewportImpl::Release() {
    // The engine owns every viewport, so it is the one that gets to delete one.
    engine_.ReleaseViewport(this);
}

ICamera* ViewportImpl::GetCamera() {
    return &camera_;
}

const ICamera* ViewportImpl::GetCamera() const {
    return &camera_;
}

void ViewportImpl::OnSurfaceResized(uint32_t width, uint32_t height) {
    renderer_.RequestRecreate();
    if (width > 0 && height > 0) {
        camera_.SetViewportSize(width, height);
    }
}

CgResult ViewportImpl::Resize(uint32_t width, uint32_t height) {
    // A zero dimension is a minimised window, which is a state to sit in rather
    // than an error to report.
    surface_->SetExtent(width, height);
    OnSurfaceResized(width, height);
    return CgResult::Ok;
}

void ViewportImpl::GetSize(uint32_t& width, uint32_t& height) const {
    renderer_.GetExtent(width, height);
    if (width == 0 || height == 0) {
        surface_->GetExtent(width, height);
    }
}

bool ViewportImpl::ShouldClose() const {
    return surface_->ShouldClose();
}

void ViewportImpl::SetBackgroundColor(const Color& color) {
    background_ = color;
}

void ViewportImpl::SetRenderMode(RenderMode mode) {
    renderMode_ = mode;
}

RenderMode ViewportImpl::GetRenderMode() const {
    return renderMode_;
}

void ViewportImpl::SetGridVisible(bool visible) {
    showGrid_ = visible;
}

bool ViewportImpl::IsGridVisible() const {
    return showGrid_;
}

void ViewportImpl::SetWorkPlane(const WorkPlane& plane) {
    // Re-orthonormalised rather than trusted: a host computing a plane from a
    // face normal will hand over axes that are a little off, and every 2D tool
    // downstream assumes they are exact.
    workPlane_ = MakeWorkPlane(plane.origin, plane.normal, plane.uAxis);
}

void ViewportImpl::GetWorkPlane(WorkPlane& out) const {
    out = workPlane_;
}

CgResult ViewportImpl::SetWorkPlaneFromPick(const PickResult& pick) {
    (void)pick;
    return core::SetError(CgResult::NotImplemented,
                          "SetWorkPlaneFromPick needs face picking, which lands in milestone M3");
}

bool ViewportImpl::Pick(double x, double y, uint32_t pickFilter, PickResult& out) const {
    (void)x;
    (void)y;
    (void)pickFilter;
    (void)out;
    core::SetError(CgResult::NotImplemented,
                   "Pick needs the BVH and the picker, which land in milestone M3");
    return false;
}

void ViewportImpl::SetPickFilter(uint32_t filter) {
    pickFilter_ = filter;
}

uint32_t ViewportImpl::GetPickFilter() const {
    return pickFilter_;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool ViewportImpl::OnMouseEvent(const MouseEvent& e) {
    // From M2 the active tool sees the event first and the camera only gets
    // what the tool declines.
    return HandleCameraMouse(e);
}

bool ViewportImpl::HandleCameraMouse(const MouseEvent& e) {
    switch (e.action) {
        case MouseAction::Down:
            // Middle drag orbits, shift+middle pans, right drag pans — the
            // bindings the rest of the CAD world uses (§6.4).
            if (e.button == MouseButton::Middle || e.button == MouseButton::Right) {
                dragging_ = true;
                dragButton_ = e.button;
                dragMods_ = e.mods;
                lastMouseX_ = e.x;
                lastMouseY_ = e.y;
                return true;
            }
            return false;

        case MouseAction::Up:
            if (dragging_ && e.button == dragButton_) {
                dragging_ = false;
                dragButton_ = MouseButton::None;
                return true;
            }
            return false;

        case MouseAction::Move: {
            const double dx = e.x - lastMouseX_;
            const double dy = e.y - lastMouseY_;
            lastMouseX_ = e.x;
            lastMouseY_ = e.y;
            if (!dragging_) {
                return false;
            }
            const bool pan =
                dragButton_ == MouseButton::Right || (dragMods_ & KeyMod_Shift) != 0;
            if (pan) {
                camera_.Pan(dx, dy);
            } else {
                // Dragging right turns the model right, which means orbiting
                // the camera the other way.
                camera_.Orbit(-dx * kOrbitRadiansPerPixel, dy * kOrbitRadiansPerPixel);
            }
            return true;
        }

        case MouseAction::Wheel:
            camera_.Zoom(e.wheelDelta, e.x, e.y);
            return true;

        case MouseAction::DoubleClick:
            if (e.button == MouseButton::Middle) {
                camera_.ZoomToFit(nullptr, 1.2);
                return true;
            }
            return false;

        default:
            return false;
    }
}

bool ViewportImpl::OnKeyEvent(const KeyEvent& e) {
    return HandleCameraKey(e);
}

bool ViewportImpl::HandleCameraKey(const KeyEvent& e) {
    if (e.action == KeyAction::Up) {
        return false;
    }

    switch (e.key) {
        case '1': camera_.SetStandardView(StandardView::Front);     return true;
        case '2': camera_.SetStandardView(StandardView::Back);      return true;
        case '3': camera_.SetStandardView(StandardView::Right);     return true;
        case '4': camera_.SetStandardView(StandardView::Left);      return true;
        case '5': camera_.SetStandardView(StandardView::Top);       return true;
        case '6': camera_.SetStandardView(StandardView::Bottom);    return true;
        case '7': camera_.SetStandardView(StandardView::Isometric); return true;
        case 'F':
            camera_.ZoomToFit(nullptr, 1.2);
            return true;
        case 'P':
            camera_.SetProjection(camera_.GetProjection() == ProjectionMode::Orthographic
                                      ? ProjectionMode::Perspective
                                      : ProjectionMode::Orthographic);
            return true;
        case 'G':
            showGrid_ = !showGrid_;
            return true;
        case 'W':
            renderMode_ = renderMode_ == RenderMode::Wireframe ? RenderMode::ShadedWithEdges
                                                               : RenderMode::Wireframe;
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

render::RenderView ViewportImpl::BuildRenderView() const {
    render::RenderView view{};
    view.viewProj = camera_.ViewProjectionRelative();
    view.cameraOrigin = camera_.Position();
    view.forward = camera_.Forward();

    Vec3d target{};
    camera_.GetTarget(target);
    view.focusRel = target - view.cameraOrigin;
    view.perspective = camera_.GetProjection() == ProjectionMode::Perspective;

    // A key light over the viewer's shoulder, so a part is never lit edge-on
    // however it is orbited. Following the camera is the right call for a
    // modelling view: the shape reads, and shadow direction carries no meaning.
    view.lightDir = Normalized(camera_.Forward() * -1.0 + camera_.Up() * 0.55 +
                               camera_.Right() * 0.35);

    const double worldPerPixel = camera_.PixelWorldSizeAtTarget();
    view.gridSpacing = NiceSpacing(worldPerPixel * kMinPixelsPerGridCell);

    // Fade relative to how much world the viewport covers, so the grid always
    // fills a similar fraction of the screen no matter the zoom.
    uint32_t width = 0;
    uint32_t height = 0;
    GetSize(width, height);
    const double viewHeight = worldPerPixel * static_cast<double>(std::max(height, 1u));
    view.gridFadeStart = viewHeight * 1.2;
    view.gridFadeEnd = viewHeight * 3.5;

    view.background = background_;
    view.showGrid = showGrid_;
    view.wireframe = renderMode_ == RenderMode::Wireframe;
    return view;
}

CgResult ViewportImpl::Render() {
    // Idempotent, and cheap when nothing changed: a host that renders without
    // ticking still gets a correct frame rather than an empty one.
    const CgResult sync = engine_.SyncRenderState();
    if (CgFailed(sync)) {
        return sync;
    }
    return renderer_.Render(BuildRenderView(), engine_.Snapshot());
}

CgResult ViewportImpl::SaveScreenshot(const char* utf8Path) {
    return renderer_.SaveScreenshot(utf8Path);
}

} // namespace cadgeom::api
