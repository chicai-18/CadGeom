#include "api/CommandStackImpl.h"

#include "api/SceneImpl.h"
#include "core/Error.h"
#include "core/Log.h"

namespace cadgeom::api {

/// Composite of everything pushed between BeginGroup and EndGroup. Children
/// have already run by the time the group lands on the stack, so Execute() only
/// matters on redo.
class CommandStackImpl::GroupCommand final : public ICommand {
public:
    explicit GroupCommand(const char* name) : name_(name ? name : "Group") {}

    void Release() override {
        for (ICommand* c : children_) {
            c->Release();
        }
        children_.clear();
        delete this;
    }

    CgResult Execute(IScene* scene) override {
        for (size_t i = 0; i < children_.size(); ++i) {
            const CgResult r = children_[i]->Execute(scene);
            if (CgFailed(r)) {
                // Roll back the part that did run, so a failed redo is a no-op
                // rather than a half-applied edit.
                for (size_t j = i; j-- > 0;) {
                    children_[j]->Undo(scene);
                }
                return r;
            }
        }
        return CgResult::Ok;
    }

    CgResult Undo(IScene* scene) override {
        CgResult worst = CgResult::Ok;
        for (size_t i = children_.size(); i-- > 0;) {
            const CgResult r = children_[i]->Undo(scene);
            if (CgFailed(r) && CgSucceeded(worst)) {
                worst = r;
            }
        }
        return worst;
    }

    const char* GetName() const override { return name_.c_str(); }

    void Adopt(ICommand* child) { children_.push_back(child); }
    bool Empty() const { return children_.empty(); }

private:
    ~GroupCommand() override = default;

    core::ObjectTracker tracker_;
    std::string name_;
    std::vector<ICommand*> children_;
};

CommandStackImpl::CommandStackImpl(SceneImpl& scene) : scene_(scene) {}

CommandStackImpl::~CommandStackImpl() {
    // An unterminated group would otherwise leak its children.
    if (group_) {
        group_->Release();
        group_ = nullptr;
    }
    Clear();
}

CgResult CommandStackImpl::Push(ICommand* command) {
    if (!command) {
        return core::SetError(CgResult::InvalidArgument, "Push: command is null");
    }

    const CgResult result = command->Execute(scene_.AsInterface());
    if (CgFailed(result)) {
        // Contract: a failed command owns nothing and changes nothing.
        CG_WARN("command '%s' failed to execute; not pushed", command->GetName());
        command->Release();
        return result;
    }

    if (group_) {
        group_->Adopt(command);
        return CgResult::Ok;
    }

    ClearRedo();
    undo_.push_back(command);
    TrimToCapacity();
    scene_.BumpRevision();
    return CgResult::Ok;
}

bool CommandStackImpl::CanUndo() const {
    return !undo_.empty() && groupDepth_ == 0;
}

bool CommandStackImpl::CanRedo() const {
    return !redo_.empty() && groupDepth_ == 0;
}

CgResult CommandStackImpl::Undo() {
    if (groupDepth_ != 0) {
        return core::SetError(CgResult::InvalidState, "Undo: a command group is still open");
    }
    if (undo_.empty()) {
        return core::SetError(CgResult::InvalidState, "Undo: nothing to undo");
    }

    ICommand* command = undo_.back();
    const CgResult result = command->Undo(scene_.AsInterface());
    if (CgFailed(result)) {
        // Leave it on the stack: the scene state is unknown, and silently
        // dropping the entry would make it unrecoverable.
        return core::SetError(result, "Undo: '%s' failed", command->GetName());
    }

    undo_.pop_back();
    redo_.push_back(command);
    scene_.BumpRevision();
    return CgResult::Ok;
}

CgResult CommandStackImpl::Redo() {
    if (groupDepth_ != 0) {
        return core::SetError(CgResult::InvalidState, "Redo: a command group is still open");
    }
    if (redo_.empty()) {
        return core::SetError(CgResult::InvalidState, "Redo: nothing to redo");
    }

    ICommand* command = redo_.back();
    const CgResult result = command->Execute(scene_.AsInterface());
    if (CgFailed(result)) {
        return core::SetError(result, "Redo: '%s' failed", command->GetName());
    }

    redo_.pop_back();
    undo_.push_back(command);
    scene_.BumpRevision();
    return CgResult::Ok;
}

const char* CommandStackImpl::PeekUndoName() const {
    return undo_.empty() ? nullptr : undo_.back()->GetName();
}

const char* CommandStackImpl::PeekRedoName() const {
    return redo_.empty() ? nullptr : redo_.back()->GetName();
}

void CommandStackImpl::BeginGroup(const char* utf8Name) {
    if (groupDepth_++ == 0) {
        group_ = new GroupCommand(utf8Name);
    }
}

void CommandStackImpl::EndGroup() {
    if (groupDepth_ == 0) {
        CG_WARN("EndGroup without a matching BeginGroup");
        return;
    }
    if (--groupDepth_ > 0) {
        return;
    }

    GroupCommand* finished = group_;
    group_ = nullptr;
    if (!finished) {
        return;
    }

    if (finished->Empty()) {
        // A gesture the user cancelled: no entry, nothing to undo.
        finished->Release();
        return;
    }

    ClearRedo();
    undo_.push_back(finished);
    TrimToCapacity();
    scene_.BumpRevision();
}

void CommandStackImpl::Clear() {
    for (ICommand* c : undo_) {
        c->Release();
    }
    undo_.clear();
    ClearRedo();
}

uint32_t CommandStackImpl::GetUndoCount() const {
    return static_cast<uint32_t>(undo_.size());
}

uint32_t CommandStackImpl::GetRedoCount() const {
    return static_cast<uint32_t>(redo_.size());
}

void CommandStackImpl::SetCapacity(uint32_t maxEntries) {
    capacity_ = maxEntries;
    TrimToCapacity();
}

void CommandStackImpl::ClearRedo() {
    for (ICommand* c : redo_) {
        c->Release();
    }
    redo_.clear();
}

void CommandStackImpl::TrimToCapacity() {
    if (capacity_ == 0) {
        return;
    }
    while (undo_.size() > capacity_) {
        undo_.front()->Release();
        undo_.erase(undo_.begin());
    }
}

} // namespace cadgeom::api
