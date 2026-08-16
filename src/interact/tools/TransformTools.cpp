#include "interact/tools/TransformTools.h"

#include <cadgeom/ICamera.h>
#include <cadgeom/ICommandStack.h>
#include <cadgeom/IEntity.h>
#include <cadgeom/IScene.h>
#include <cadgeom/ISelection.h>
#include <cadgeom/IViewport.h>

#include "core/Log.h"
#include "core/ObjectTracker.h"
#include "interact/Gizmo.h"
#include "interact/ToolContext.h"
#include "interact/tools/ToolBase.h"

#include <vector>

namespace cadgeom::interact {
namespace {

/// @brief 把一组实体的局部变换整体换掉 —— Gizmo 松手时产生的那一步。
///
/// 它只用公共接口写场景，和宿主自己写一个 ICommand 没有任何区别。这是有意的：
/// 内置工具能做的事，宿主写的工具也必须能做（CLAUDE.md 的分层约定）。
///
/// 拖拽过程中变换已经被直接改过了，所以第一次 Execute() 是一次幂等的重放；真正
/// 起作用的是之后的 Undo/Redo。
class SetTransformsCommand final : public ICommand {
public:
    explicit SetTransformsCommand(const char* label) : label_(label) {}

    void Add(EntityId entity, const Transform& before, const Transform& after) {
        records_.push_back(Record{entity, before, after});
    }

    bool IsEmpty() const { return records_.empty(); }

    void Release() override { delete this; }

    CgResult Execute(IScene* scene) override { return Apply(scene, /*forward=*/true); }
    CgResult Undo(IScene* scene) override { return Apply(scene, /*forward=*/false); }

    const char* GetName() const override { return label_; }

private:
    struct Record {
        EntityId entity;
        Transform before;
        Transform after;
    };

    ~SetTransformsCommand() override = default;

    CgResult Apply(IScene* scene, bool forward) {
        if (!scene) {
            return CgResult::InvalidState;
        }
        // 先全部校验再动手：失败的命令必须让场景保持原样。
        for (const Record& record : records_) {
            if (!scene->Exists(record.entity)) {
                return CgResult::InvalidHandle;
            }
        }
        for (const Record& record : records_) {
            scene->GetEntity(record.entity)->SetLocalTransform(forward ? record.after
                                                                       : record.before);
        }
        return CgResult::Ok;
    }

    core::ObjectTracker tracker_;
    /// 字面量，工具自己给的，活得比命令久。
    const char* label_;
    std::vector<Record> records_;
};

// ---------------------------------------------------------------------------
// Move / Rotate 的公共部分
// ---------------------------------------------------------------------------

class TransformToolBase : public ToolBase {
public:
    ToolResult OnMouseDown(const MouseEvent& e, IToolContext* ctx) override {
        if (!IsLeft(e) || !ctx) {
            return ToolResult::Ignored;
        }
        ICamera* camera = ctx->GetCamera();
        if (camera && RefreshOrigin(ctx)) {
            const GizmoHandle handle = gizmo_.HitTest(*camera, e.x, e.y);
            if (handle != GizmoHandle::None && gizmo_.BeginDrag(*camera, handle, e.x, e.y)) {
                if (Capture(ctx)) {
                    hovered_ = handle;
                    ShowPrompt(ctx);
                    return ToolResult::Handled;
                }
                gizmo_.EndDrag();
            }
        }

        // 没抓到手柄：当成一次普通拾取，用户就不用为了换选对象切回 Select。
        PickAndSelect(e, ctx);
        RefreshOrigin(ctx);
        ShowPrompt(ctx);
        return ToolResult::Handled;
    }

    ToolResult OnMouseMove(const MouseEvent& e, IToolContext* ctx) override {
        ICamera* camera = ctx ? ctx->GetCamera() : nullptr;
        if (!camera) {
            return ToolResult::Ignored;
        }

        if (gizmo_.IsDragging()) {
            Vec3d translation{};
            Quatd rotation{};
            if (gizmo_.UpdateDrag(*camera, e.x, e.y, translation, rotation)) {
                ApplyDelta(ctx, translation, rotation);
            }
            return ToolResult::Handled;
        }

        if (!RefreshOrigin(ctx)) {
            hovered_ = GizmoHandle::None;
            return ToolResult::Ignored;
        }
        hovered_ = gizmo_.HitTest(*camera, e.x, e.y);
        // 悬停在手柄上要认领这次移动，好让视口知道该重画一帧；悬停在别处不认领，
        // 否则会把相机和别的东西一起饿死。
        return hovered_ != GizmoHandle::None ? ToolResult::Handled : ToolResult::Ignored;
    }

