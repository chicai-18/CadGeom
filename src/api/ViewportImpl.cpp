#include "api/ViewportImpl.h"

#include <cadgeom/ICommandStack.h>

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

/// 拾取容差，屏幕像素（docs/architecture.md §6.3）。它是「点得准」和「点得中」
/// 之间的那条线：小了细线选不上，大了两条挨着的线分不开。
constexpr double kPickPixels = 6.0;

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
    : engine_(engine),
      surface_(std::move(surface)),
      toolContext_(engine.Scene(), *this, camera_, engine.Tools().Settings(), &engine.Scene()),
      overlayBuilder_(overlay_) {}

ViewportImpl::~ViewportImpl() {
    // The tool manager may still be pointing at this viewport's context, which
    // is about to stop existing.
    if (engine_.Tools().Context() == &toolContext_) {
        engine_.Tools().SetContext(nullptr);
    }

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
    if (pick.kind == PickKind::None || !IsValid(pick.entity)) {
        return core::SetError(CgResult::InvalidArgument,
                              "SetWorkPlaneFromPick: the pick did not hit anything");
    }
    if (LengthSq(pick.normal) < kEpsilon) {
        return core::SetError(CgResult::InvalidArgument,
                              "SetWorkPlaneFromPick: the pick carries a zero-length normal");
    }

    // u 轴优先沿用当前平面的，这样在共面的两个面之间切换时，网格和矩形工具的
    // 朝向不会莫名其妙地转过去。和法线平行时 MakeWorkPlane 自己会另选一个。
    workPlane_ = MakeWorkPlane(pick.point, pick.normal, workPlane_.uAxis);
    CG_DEBUG("work plane set from a %s pick on entity %llu",
             pick.kind == PickKind::Face ? "face" : "curve",
             static_cast<unsigned long long>(pick.entity.value));
    return CgResult::Ok;
}

interact::PickTolerance ViewportImpl::PickToleranceFor(double pixels) const {
    // 正交下一个像素处处一样大，透视下它随深度线性变大 —— 两者写成同一个
    // `base + slope * t`，窄阶段就不必知道自己在哪种投影里。
    Vec3d eye{};
    Vec3d forward{};
    camera_.GetPosition(eye);
    camera_.GetForward(forward);
    const double perUnitDepth = camera_.GetPixelWorldSize(eye + forward);

    interact::PickTolerance tolerance{};
    if (camera_.GetProjection() == ProjectionMode::Perspective) {
        // t 是沿射线的距离，深度是它在视线方向上的投影，两者在视野边缘差一个
        // 余弦。差出来的是稍微宽一点的容差，比漏选好。
        tolerance.slope = pixels * perUnitDepth;
    } else {
        tolerance.base = pixels * perUnitDepth;
    }
    return tolerance;
}

