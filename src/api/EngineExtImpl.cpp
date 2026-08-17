#include "api/EngineExtImpl.h"

#include "api/EngineImpl.h"
#include "core/Units.h"

#if CADGEOM_HAS_VULKAN
#include "api/ViewportImpl.h"
#endif

namespace cadgeom::api {
namespace {

#if CADGEOM_HAS_VULKAN
/// @brief 把宿主递过来的 `IViewport*` 认回自己的实现类型。
/// @return 这个引擎不认识它时为 null。
///
/// 走一遍视口表而不是直接 static_cast：宿主手里的 IViewport* 只能来自
/// CreateViewport，但一个已经 Release 掉的指针长得一模一样，而查表能把它挡在
/// 门外（在别的引擎上创建的视口同理）。
ViewportImpl* Resolve(EngineImpl& engine, const IViewport* viewport) {
    if (!viewport) {
        return nullptr;
    }
    for (uint32_t i = 0; i < engine.GetViewportCount(); ++i) {
        IViewport* candidate = engine.GetViewportAt(i);
        if (candidate == viewport) {
            return static_cast<ViewportImpl*>(candidate);
        }
    }
    return nullptr;
}
#endif

} // namespace

EngineExtImpl::EngineExtImpl(EngineImpl& engine) : engine_(engine) {}

EngineExtImpl::~EngineExtImpl() = default;

// ---------------------------------------------------------------------------
// 单位系统
// ---------------------------------------------------------------------------

void EngineExtImpl::SetUnitSettings(const UnitSettings& settings) {
    // 单位只影响读数，所以这里不推进 revision，也不碰任何几何 —— 改显示单位不该
    // 让整个场景重传一遍显存。
    engine_.Tools().Settings().units = settings;
}

void EngineExtImpl::GetUnitSettings(UnitSettings& out) const {
    out = engine_.Tools().Settings().units;
}

double EngineExtImpl::ToDisplayLength(double modelUnits) const {
    return core::ToDisplay(engine_.Tools().Settings().units, modelUnits);
}

double EngineExtImpl::ToModelLength(double displayValue) const {
    return core::ToModel(engine_.Tools().Settings().units, displayValue);
}

uint32_t EngineExtImpl::FormatLength(double modelUnits, char* buffer, uint32_t capacity) const {
    return core::FormatLength(engine_.Tools().Settings().units, modelUnits, buffer, capacity);
}

uint32_t EngineExtImpl::FormatAngle(double radians, char* buffer, uint32_t capacity) const {
    return core::FormatAngle(engine_.Tools().Settings().units, radians, buffer, capacity);
}

// ---------------------------------------------------------------------------
// 吸附参考点
// ---------------------------------------------------------------------------

void EngineExtImpl::SetSnapReference(const Vec3d& point) {
    interact::ToolSettings& settings = engine_.Tools().Settings();
    settings.snapReference = point;
    settings.hasSnapReference = true;
}

void EngineExtImpl::ClearSnapReference() {
    engine_.Tools().Settings().hasSnapReference = false;
}

bool EngineExtImpl::GetSnapReference(Vec3d& out) const {
    const interact::ToolSettings& settings = engine_.Tools().Settings();
    if (!settings.hasSnapReference) {
        return false;
    }
    out = settings.snapReference;
    return true;
}

// ---------------------------------------------------------------------------
// 视口附加
// ---------------------------------------------------------------------------

const char* EngineExtImpl::GetStatusText(const IViewport* viewport) const {
#if CADGEOM_HAS_VULKAN
    if (const ViewportImpl* impl = Resolve(engine_, viewport)) {
        return impl->StatusText();
    }
#else
    (void)viewport;
#endif
    // 没指定视口（或那个视口不是我们的）就问工具管理器当前挂着的上下文 —— 那正是
    // 最近一次派发过事件的视口，也就是用户此刻在操作的那个。上下文是视口建的，所以
    // 这个 static_cast 认的是我们自己造出来的对象。
    if (const auto* context =
            static_cast<const interact::ToolContext*>(engine_.Tools().Context())) {
        return context->StatusText();
    }
    return "";
}

void EngineExtImpl::SetHudVisible(IViewport* viewport, bool visible) {
#if CADGEOM_HAS_VULKAN
    if (ViewportImpl* impl = Resolve(engine_, viewport)) {
        impl->SetHudVisible(visible);
        return;
    }
    if (!viewport) {
        // null 就是「所有视口」：多视口下逐个去设是宿主的事，但「把 HUD 关掉」
        // 通常说的是整个应用。
        for (uint32_t i = 0; i < engine_.GetViewportCount(); ++i) {
            static_cast<ViewportImpl*>(engine_.GetViewportAt(i))->SetHudVisible(visible);
        }
    }
#else
    (void)viewport;
    (void)visible;
#endif
}

bool EngineExtImpl::IsHudVisible(const IViewport* viewport) const {
#if CADGEOM_HAS_VULKAN
    if (const ViewportImpl* impl = Resolve(engine_, viewport)) {
        return impl->IsHudVisible();
    }
    if (!viewport && engine_.GetViewportCount() > 0) {
        return static_cast<const ViewportImpl*>(engine_.GetViewportAt(0))->IsHudVisible();
    }
#else
    (void)viewport;
#endif
    return false;
}

uint32_t EngineExtImpl::GetSampleCount(const IViewport* viewport) const {
#if CADGEOM_HAS_VULKAN
    if (const ViewportImpl* impl = Resolve(engine_, viewport)) {
        return impl->SampleCount();
    }
#else
    (void)viewport;
#endif
    return 0;
}

// ---------------------------------------------------------------------------
// 测量
// ---------------------------------------------------------------------------

bool EngineExtImpl::GetMeasurement(Vec3d& from, Vec3d& to, double& distance) const {
    const interact::ToolSettings& settings = engine_.Tools().Settings();
    if (!settings.hasMeasurement) {
        return false;
    }
    from = settings.measureFrom;
    to = settings.measureTo;
    distance = settings.measureDistance;
    return true;
}

} // namespace cadgeom::api