    ToolResult OnMouseUp(const MouseEvent& e, IToolContext* ctx) override {
        if (!IsLeft(e) || !gizmo_.IsDragging()) {
            return ToolResult::Ignored;
        }
        gizmo_.EndDrag();
        Commit(ctx);
        ShowPrompt(ctx);
        return ToolResult::Completed;
    }

    void BuildPreview(IOverlayBuilder* overlay, IToolContext* ctx) override {
        if (!overlay || !ctx) {
            return;
        }
        const ICamera* camera = ctx->GetCamera();
        if (!camera || !RefreshOrigin(ctx)) {
            return;
        }
        gizmo_.BuildOverlay(*overlay, *camera,
                            gizmo_.IsDragging() ? gizmo_.DragHandle() : hovered_);
    }

protected:
    TransformToolBase(const ToolSettings& settings, GizmoMode mode) : ToolBase(settings) {
        gizmo_.SetMode(mode);
    }
    ~TransformToolBase() override = default;

    bool InProgress() const override { return gizmo_.IsDragging(); }

    void Reset() override {
        if (gizmo_.IsDragging()) {
            gizmo_.EndDrag();
        }
        captured_.clear();
        hovered_ = GizmoHandle::None;
    }

    /// 取消一次进行中的拖拽：把每个实体放回按下鼠标时的样子。
    void OnCancel(IToolContext* ctx) override {
        if (gizmo_.IsDragging() && ctx) {
            IScene* scene = ctx->GetScene();
            for (const Captured& record : captured_) {
                if (scene && scene->Exists(record.entity)) {
                    scene->GetEntity(record.entity)->SetLocalTransform(record.localBefore);
                }
            }
            gizmo_.SetOrigin(dragOrigin_);
        }
        ToolBase::OnCancel(ctx);
    }

    /// 命令在撤销菜单里的名字。
    virtual const char* CommandLabel() const = 0;

private:
    struct Captured {
        EntityId entity;
        Transform localBefore;
        Mat4d worldBefore{Mat4Identity()};
        /// 父链世界变换的逆。把世界增量折回局部变换要用它。
        Mat4d parentInverse{Mat4Identity()};
    };

    /// 把 Gizmo 摆到选择集包围盒中心。@return 有选中的东西时为 true。
    bool RefreshOrigin(IToolContext* ctx) {
        if (gizmo_.IsDragging()) {
            return true;  // 拖拽中原点由 ApplyDelta 维护。
        }
        IScene* scene = ctx ? ctx->GetScene() : nullptr;
        ISelection* selection = scene ? scene->GetSelection() : nullptr;
        Aabb bounds{};
        if (!selection || selection->GetCount() == 0 || !selection->GetBounds(bounds)) {
            return false;
        }
        gizmo_.SetOrigin(Center(bounds));
        return true;
    }

    /// 记下拖拽开始时每个被选实体的状态。@return 有东西可拖时为 true。
    bool Capture(IToolContext* ctx) {
        captured_.clear();
        IScene* scene = ctx ? ctx->GetScene() : nullptr;
        ISelection* selection = scene ? scene->GetSelection() : nullptr;
        if (!selection) {
            return false;
        }

        for (uint32_t i = 0; i < selection->GetCount(); ++i) {
            const EntityId id = selection->GetAt(i);
            IEntity* entity = scene->GetEntity(id);
            if (!entity) {
                continue;
            }
            // 祖先也在选择集里的话跳过：变换会沿层级传下去，两边都动就是动了两次。
            bool ancestorSelected = false;
            for (EntityId cursor = entity->GetParent(); IsValid(cursor);) {
                if (selection->Contains(cursor)) {
                    ancestorSelected = true;
                    break;
                }
                IEntity* parent = scene->GetEntity(cursor);
                cursor = parent ? parent->GetParent() : kInvalidEntity;
            }
            if (ancestorSelected) {
                continue;
            }

            Captured record{};
            record.entity = id;
            entity->GetLocalTransform(record.localBefore);
            entity->GetWorldTransform(record.worldBefore);

            Mat4d parentWorld = Mat4Identity();
            if (IEntity* parent = scene->GetEntity(entity->GetParent())) {
                parent->GetWorldTransform(parentWorld);
            }
            record.parentInverse = Inverse(parentWorld);
            captured_.push_back(record);
        }

        dragOrigin_ = gizmo_.Origin();
        return !captured_.empty();
    }

