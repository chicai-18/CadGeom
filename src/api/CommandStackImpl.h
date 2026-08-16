#pragma once

#include <cadgeom/ICommandStack.h>

#include "core/ObjectTracker.h"

#include <string>
#include <vector>

namespace cadgeom::api {

class SceneImpl;

/// Undo/redo for the scene.
///
/// Complete as of M0 even though nothing pushes to it yet: the stack is
/// self-contained, the engine's own commands arrive with the kernel in M2, and
/// a host can already route its edits through ICommand today.
class CommandStackImpl final : public ICommandStack {
public:
    explicit CommandStackImpl(SceneImpl& scene);
    ~CommandStackImpl() override;

    CgResult Push(ICommand* command) override;

    bool CanUndo() const override;
    bool CanRedo() const override;
    CgResult Undo() override;
    CgResult Redo() override;

    const char* PeekUndoName() const override;
    const char* PeekRedoName() const override;

    void BeginGroup(const char* utf8Name) override;
    void EndGroup() override;

    void Clear() override;
    uint32_t GetUndoCount() const override;
    uint32_t GetRedoCount() const override;
    void SetCapacity(uint32_t maxEntries) override;

private:
    class GroupCommand;

    void ClearRedo();
    void TrimToCapacity();

    core::ObjectTracker tracker_;
    SceneImpl& scene_;

    std::vector<ICommand*> undo_;
    std::vector<ICommand*> redo_;

    /// Open group, if any, plus how deeply BeginGroup has been nested. Only the
    /// outermost group becomes a stack entry so one user gesture is one undo.
    GroupCommand* group_{nullptr};
    uint32_t groupDepth_{0};

    uint32_t capacity_{0};  ///< 0 = unlimited
};

} // namespace cadgeom::api
