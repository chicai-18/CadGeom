#pragma once

#include <cadgeom/ITool.h>

#include "core/ObjectTracker.h"

#include <unordered_map>

namespace cadgeom::api {

class EngineImpl;

/// Registration, ownership and the active-tool slot are real as of M0; the
/// built-in tools and the event dispatch that feeds them need a viewport to
/// supply the IToolContext, so they arrive with M1/M2
/// (docs/architecture.md §6.2).
///
/// A host can register its own ITool today and it will be owned, activated and
/// released correctly.
class ToolManagerImpl final : public IToolManager {
public:
    explicit ToolManagerImpl(EngineImpl& engine);
    ~ToolManagerImpl() override;

    CgResult Activate(ToolId id) override;
    ToolId GetActiveTool() const override;
    ITool* GetActiveToolInterface() override;
    void Cancel() override;

    CgResult RegisterTool(ITool* tool) override;
    CgResult UnregisterTool(ToolId id) override;

    void SetSnapMask(uint32_t snapMask) override;
    uint32_t GetSnapMask() const override;
    void SetSnapTolerance(double pixels) override;

    void SetContinuousMode(bool enabled) override;
    bool IsContinuousMode() const override;

    double GetSnapTolerance() const { return snapTolerance_; }

    /// Installed by a viewport once one exists; until then Activate() records
    /// the choice without calling into the tool.
    void SetContext(IToolContext* context);

private:
    ITool* Find(ToolId id) const;

    core::ObjectTracker tracker_;
    EngineImpl& engine_;

    std::unordered_map<int32_t, ITool*> tools_;
    ToolId activeId_{ToolId::None};
    ITool* active_{nullptr};
    IToolContext* context_{nullptr};

    uint32_t snapMask_{Snap_Endpoint | Snap_Midpoint | Snap_Center | Snap_Intersection};
    double snapTolerance_{8.0};
    bool continuous_{false};
};

} // namespace cadgeom::api
