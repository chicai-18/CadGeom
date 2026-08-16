#include "api/EngineImpl.h"

#include <cadgeom/IViewport.h>

#include <cadgeom/ISelection.h>

#include "core/Error.h"
#include "core/Log.h"
#include "interact/tools/DrawTools.h"
#include "interact/tools/ExtrudeTool.h"
#include "interact/tools/TransformTools.h"
#include "io/Registry.h"

#if CADGEOM_HAS_VULKAN
#include "api/ViewportImpl.h"
#include "render/Surface.h"
#endif

#include <algorithm>
#include <atomic>
#include <memory>

namespace cadgeom::api {
namespace {

/// The log sink is module-global, so the last engine out turns off the lights.
/// Several engines in one process share one sink — the newest one installed
/// wins. Per-engine routing would mean threading an engine pointer through
/// every internal log call, which is not worth it for a case that only arises
/// in multi-document hosts.
std::atomic<uint32_t> g_liveEngines{0};

#if CADGEOM_HAS_VULKAN
/// 选中与预高亮的颜色。线性光（渲染器在线性空间合成），所以这些数看着比屏幕上
/// 暗；琥珀色与工具预览是同一族，青色明显不同，两者一眼分得开。
constexpr Color kSelectedColor{1.00f, 0.42f, 0.05f, 1.0f};
constexpr Color kHoveredColor{0.30f, 0.72f, 1.00f, 1.0f};
/// 高亮同时加粗，颜色之外再给一个不依赖辨色能力的信号。
constexpr float kHighlightWidthScale = 1.6f;
#endif

} // namespace

EngineImpl::EngineImpl(const EngineDesc& desc)
    : appName_(desc.applicationName ? desc.applicationName : "CadGeom host"),
      kernelType_(desc.kernelType),
      validation_(desc.enableValidation),
      scene_(),
      tools_(*this),
      ioBridge_(scene_),
      io_(*this) {
    g_liveEngines.fetch_add(1, std::memory_order_relaxed);

    // The built-ins are engine-owned like any other registered tool, so they go
    // through the same RegisterTool path a host would use — and can be replaced
    // by a host that registers its own under the same id.
    interact::RegisterBuiltinTools(tools_, tools_.Settings());
    interact::RegisterExtrudeTool(tools_, tools_.Settings());
    interact::RegisterTransformTools(tools_, tools_.Settings());
    tools_.Activate(ToolId::Select);

    // 内置的 OBJ / glTF 处理器走的是宿主注册自己格式时走的同一条路，一点特权都没有
    // （docs/architecture.md §7）。注册不上不该拖垮整个引擎：几何和场景照常可用，
    // 只是读不了文件。
    if (CgFailed(io::RegisterBuiltinFormats(io_, ioBridge_, ioBridge_))) {
        CG_WARN("built-in file formats are unavailable: %s", core::LastErrorMessage());
        core::ClearError();
    }

    CG_INFO("engine created for '%s' (kernel=%s, validation=%s)", appName_.c_str(),
            kernelType_ == KernelType::Simple ? "Simple" : "Occt",
            validation_ ? "on" : "off");
}

EngineImpl::~EngineImpl() {
    // Viewports the host never released. Documented behaviour: the engine owns
    // them in the end, so shutting down without tidying up is not a leak.
    // Swapped out first because destroying one otherwise mutates the vector
    // being walked.
    std::vector<IViewport*> owned;
    owned.swap(viewports_);
#if CADGEOM_HAS_VULKAN
    for (IViewport* viewport : owned) {
        delete static_cast<ViewportImpl*>(viewport);
    }
    // renderSystem_ is a member, so it goes down after this body has run —
    // which is the right order: every viewport is already gone.
#endif

    CG_INFO("engine destroyed after %llu frames", static_cast<unsigned long long>(frameIndex_));

    // Last act: stop calling into the host. Anything logged after this point
    // would be reaching into state the host may already have torn down.
    if (g_liveEngines.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        core::Logger::SetSink(nullptr, nullptr);
    }
}

void EngineImpl::Release() {
    delete this;
}

IScene* EngineImpl::GetScene() {
    return &scene_;
}

const IScene* EngineImpl::GetScene() const {
    return &scene_;
}

IViewport* EngineImpl::CreateViewport(const ViewportDesc& desc) {
#if CADGEOM_HAS_VULKAN
    std::unique_ptr<render::Surface> surface = render::CreateSurface(desc.surface);
    if (!surface) {
        return nullptr;  // CreateSurface has already recorded why.
    }
    const bool needsPresentation = surface->PresentsToScreen();

    if (!renderSystem_.IsValid()) {
        // Presentation support is asked for even when this first viewport is
        // headless, so a later windowed one is not left without a swapchain.
        // A machine that cannot present at all still gets a headless device.
        CgResult r = renderSystem_.Initialize(appName_.c_str(), validation_, true);
        if (CgFailed(r) && !needsPresentation) {
            CG_WARN("no presentation-capable device; falling back to headless rendering");
            r = renderSystem_.Initialize(appName_.c_str(), validation_, false);
        }
        if (CgFailed(r)) {
            return nullptr;
        }
    }

    auto viewport = std::make_unique<ViewportImpl>(*this, std::move(surface));
    if (CgFailed(viewport->Initialize(desc))) {
        return nullptr;
    }

    viewports_.push_back(viewport.get());
    CG_INFO("viewport created on %s", renderSystem_.Context().DeviceName());
    return viewport.release();
#else
    (void)desc;
    core::SetError(CgResult::NotImplemented,
                   "this build has no renderer: it was configured without the Vulkan SDK. "
                   "Install LunarG >= 1.3.275, set VULKAN_SDK and reconfigure.");
    return nullptr;
#endif
}

void EngineImpl::ReleaseViewport(IViewport* viewport) {
    const auto it = std::find(viewports_.begin(), viewports_.end(), viewport);
    if (it == viewports_.end()) {
        CG_WARN("IViewport::Release() on a viewport this engine does not own");
        return;
    }
    viewports_.erase(it);

#if CADGEOM_HAS_VULKAN
    delete static_cast<ViewportImpl*>(viewport);
    // The device stays up even with no viewports left: a host that closes one
    // window and opens another should not pay for a full device rebuild, and
    // the memory involved is trivial next to the geometry it holds.
#endif
}

uint32_t EngineImpl::GetViewportCount() const {
    return static_cast<uint32_t>(viewports_.size());
}

IViewport* EngineImpl::GetViewportAt(uint32_t index) {
    return index < viewports_.size() ? viewports_[index] : nullptr;
}

IToolManager* EngineImpl::GetToolManager() {
    return &tools_;
}

IIoRegistry* EngineImpl::GetIoRegistry() {
    return &io_;
}

void EngineImpl::Tick(double deltaSeconds) {
    // Guard against a host feeding a garbage delta after a breakpoint or a
    // stalled frame; clamping here keeps every downstream integrator sane.
    if (!(deltaSeconds >= 0.0)) {  // Also catches NaN.
        deltaSeconds = 0.0;
    } else if (deltaSeconds > 1.0) {
        deltaSeconds = 1.0;
    }

    elapsedSeconds_ += deltaSeconds;
    ++frameIndex_;

#if CADGEOM_HAS_VULKAN
    // Windows the engine owns need their queue drained; windows the host owns
    // are its own business and this is a no-op for them.
    render::PumpWindowEvents();

    if (renderSystem_.IsValid()) {
        // The ordering is the contract: retire what the GPU has finished with,
        // then resolve everything dirty, and only then let viewports render.
        renderSystem_.BeginFrame(frameIndex_);
        SyncRenderState();
    }
#endif
}

#if CADGEOM_HAS_VULKAN

void EngineImpl::UpdateSnapshot() {
    const uint64_t revision = scene_.GetRevision();
    if (snapshotValid_ && revision == snapshotRevision_) {
        return;
    }

    snapshot_.Clear();
    geom::IGeometryKernel& kernel = scene_.Kernel();
    const ISelection& selection = *scene_.GetSelection();
    const EntityId hovered = selection.GetHovered();

    const uint32_t count = scene_.GetEntityCount();
    for (uint32_t i = 0; i < count; ++i) {
        const EntityId id = scene_.GetEntityAt(i);
        const EntityImpl* entity = scene_.FindEntity(id);
        if (!entity || !IsValid(entity->GetShape()) || !scene_.IsEffectivelyVisible(id)) {
            continue;
        }

        // Resolve tessellates on demand. The pointers it hands back stay valid
        // until the shape's parameters move, and that bumps the revision, which
        // is what brings us back here.
        const geom::Shape* shape = kernel.Resolve(entity->GetShape());
        if (!shape) {
            continue;
        }

        Mat4d world{};
        entity->GetWorldTransform(world);
        EntityStyle style{};
        entity->GetStyle(style);

        // 选中和预高亮就是把实体的颜色换掉、把线加粗，不是另开一个 pass：高亮
        // 要和几何一样被深度测试挡住，而选择集一变就会推进 revision，快照因此
        // 自然而然地重建（docs/architecture.md §9，M3「Selection 高亮」）。
        const bool isSelected = selection.Contains(id);
        const bool isHovered = IsValid(hovered) && id == hovered;
        if (isSelected || isHovered) {
            style.color = isSelected ? kSelectedColor : kHoveredColor;
            // 实体的高亮主要靠边看出来：面被光照一染，颜色变化远没有轮廓线明显。
            style.edgeColor = style.color;
            style.lineWidth *= kHighlightWidthScale;
            style.pointSize *= kHighlightWidthScale;
        }

        if (!shape->mesh.IsEmpty()) {
            render::DrawItem item{};
            item.meshIndex = static_cast<uint32_t>(snapshot_.meshes.size());
            item.worldTransform = world;
            item.color = style.color;
            snapshot_.meshes.push_back(&shape->mesh);
            snapshot_.items.push_back(item);
        }

        if (shape->wire.positions.empty()) {
            continue;
        }
        const auto curveIndex = static_cast<uint32_t>(snapshot_.curves.size());
        snapshot_.curves.push_back(&shape->wire);

        // 实体的线框是它的特征边，走 EdgePass：贴在自己的表面上、用 edgeColor、
        // 而且在 RenderMode::Shaded 下整批不画。曲线的线框就是曲线本身，走
        // LinePass（docs/architecture.md §4.2）。
        const bool isSolid = !shape->topology.IsEmpty();
        if (shape->wire.SegmentCount() > 0) {
            render::CurveItem item{};
            item.curveIndex = curveIndex;
            item.worldTransform = world;
            item.color = isSolid ? style.edgeColor : style.color;
            item.width = style.lineWidth;
            item.style = isSolid ? LineStyle::Solid : style.lineStyle;
            (isSolid ? snapshot_.edgeItems : snapshot_.curveItems).push_back(item);
        }
        // 点图元一直要画标记；被选中的曲线额外把顶点显示出来，用户因此看得见
        // 自己能吸附到哪儿 —— 这也是 M3 的顶点拾取在屏幕上的样子。实体不在此列：
        // 它的线框上是一圈细分点，标出来只是一片噪声。
        if (entity->GetShapeType() == ShapeType::Point || (isSelected && !isSolid)) {
            render::PointItem item{};
            item.curveIndex = curveIndex;
            item.worldTransform = world;
            item.color = style.color;
            item.size = style.pointSize;
            snapshot_.pointItems.push_back(item);
        }
    }

    snapshot_.revision = revision;
    // 高亮变化会推进 revision 但不推进这一个，GpuScene 因此不会为了换个颜色重传
    // 整个场景的顶点。
    snapshot_.geometryRevision = scene_.GetGeometryRevision();
    snapshotRevision_ = revision;
    snapshotValid_ = true;
}

CgResult EngineImpl::SyncRenderState() {
    if (!renderSystem_.IsValid()) {
        return CgResult::Ok;
    }
    UpdateSnapshot();
    return renderSystem_.Geometry().Sync(renderSystem_.Context(), renderSystem_.Deletions(),
                                         frameIndex_, snapshot_);
}

#endif // CADGEOM_HAS_VULKAN

CgResult EngineImpl::GetLastError() const {
    return core::LastError();
}

const char* EngineImpl::GetLastErrorMessage() const {
    return core::LastErrorMessage();
}

void EngineImpl::ClearLastError() {
    core::ClearError();
}

void EngineImpl::SetLogLevel(LogLevel level) {
    core::Logger::SetLevel(level);
}

LogLevel EngineImpl::GetLogLevel() const {
    return core::Logger::GetLevel();
}

const char* EngineImpl::GetDeviceName() const {
#if CADGEOM_HAS_VULKAN
    if (renderSystem_.IsValid()) {
        return renderSystem_.Context().DeviceName();
    }
    return "<no device yet: created with the first viewport>";
#else
    return "<no Vulkan device: this build has no renderer>";
#endif
}

void* EngineImpl::GetExtension(uint32_t interfaceId) {
    (void)interfaceId;
    return nullptr;
}

} // namespace cadgeom::api
