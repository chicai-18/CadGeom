#include "api/ToolManagerImpl.h"

#include "core/Error.h"
#include "core/Log.h"

namespace cadgeom::api {
namespace {

/// @brief 还没做出来的内置工具，以及它属于哪个里程碑。
struct PendingTool {
    const char* name;
    const char* milestone;
};

/// @return null 表示这个 id 压根不是内置的 —— 对宿主的 id 来说就是「你没注册过」。
const PendingTool* PendingBuiltin(ToolId id) {
    switch (id) {
        case ToolId::Extrude: {
            static const PendingTool pending{"Extrude",
                                             "M4, with profile triangulation and solid topology"};
            return &pending;
        }
        case ToolId::Scale: {
            static const PendingTool pending{"Scale", "M6, with the rest of the polish"};
            return &pending;
        }
        case ToolId::Measure: {
            static const PendingTool pending{"Measure", "M6, with the on-screen readout"};
            return &pending;
        }
        default:
            return nullptr;
    }
}

} // namespace

ToolManagerImpl::ToolManagerImpl(EngineImpl& engine) : engine_(engine) {}

ToolManagerImpl::~ToolManagerImpl() {
    if (active_ && context_) {
        active_->OnDeactivate(context_);
    }
    for (auto& [id, tool] : tools_) {
        tool->Release();
    }
    tools_.clear();
}

ITool* ToolManagerImpl::Find(ToolId id) const {
    const auto it = tools_.find(static_cast<int32_t>(id));
    return it != tools_.end() ? it->second : nullptr;
}

CgResult ToolManagerImpl::Activate(ToolId id) {
    if (id == ToolId::None) {
        if (active_ && context_) {
            active_->OnDeactivate(context_);
        }
        active_ = nullptr;
        activeId_ = ToolId::None;
        return CgResult::Ok;
    }

    ITool* tool = Find(id);
    if (!tool) {
        if (const PendingTool* pending = PendingBuiltin(id)) {
            return core::SetError(CgResult::NotImplemented, "the %s tool arrives in milestone %s",
                                  pending->name, pending->milestone);
        }
        return core::SetError(CgResult::NotFound, "no tool registered for id %d",
                              static_cast<int>(id));
    }

    if (active_ == tool) {
        return CgResult::Ok;
    }

    if (active_ && context_) {
        // Switching mid-gesture must not leave half-built geometry behind.
        active_->OnCancel(context_);
        active_->OnDeactivate(context_);
    }

    active_ = tool;
    activeId_ = id;
    if (context_) {
        active_->OnActivate(context_);
    }
    CG_DEBUG("activated tool '%s'", tool->GetName());
    return CgResult::Ok;
}

ToolId ToolManagerImpl::GetActiveTool() const {
    return activeId_;
}

ITool* ToolManagerImpl::GetActiveToolInterface() {
    return active_;
}

void ToolManagerImpl::Cancel() {
    if (active_ && context_) {
        active_->OnCancel(context_);
    }
    // Falling back to Select is the documented behaviour; with no Select tool
    // registered yet the manager simply idles.
    Activate(Find(ToolId::Select) ? ToolId::Select : ToolId::None);
}

CgResult ToolManagerImpl::RegisterTool(ITool* tool) {
    if (!tool) {
        return core::SetError(CgResult::InvalidArgument, "RegisterTool: tool is null");
    }

    const ToolId id = tool->GetId();
    if (id == ToolId::None) {
        return core::SetError(CgResult::InvalidArgument,
                              "RegisterTool: ToolId::None is not a valid registration id");
    }

    const auto key = static_cast<int32_t>(id);
    if (const auto it = tools_.find(key); it != tools_.end()) {
        // Replacing a built-in is legitimate: a host may want its own Select.
        if (it->second == active_) {
            if (context_) {
                active_->OnCancel(context_);
                active_->OnDeactivate(context_);
            }
            active_ = nullptr;
        }
        it->second->Release();
        it->second = tool;
    } else {
        tools_.emplace(key, tool);
    }

    if (activeId_ == id) {
        active_ = tool;
        if (context_) {
            active_->OnActivate(context_);
        }
    }
    return CgResult::Ok;
}

CgResult ToolManagerImpl::UnregisterTool(ToolId id) {
    const auto it = tools_.find(static_cast<int32_t>(id));
    if (it == tools_.end()) {
        return core::SetError(CgResult::NotFound, "UnregisterTool: no tool with id %d",
                              static_cast<int>(id));
    }

    if (it->second == active_) {
        if (context_) {
            active_->OnCancel(context_);
            active_->OnDeactivate(context_);
        }
        active_ = nullptr;
        activeId_ = ToolId::None;
    }
    it->second->Release();
    tools_.erase(it);
    return CgResult::Ok;
}

void ToolManagerImpl::SetSnapMask(uint32_t snapMask) {
    settings_.snapMask = snapMask;
}

uint32_t ToolManagerImpl::GetSnapMask() const {
    return settings_.snapMask;
}

void ToolManagerImpl::SetSnapTolerance(double pixels) {
    if (pixels > 0.0) {
        settings_.tolerancePixels = pixels;
    }
}

void ToolManagerImpl::SetContinuousMode(bool enabled) {
    settings_.continuous = enabled;
}

bool ToolManagerImpl::IsContinuousMode() const {
    return settings_.continuous;
}

void ToolManagerImpl::SetContext(IToolContext* context) {
    if (context_ == context) {
        return;  // Called before every dispatch; the common case is no change.
    }
    // The old context is going away as far as the tool is concerned, so it gets
    // the same courtesy as a tool switch: drop the half-built gesture rather
    // than carry it into a different view with a different camera.
    if (active_ && context_) {
        active_->OnCancel(context_);
        active_->OnDeactivate(context_);
    }
    context_ = context;
    if (active_ && context_) {
        active_->OnActivate(context_);
    }
}

} // namespace cadgeom::api