    /// 把累计增量应用到每个被捕获的实体上。增量在世界空间里合成，再折回局部 ——
    /// 带父节点的实体因此不必分情况讨论。
    void ApplyDelta(IToolContext* ctx, const Vec3d& translation, const Quatd& rotation) {
        IScene* scene = ctx ? ctx->GetScene() : nullptr;
        if (!scene) {
            return;
        }

        Transform deltaXform{};
        if (gizmo_.Mode() == GizmoMode::Rotate) {
            deltaXform.rotation = rotation;
            // 绕 Gizmo 原点转，而不是绕世界原点。
            deltaXform.translation = dragOrigin_ - Rotate(rotation, dragOrigin_);
        } else {
            deltaXform.translation = translation;
        }
        const Mat4d delta = ToMatrix(deltaXform);

        for (const Captured& record : captured_) {
            IEntity* entity = scene->GetEntity(record.entity);
            if (!entity) {
                continue;
            }
            Transform local{};
            if (Decompose(record.parentInverse * delta * record.worldBefore, local)) {
                entity->SetLocalTransform(local);
            } else {
                // 镜像或错切的父链分解不出 TRS。宁可不动，也不给一个看起来合理
                // 但其实是错的姿态。
                CG_WARN("transform: entity %llu sits under a transform that does not decompose; "
                        "it is left where it was",
                        static_cast<unsigned long long>(record.entity.value));
            }
        }

        if (gizmo_.Mode() == GizmoMode::Translate) {
            // 平移时 Gizmo 跟着对象走；旋转时它待在转轴上不动。
            gizmo_.SetOrigin(dragOrigin_ + translation);
        }
    }

    /// 拖拽结束：把「按下时」和「松手时」的局部变换打包成一步撤销。
    void Commit(IToolContext* ctx) {
        IScene* scene = ctx ? ctx->GetScene() : nullptr;
        if (!scene || captured_.empty()) {
            captured_.clear();
            return;
        }

        auto* command = new SetTransformsCommand(CommandLabel());
        for (const Captured& record : captured_) {
            IEntity* entity = scene->GetEntity(record.entity);
            if (!entity) {
                continue;
            }
            Transform after{};
            entity->GetLocalTransform(after);
            command->Add(record.entity, record.localBefore, after);
        }
        captured_.clear();

        if (command->IsEmpty()) {
            command->Release();
            return;
        }
        // 一次没挪动的点击也会走到这里；命令是幂等的，代价只是撤销栈里多一步，
        // 而那一步确实对应用户做过的一个动作。
        ctx->PushCommand(command);
    }

    Gizmo gizmo_;
    GizmoHandle hovered_{GizmoHandle::None};
    std::vector<Captured> captured_;
    Vec3d dragOrigin_{0.0, 0.0, 0.0};
};

class MoveTool final : public TransformToolBase {
public:
    explicit MoveTool(const ToolSettings& settings)
        : TransformToolBase(settings, GizmoMode::Translate) {}

    ToolId GetId() const override { return ToolId::Move; }
    const char* GetName() const override { return "Move"; }

protected:
    const char* Prompt() const override {
        return InProgress() ? "Drag to move" : "Select an object, then drag an axis or plane";
    }
    const char* CommandLabel() const override { return "Move"; }

private:
    ~MoveTool() override = default;
};

class RotateTool final : public TransformToolBase {
public:
    explicit RotateTool(const ToolSettings& settings)
        : TransformToolBase(settings, GizmoMode::Rotate) {}

    ToolId GetId() const override { return ToolId::Rotate; }
    const char* GetName() const override { return "Rotate"; }

protected:
    const char* Prompt() const override {
        return InProgress() ? "Drag to rotate" : "Select an object, then drag a ring";
    }
    const char* CommandLabel() const override { return "Rotate"; }

private:
    ~RotateTool() override = default;
};

} // namespace

void RegisterTransformTools(IToolManager& manager, const ToolSettings& settings) {
    manager.RegisterTool(new MoveTool(settings));
    manager.RegisterTool(new RotateTool(settings));
    CG_DEBUG("registered 2 transform tools");
}

} // namespace cadgeom::interact
