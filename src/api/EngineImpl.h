#pragma once

#include <cadgeom/IEngine.h>

#include "api/IoRegistryImpl.h"
#include "api/SceneImpl.h"
#include "api/SceneIo.h"
#include "api/ToolManagerImpl.h"
#include "core/ObjectTracker.h"

#if CADGEOM_HAS_VULKAN
#include "render/GpuScene.h"
#include "render/RenderSystem.h"
#endif

#include <string>
#include <vector>

namespace cadgeom::api {

class EngineImpl final : public ICadEngine {
public:
    /// `desc` has already been validated by the factory.
    explicit EngineImpl(const EngineDesc& desc);
    ~EngineImpl() override;

    void Release() override;

    IScene* GetScene() override;
    const IScene* GetScene() const override;

    IViewport* CreateViewport(const ViewportDesc& desc) override;
    uint32_t GetViewportCount() const override;
    IViewport* GetViewportAt(uint32_t index) override;

    IToolManager* GetToolManager() override;
    IIoRegistry* GetIoRegistry() override;

    void Tick(double deltaSeconds) override;

    CgResult GetLastError() const override;
    const char* GetLastErrorMessage() const override;
    void ClearLastError() override;

    void SetLogLevel(LogLevel level) override;
    LogLevel GetLogLevel() const override;

    const char* GetDeviceName() const override;

    void* GetExtension(uint32_t interfaceId) override;

    // -- Engine-side access -------------------------------------------------

    SceneImpl& Scene() { return scene_; }
    ToolManagerImpl& Tools() { return tools_; }
    bool ValidationEnabled() const { return validation_; }
    uint64_t FrameIndex() const { return frameIndex_; }

    /// Destroys one viewport. IViewport::Release() routes here because the
    /// engine owns viewports even after the host has let go of them.
    void ReleaseViewport(IViewport* viewport);

#if CADGEOM_HAS_VULKAN
    render::RenderSystem& Renderer() { return renderSystem_; }

    /// Brings the GPU's copy of the scene up to date. Idempotent and cheap
    /// when nothing has changed, so a viewport can call it before every frame
    /// and a host that forgets to Tick() still gets a correct picture.
    CgResult SyncRenderState();

    const render::SceneSnapshot& Snapshot() const { return snapshot_; }
#endif

private:
#if CADGEOM_HAS_VULKAN
    void UpdateSnapshot();
#endif

    core::ObjectTracker tracker_;

    std::string appName_;
    KernelType kernelType_;
    bool validation_;

    SceneImpl scene_;
    ToolManagerImpl tools_;
    /// 声明在 io_ 之前：注册表销毁时会 Release 掉内置处理器，而那些处理器抓着这座
    /// 桥的引用。反过来排的话，最后一批处理器会向一个已经没了的对象告别。
    SceneIoBridge ioBridge_;
    IoRegistryImpl io_;

    std::vector<IViewport*> viewports_;

    uint64_t frameIndex_{0};
    double elapsedSeconds_{0.0};

#if CADGEOM_HAS_VULKAN
    /// Created on the first CreateViewport(): a host that only edits geometry
    /// never initialises a GPU.
    render::RenderSystem renderSystem_;
    render::SceneSnapshot snapshot_;
    uint64_t snapshotRevision_{0};
    bool snapshotValid_{false};
#endif
};

} // namespace cadgeom::api
