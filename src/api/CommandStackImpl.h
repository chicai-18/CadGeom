#pragma once

#include <cadgeom/ICommandStack.h>

#include "core/ObjectTracker.h"

#include <string>
#include <vector>

namespace cadgeom::api {

class SceneImpl;

/// Undo/redo for the scene.
///
/// Every engine-side mutation that can be undone goes through here: creating
/// geometry, editing parameters, deleting. Which means a command running on this
/// stack can trigger another Push — a host command whose Undo() calls
/// IScene::DestroyEntity does exactly that — and the stack has to survive it.
/// See Push() for how.
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

    // -- Engine-side access -------------------------------------------------

    /// @brief 把当前这一组整个撤销掉并丢弃，撤销栈和重做栈都不留痕迹。
    ///
    /// 一次读到一半失败的导入要的正是这个：`EndGroup()` 之后再 `Undo()` 也能把东西
    /// 清干净，但会在重做栈上留下一条「重做 Import」—— 重做出来的是那半个模型，
    /// 用户点下去只会得到刚才失败的残骸。
    ///
    /// @note 只对最外层有效：嵌套的 BeginGroup 由它们自己的 EndGroup 收尾，中止的
    ///       是整个手势。
    void AbortGroup();

    void Clear() override;
    uint32_t GetUndoCount() const override;
    uint32_t GetRedoCount() const override;
    void SetCapacity(uint32_t maxEntries) override;

private:
    class GroupCommand;

    /// Marks the stack as "inside a command" for as long as it lives, so a
    /// nested Push knows to absorb rather than record.
    class Running;

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

    /// How deep inside Execute()/Undo() the stack currently is. Non-zero means
    /// any Push arriving now is a side effect of a command already running.
    uint32_t running_{0};

    uint32_t capacity_{0};  ///< 0 = unlimited
};

} // namespace cadgeom::api