bool ViewportImpl::Pick(double x, double y, uint32_t pickFilter, PickResult& out) const {
    Ray ray{};
    camera_.ScreenToRay(x, y, ray);
    // 光标下没有东西不是错误：鼠标划过空白是最常见的情况，把它记进错误槽会让
    // 宿主每一帧都读到一条假故障。
    return engine_.Scene().RaycastWithTolerance(ray, pickFilter, PickToleranceFor(kPickPixels),
                                                out);
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

ITool* ViewportImpl::ActiveTool() {
    // Set every time rather than once at construction: the manager holds a
    // single context slot, and with two viewports open the right one is
    // whichever is dispatching now.
    engine_.Tools().SetContext(&toolContext_);
    toolContext_.SetGridSpacing(GridSpacing());
    return engine_.Tools().GetActiveToolInterface();
}

bool ViewportImpl::OnMouseEvent(const MouseEvent& e) {
    // Track the cursor here rather than inside the camera handler, so a tool
    // consuming a move does not leave the camera with a stale origin and a
    // one-frame jump the next time a drag starts.
    const double dx = e.x - lastMouseX_;
    const double dy = e.y - lastMouseY_;
    if (e.action == MouseAction::Move) {
        lastMouseX_ = e.x;
        lastMouseY_ = e.y;
    }

    // A drag in progress owns the mouse until it ends, whatever else wants it.
    const bool cameraGesture = dragging_ || e.action == MouseAction::Wheel ||
                               e.button == MouseButton::Middle || e.button == MouseButton::Right;

    if (!cameraGesture) {
        if (ITool* tool = ActiveTool()) {
            ToolResult result = ToolResult::Ignored;
            switch (e.action) {
                case MouseAction::Down:
                case MouseAction::DoubleClick:
                    // Both land on OnMouseDown; the event says which it was, so
                    // a tool that ends on a double-click can tell and the rest
                    // do not have to care.
                    result = tool->OnMouseDown(e, &toolContext_);
                    break;
                case MouseAction::Move:
                    result = tool->OnMouseMove(e, &toolContext_);
                    break;
                case MouseAction::Up:
                    result = tool->OnMouseUp(e, &toolContext_);
                    break;
                default:
                    break;
            }
            if (result == ToolResult::Finished) {
                engine_.Tools().Cancel();
                return true;
            }
            if (result != ToolResult::Ignored) {
                return true;
            }
        }
    }

    return HandleCameraMouse(e, dx, dy);
}

bool ViewportImpl::HandleCameraMouse(const MouseEvent& e, double dx, double dy) {
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
    if (ITool* tool = ActiveTool()) {
        const ToolResult result = tool->OnKey(e, &toolContext_);
        if (result == ToolResult::Finished) {
            engine_.Tools().Cancel();
            return true;
        }
        if (result != ToolResult::Ignored) {
            return true;
        }
    }
    return HandleEditKey(e) || HandleToolKey(e) || HandleCameraKey(e);
}

bool ViewportImpl::HandleEditKey(const KeyEvent& e) {
    if (e.action == KeyAction::Up || (e.mods & KeyMod_Ctrl) == 0) {
        return false;
    }

    ICommandStack* stack = engine_.Scene().GetCommandStack();
    switch (e.key) {
        case 'Z':
            // Ctrl+Shift+Z 也是重做：两派习惯都有人用，而它们不冲突。
            if ((e.mods & KeyMod_Shift) != 0) {
                return CgSucceeded(stack->Redo());
            }
            return CgSucceeded(stack->Undo());
        case 'Y':
            return CgSucceeded(stack->Redo());
        default:
            return false;
    }
}

bool ViewportImpl::HandleToolKey(const KeyEvent& e) {
    // 带 Ctrl 的按键归编辑快捷键，别让 Ctrl+C 切到画圆工具去。
    if (e.action == KeyAction::Up || (e.mods & KeyMod_Ctrl) != 0) {
        return false;
    }

    // Escape 永远能回到 Select，哪怕当前工具刚刚拒绝了它 —— 用户迷路时按的就是
    // 这个键。
    if (e.key == Key_Escape) {
        engine_.Tools().Cancel();
        return true;
    }

    ToolId id = ToolId::None;
    switch (e.key) {
        case 'V': id = ToolId::Select;    break;
        case 'X': id = ToolId::Point;     break;
        case 'L': id = ToolId::Line;      break;
        case 'C': id = ToolId::Circle;    break;
        case 'R': id = ToolId::Rectangle; break;
        case 'Y': id = ToolId::Polyline;  break;
        case 'E': id = ToolId::Extrude;   break;
        case 'M': id = ToolId::Move;      break;
        case 'T': id = ToolId::Rotate;    break;  // R 已经给了矩形，T 取 turn。
        default: return false;
    }

    engine_.Tools().SetContext(&toolContext_);
    return CgSucceeded(engine_.Tools().Activate(id));
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

double ViewportImpl::GridSpacing() const {
    return NiceSpacing(camera_.PixelWorldSizeAtTarget() * kMinPixelsPerGridCell);
}

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
    view.gridSpacing = GridSpacing();
    // What the line and point passes scale their screen-space expansion by.
    view.pixelWorldSize = worldPerPixel;

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
    // 线框模式下不画表面，只留特征边和曲线 —— CAD 的线框图指的就是这个。着色模式
    // 反过来，只有面没有边。默认的 ShadedWithEdges 两样都要，也是 M4 的验收样子：
    // 带轮廓黑边的实体。RenderMode::HiddenLine 暂时按 ShadedWithEdges 处理，它要的
    // 「被遮挡的边画成虚线」得等 M6 的第二遍深度。
    view.drawSurfaces = renderMode_ != RenderMode::Wireframe;
    view.drawEdges = renderMode_ != RenderMode::Shaded;
    return view;
}

CgResult ViewportImpl::Render() {
    // Idempotent, and cheap when nothing changed: a host that renders without
    // ticking still gets a correct frame rather than an empty one.
    const CgResult sync = engine_.SyncRenderState();
    if (CgFailed(sync)) {
        return sync;
    }

    // The preview is rebuilt from scratch every frame from the tool's current
    // state, which is what keeps it out of the scene and out of the undo stack.
    overlay_.Clear();
    if (ITool* tool = ActiveTool()) {
        TessParams tess{};
        engine_.Scene().GetGeometryBuilder()->GetTessParams(tess);
        overlayBuilder_.SetTessParams(tess);
        tool->BuildPreview(&overlayBuilder_, &toolContext_);
    }

    return renderer_.Render(BuildRenderView(), engine_.Snapshot(), overlay_);
}

CgResult ViewportImpl::SaveScreenshot(const char* utf8Path) {
    return renderer_.SaveScreenshot(utf8Path);
}

} // namespace cadgeom::api
