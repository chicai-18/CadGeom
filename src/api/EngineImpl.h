#pragma once

#include <cadgeom/IEngine.h>

#include "api/IoRegistryImpl.h"
#include "api/SceneImpl.h"
#include "api/ToolManagerImpl.h"
#include "core/ObjectTracker.h"

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
    bool ValidationEnabled() const { return validation_; }
    uint64_t FrameIndex() const { return frameIndex_; }

private:
    core::ObjectTracker tracker_;

    std::string appName_;
    KernelType kernelType_;
    bool validation_;

    SceneImpl scene_;
    ToolManagerImpl tools_;
    IoRegistryImpl io_;

    std::vector<IViewport*> viewports_;

    uint64_t frameIndex_{0};
    double elapsedSeconds_{0.0};
};

} // namespace cadgeom::api
