#pragma once

#include <cadgeom/ITool.h>

#include "core/ObjectTracker.h"
#include "interact/ToolContext.h"

#include <unordered_map>

namespace cadgeom::api {

class EngineImpl;

/// Owns the built-in tools, whatever the host has registered, and the one
/// active-tool slot (docs/architecture.md §6.2).
///
/// The context comes from whichever viewport is currently dispatching, because
/// half of what a tool asks for is a property of the view it is being driven
/// from. Everything that is *not* per-view — continuous mode, the snap mask —
/// lives in `settings_` and is read by the tools directly.
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

    /// The engine-global half of a tool's state. Lives here because it is set
    /// through IToolManager and applies to every viewport; the built-in tools
    /// hold a reference to it for the lifetime of the manager.
    ///
    /// 可写的那一份是给工具和 ICadEngine2 用的：垂足吸附的参考点和显示单位都住在
    /// 这里，而它们由「落下了一个点」的工具和宿主分别写入（§6.3、M6 单位系统）。
    interact::ToolSettings& Settings() { return settings_; }
    const interact::ToolSettings& Settings() const { return settings_; }

    /// Installed by whichever viewport is about to dispatch. Switching contexts
    /// re-activates the current tool, so a tool always knows which view it is
    /// being driven from.
    void SetContext(IToolContext* context);
    IToolContext* Context() { return context_; }

private:
    ITool* Find(ToolId id) const;

    core::ObjectTracker tracker_;
    EngineImpl& engine_;

    std::unordered_map<int32_t, ITool*> tools_;
    ToolId activeId_{ToolId::None};
    ITool* active_{nullptr};
    IToolContext* context_{nullptr};

    interact::ToolSettings settings_{};
};

} // namespace cadgeom::api
