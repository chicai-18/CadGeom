#include "api/Commands.h"

#include "api/SceneImpl.h"
#include "core/Error.h"
#include "geom/Kernel.h"

#include <algorithm>

namespace cadgeom::api {
namespace {

const char* TypeName(ShapeType type) {
    switch (type) {
        case ShapeType::Point:     return "Point";
        case ShapeType::Line:      return "Line";
        case ShapeType::Circle:    return "Circle";
        case ShapeType::Arc:       return "Arc";
        case ShapeType::Rectangle: return "Rectangle";
        case ShapeType::Polyline:  return "Polyline";
        case ShapeType::Mesh:      return "Mesh";
        case ShapeType::Solid:     return "Solid";
        case ShapeType::None:
        default:                   return "Shape";
    }
}

/// Whichever error the failing call recorded, or a generic one if it recorded
/// nothing. A command must never report Ok after failing.
CgResult ReportedError(CgResult fallback) {
    const CgResult recorded = core::LastError();
    return CgFailed(recorded) ? recorded : fallback;
}

} // namespace

// ---------------------------------------------------------------------------
// CreateShapeCommand
// ---------------------------------------------------------------------------

CreateShapeCommand::CreateShapeCommand(SceneImpl& scene, geom::ShapeDef def, const char* name,
                                       EntityId parent, const Transform& local,
                                       const EntityStyle& style)
    : scene_(scene),
      def_(std::move(def)),
      // 撤销菜单里读的是动作，不是类型：一个实体是「拉伸」出来的，说「创建实体」
      // 等于把用户刚做的那件事换了个名字。
      label_(def_.Type() == ShapeType::Solid ? std::string("Extrude")
                                             : std::string("Create ") + TypeName(def_.Type())),
      entityName_(name ? name : TypeName(def_.Type())),
      parent_(parent),
      local_(local),
      style_(style) {}

CreateShapeCommand::~CreateShapeCommand() = default;

void CreateShapeCommand::Release() {
    // Undone and then dropped off the stack: nothing references the shape any
    // more, so this is the last chance to give it back to the kernel.
    if (!applied_ && IsValid(shape_)) {
        scene_.Kernel().Destroy(shape_);
        shape_ = kInvalidShape;
    }
    delete this;
}

CgResult CreateShapeCommand::Execute(IScene*) {
    if (!IsValid(shape_)) {
        shape_ = scene_.Kernel().Create(def_);
        if (!IsValid(shape_)) {
            return ReportedError(CgResult::GeometryError);
        }
    }

    // On the first run `entity_` is invalid and the scene allocates a fresh id;
    // on a redo it is the id from last time, and reusing it is what keeps any
    // other command that stored it pointing at the same object.
    const EntityId created = scene_.CreateEntityInternal(entityName_.c_str(), parent_, entity_);
    if (!IsValid(created)) {
        return ReportedError(CgResult::InvalidState);
    }
    entity_ = created;

    EntityImpl* entity = scene_.FindEntity(entity_);
    entity->SetLocalTransform(local_);
    entity->SetStyle(style_);
    entity->SetShape(shape_, def_.Type());
    applied_ = true;
    return CgResult::Ok;
}

CgResult CreateShapeCommand::Undo(IScene*) {
    EntityImpl* entity = scene_.FindEntity(entity_);
    if (!entity) {
        return core::SetError(CgResult::InvalidHandle,
                              "Undo Create: entity %llu is already gone",
                              static_cast<unsigned long long>(entity_.value));
    }
    // Detach first: the entity is about to be destroyed, and the shape has to
    // survive for the redo.
    entity->DetachShape();
    const CgResult r = scene_.DestroyEntityInternal(entity_);
    if (CgFailed(r)) {
        entity->SetShape(shape_, def_.Type());
        return r;
    }
    applied_ = false;
    return CgResult::Ok;
}

const char* CreateShapeCommand::GetName() const {
    return label_.c_str();
}

// ---------------------------------------------------------------------------
// CreateGroupCommand
// ---------------------------------------------------------------------------

CreateGroupCommand::CreateGroupCommand(SceneImpl& scene, const char* name, EntityId parent,
                                       const Transform& local)
    : scene_(scene), entityName_(name ? name : "Group"), parent_(parent), local_(local) {}

CreateGroupCommand::~CreateGroupCommand() = default;

void CreateGroupCommand::Release() {
    delete this;
}

CgResult CreateGroupCommand::Execute(IScene*) {
    // 和 CreateShapeCommand 同理：重做要把实体放回原来那个 id 上，否则任何还攥着
    // 这个 id 的东西（比如同一组里稍后建出来的子节点）就指向了空处。
    const EntityId created = scene_.CreateEntityInternal(entityName_.c_str(), parent_, entity_);
    if (!IsValid(created)) {
        return ReportedError(CgResult::InvalidState);
    }
    entity_ = created;
    scene_.FindEntity(entity_)->SetLocalTransform(local_);
    return CgResult::Ok;
}

CgResult CreateGroupCommand::Undo(IScene*) {
    if (!scene_.FindEntity(entity_)) {
        return core::SetError(CgResult::InvalidHandle, "Undo Create: entity %llu is already gone",
                              static_cast<unsigned long long>(entity_.value));
    }
    return scene_.DestroyEntityInternal(entity_);
}

const char* CreateGroupCommand::GetName() const {
    return "Create Group";
}

// ---------------------------------------------------------------------------
// SetShapeParamsCommand
// ---------------------------------------------------------------------------

SetShapeParamsCommand::SetShapeParamsCommand(SceneImpl& scene, EntityId entity,
                                             geom::ShapeDef previous, geom::ShapeDef next)
    : scene_(scene),
      entity_(entity),
      previous_(std::move(previous)),
      next_(std::move(next)) {}

SetShapeParamsCommand::~SetShapeParamsCommand() = default;

void SetShapeParamsCommand::Release() {
    delete this;
}

CgResult SetShapeParamsCommand::Apply(const geom::ShapeDef& def) {
    const EntityImpl* entity = scene_.FindEntity(entity_);
    if (!entity) {
        return core::SetError(CgResult::InvalidHandle, "SetParams: entity %llu no longer exists",
                              static_cast<unsigned long long>(entity_.value));
    }
    const CgResult r = scene_.Kernel().Update(entity->GetShape(), def);
    if (CgSucceeded(r)) {
        scene_.BumpRevision();
    }
    return r;
}

CgResult SetShapeParamsCommand::Execute(IScene*) {
    return Apply(next_);
}

CgResult SetShapeParamsCommand::Undo(IScene*) {
    return Apply(previous_);
}

const char* SetShapeParamsCommand::GetName() const {
    return "Edit Parameters";
}

// ---------------------------------------------------------------------------
// DestroyEntitiesCommand
// ---------------------------------------------------------------------------

DestroyEntitiesCommand::DestroyEntitiesCommand(SceneImpl& scene, CgSpan<const EntityId> entities)
    : scene_(scene) {
    const auto isDescendantOf = [this](EntityId candidate, EntityId ancestor) {
        EntityId cursor = candidate;
        while (IsValid(cursor)) {
            if (cursor == ancestor) {
                return true;
            }
            const EntityImpl* e = scene_.FindEntity(cursor);
            if (!e) {
                return false;
            }
            cursor = e->GetParent();
        }
        return false;
    };

    for (size_t i = 0; i < entities.count; ++i) {
        const EntityId id = entities.data[i];
        if (!scene_.FindEntity(id)) {
            continue;  // Unknown or already covered by an earlier subtree.
        }
        // Deleting a selection that holds both a parent and its child is
        // routine; reducing to the outermost entities first means the subtree
        // is captured once and restored once.
        const bool covered = std::any_of(roots_.begin(), roots_.end(), [&](EntityId root) {
            return isDescendantOf(id, root);
        });
        if (covered) {
            continue;
        }
        roots_.erase(std::remove_if(roots_.begin(), roots_.end(),
                                    [&](EntityId root) { return isDescendantOf(root, id); }),
                     roots_.end());
        roots_.push_back(id);
    }

    label_ = roots_.size() == 1 ? "Delete" : "Delete Objects";
}

DestroyEntitiesCommand::~DestroyEntitiesCommand() = default;

void DestroyEntitiesCommand::Release() {
    if (applied_) {
        // The entities are gone and nothing else references their shapes.
        for (const Record& record : records_) {
            if (IsValid(record.shape)) {
                scene_.Kernel().Destroy(record.shape);
            }
        }
    }
    delete this;
}

void DestroyEntitiesCommand::Capture(EntityId id) {
    const EntityImpl* entity = scene_.FindEntity(id);
    if (!entity) {
        return;
    }

    Record record{};
    record.id = id;
    record.parent = entity->GetParent();
    record.name = entity->GetName();
    entity->GetLocalTransform(record.local);
    entity->GetStyle(record.style);
    record.shape = entity->GetShape();
    record.shapeType = entity->GetShapeType();
    record.visible = entity->IsVisible();
    records_.push_back(std::move(record));

    // Pre-order: a parent is always recorded before its children, so restoring
    // in order never looks for a parent that is not back yet.
    for (const EntityId child : entity->Children()) {
        Capture(child);
    }
}

CgResult DestroyEntitiesCommand::Execute(IScene*) {
    // Validate everything before touching anything: a failed command has to
    // leave the scene exactly as it found it.
    for (const EntityId root : roots_) {
        if (!scene_.FindEntity(root)) {
            return core::SetError(CgResult::InvalidHandle, "Delete: entity %llu no longer exists",
                                  static_cast<unsigned long long>(root.value));
        }
    }

    records_.clear();
    for (const EntityId root : roots_) {
        Capture(root);
    }

    // The shapes outlive the entities so an undo can reattach them.
    for (const Record& record : records_) {
        if (EntityImpl* entity = scene_.FindEntity(record.id)) {
            entity->DetachShape();
        }
    }
    for (const EntityId root : roots_) {
        scene_.DestroyEntityInternal(root);
    }

    applied_ = true;
    return CgResult::Ok;
}

CgResult DestroyEntitiesCommand::Undo(IScene*) {
    for (const Record& record : records_) {
        const EntityId id = scene_.CreateEntityInternal(record.name.c_str(), record.parent,
                                                        record.id);
        if (!IsValid(id)) {
            return ReportedError(CgResult::InvalidState);
        }
        EntityImpl* entity = scene_.FindEntity(id);
        entity->SetLocalTransform(record.local);
        entity->SetStyle(record.style);
        entity->SetVisible(record.visible);
        entity->SetShape(record.shape, record.shapeType);
    }
    applied_ = false;
    return CgResult::Ok;
}

const char* DestroyEntitiesCommand::GetName() const {
    return label_.c_str();
}

} // namespace cadgeom::api
