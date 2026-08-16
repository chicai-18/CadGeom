#include "api/EngineImpl.h"

#include <cadgeom/IViewport.h>

#include "core/Error.h"
#include "core/Log.h"

#include <atomic>

namespace cadgeom::api {
namespace {

/// The log sink is module-global, so the last engine out turns off the lights.
/// Several engines in one process share one sink — the newest one installed
/// wins. Per-engine routing would mean threading an engine pointer through
/// every internal log call, which is not worth it for a case that only arises
/// in multi-document hosts.
std::atomic<uint32_t> g_liveEngines{0};

} // namespace

EngineImpl::EngineImpl(const EngineDesc& desc)
    : appName_(desc.applicationName ? desc.applicationName : "CadGeom host"),
      kernelType_(desc.kernelType),
      validation_(desc.enableValidation),
      scene_(),
      tools_(*this),
      io_(*this) {
    g_liveEngines.fetch_add(1, std::memory_order_relaxed);
    CG_INFO("engine created for '%s' (kernel=%s, validation=%s)", appName_.c_str(),
            kernelType_ == KernelType::Simple ? "Simple" : "Occt",
            validation_ ? "on" : "off");
}

EngineImpl::~EngineImpl() {
    // Viewports the host never released. Documented behaviour: the engine owns
    // them in the end, so shutting down without tidying up is not a leak.
    for (IViewport* vp : viewports_) {
        vp->Release();
    }
    viewports_.clear();

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
    (void)desc;
    // A viewport is a Vulkan swapchain plus a renderer; both arrive in M1.
    // Failing loudly here beats handing back a half-alive object that renders
    // nothing and cannot say why.
    core::SetError(CgResult::NotImplemented,
                   "CreateViewport needs the Vulkan renderer, which lands in milestone M1 "
                   "(install the Vulkan SDK >= 1.3.275 before starting it)");
    return nullptr;
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

    // M1 adds transform flush + re-tessellation + GpuScene::Sync here. The
    // ordering is the contract: everything dirty is resolved before any
    // viewport renders.
}

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
    return "<no Vulkan device: renderer lands in M1>";
}

void* EngineImpl::GetExtension(uint32_t interfaceId) {
    (void)interfaceId;
    return nullptr;
}

} // namespace cadgeom::api
