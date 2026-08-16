// The engine's own undoable operations (docs/architecture.md §5).
//
// Every scene mutation the engine performs goes through one of these, which is
// what makes undo free rather than a retrofit. They are ICommand implementations
// like any a host would write, with one difference: they hold the concrete
// SceneImpl rather than the IScene the stack hands them, because they need the
// kernel and the entity table that the public interface deliberately hides.
//
// Shape ownership is the subtle part. A shape is created once and outlives every
// undo of the entity that references it: undoing a create detaches the shape and
// leaves the command holding it, and Release() is where it finally goes. Any
// other arrangement makes redo either leak or resurrect a dangling ShapeId.
#pragma once

#include <cadgeom/ICommandStack.h>

#include "core/ObjectTracker.h"
#include "geom/Shape.h"

#include <string>
#include <vector>

namespace cadgeom::api {

class SceneImpl;

/// Creates one entity carrying one kernel shape.
class CreateShapeCommand final : public ICommand {
public:
    /// @param local 新实体的局部变换。除了拉伸都是单位阵：实体是在轮廓的对象空间
    ///              里扫掠出来的，所以它得原样接过轮廓的那一份变换，否则轮廓一旦
    ///              被挪动过，拉出来的实体就会落在别处。
    /// @param style 新实体的显示样式。导入的实体带着文件里的颜色进来，而样式必须
    ///              由命令持有 —— 撤销之后重做，实体是重新建出来的，样式不在命令
    ///              里就会丢。
    CreateShapeCommand(SceneImpl& scene, geom::ShapeDef def, const char* name, EntityId parent,
                       const Transform& local = Transform{},
                       const EntityStyle& style = EntityStyle{});

    void Release() override;
    CgResult Execute(IScene* scene) override;
    CgResult Undo(IScene* scene) override;
    const char* GetName() const override;

    /// The entity produced by the first Execute(). Invalid before then, and
    /// stable across every later undo/redo.
    EntityId Entity() const { return entity_; }

private:
    ~CreateShapeCommand() override;

    core::ObjectTracker tracker_;
    SceneImpl& scene_;

    geom::ShapeDef def_;
    std::string label_;
    std::string entityName_;
    EntityId parent_;
    Transform local_;
    EntityStyle style_;

    EntityId entity_{kInvalidEntity};
    ShapeId shape_{kInvalidShape};
    /// True while the entity is in the scene. Drives who owns `shape_`.
    bool applied_{false};
};

/// @brief 建一个没有几何的组节点。
///
/// `IScene::CreateGroup` 不进撤销栈（组是宿主随手搭的层级，不是一次编辑）。导入
/// 不一样：文件里的层级和它的内容必须一起撤销，否则撤销之后场景里会剩下一堆空
/// 节点。所以导入走这条路。
class CreateGroupCommand final : public ICommand {
public:
    CreateGroupCommand(SceneImpl& scene, const char* name, EntityId parent,
                       const Transform& local = Transform{});

    void Release() override;
    CgResult Execute(IScene* scene) override;
    CgResult Undo(IScene* scene) override;
    const char* GetName() const override;

    /// 第一次 Execute() 建出来的实体，之后每次撤销/重做都是同一个 id。
    EntityId Entity() const { return entity_; }

private:
    ~CreateGroupCommand() override;

    core::ObjectTracker tracker_;
    SceneImpl& scene_;

    std::string entityName_;
    EntityId parent_;
    Transform local_;
    EntityId entity_{kInvalidEntity};
};

/// Rewrites a shape's parametric definition in place. The mesh is not touched:
/// it is a cache, and marking it dirty is the whole of the edit.
class SetShapeParamsCommand final : public ICommand {
public:
    /// `previous` must be the definition currently in the kernel.
    SetShapeParamsCommand(SceneImpl& scene, EntityId entity, geom::ShapeDef previous,
                          geom::ShapeDef next);

    void Release() override;
    CgResult Execute(IScene* scene) override;
    CgResult Undo(IScene* scene) override;
    const char* GetName() const override;

private:
    ~SetShapeParamsCommand() override;

    CgResult Apply(const geom::ShapeDef& def);

    core::ObjectTracker tracker_;
    SceneImpl& scene_;

    EntityId entity_;
    geom::ShapeDef previous_;
    geom::ShapeDef next_;
};

/// Destroys entities and their descendants, keeping enough to put them back.
class DestroyEntitiesCommand final : public ICommand {
public:
    DestroyEntitiesCommand(SceneImpl& scene, CgSpan<const EntityId> entities);

    void Release() override;
    CgResult Execute(IScene* scene) override;
    CgResult Undo(IScene* scene) override;
    const char* GetName() const override;

    /// False when nothing in the request resolved to a live entity, so the
    /// caller can report InvalidHandle instead of pushing an empty undo step.
    bool HasTargets() const { return !roots_.empty(); }

private:
    ~DestroyEntitiesCommand() override;

    /// One entity, captured in enough detail to be recreated identically.
    struct Record {
        EntityId id;
        EntityId parent;
        std::string name;
        Transform local;
        EntityStyle style;
        ShapeId shape;
        ShapeType shapeType;
        bool visible;
    };

    void Capture(EntityId id);

    core::ObjectTracker tracker_;
    SceneImpl& scene_;

    std::string label_;
    /// The requested entities that were live and not already covered by another
    /// entry's subtree.
    std::vector<EntityId> roots_;
    /// Pre-order, so restoring in order always finds the parent already there.
    std::vector<Record> records_;
    bool applied_{false};
};

} // namespace cadgeom::api
