#include "api/SceneImpl.h"

#include "core/Error.h"
#include "core/Log.h"
#include "core/Math.h"

#include <algorithm>

namespace cadgeom::api {

SceneImpl::SceneImpl() : selection_(*this), commands_(*this), builder_(*this) {}

SceneImpl::~SceneImpl() {
    // Drop commands before entities: a command may hold ids it wants to report
    // on, and releasing in the other order would have it looking at rubble.
    commands_.Clear();
    entities_.clear();
    order_.clear();
    roots_.clear();
}

IGeometryBuilder* SceneImpl::GetGeometryBuilder() {
    return &builder_;
}

EntityId SceneImpl::CreateGroup(const char* utf8Name, EntityId parent) {
    if (IsValid(parent) && !Exists(parent)) {
        core::SetError(CgResult::InvalidHandle, "CreateGroup: parent %llu does not exist",
                       static_cast<unsigned long long>(parent.value));
        return kInvalidEntity;
    }

    const EntityId id{nextEntityId_++};
    entities_.emplace(id.value, std::make_unique<EntityImpl>(*this, id, utf8Name, parent));
    order_.push_back(id);

    if (IsValid(parent)) {
        FindEntity(parent)->AddChild(id);
    } else {
        roots_.push_back(id);
    }

    BumpRevision();
    return id;
}

CgResult SceneImpl::DestroyEntity(EntityId entity) {
    EntityImpl* e = FindEntity(entity);
    if (!e) {
        return core::SetError(CgResult::InvalidHandle, "DestroyEntity: unknown entity %llu",
                              static_cast<unsigned long long>(entity.value));
    }

    // Unhook from the parent first so the recursive teardown below never walks
    // back up into a list that is being mutated.
    const EntityId parent = e->GetParent();
    if (IsValid(parent)) {
        if (EntityImpl* p = FindEntity(parent)) {
            p->RemoveChild(entity);
        }
    } else {
        roots_.erase(std::remove(roots_.begin(), roots_.end(), entity), roots_.end());
    }

    DestroyRecursive(entity);
    BumpRevision();
    return CgResult::Ok;
}

CgResult SceneImpl::DestroyEntities(CgSpan<const EntityId> entities) {
    if (!entities.data && entities.count > 0) {
        return core::SetError(CgResult::InvalidArgument, "DestroyEntities: null span");
    }

    // Keep going after a bad id — a caller deleting a selection should not be
    // left with a half-deleted scene because one entry went stale.
    CgResult worst = CgResult::Ok;
    for (size_t i = 0; i < entities.count; ++i) {
        if (!Exists(entities.data[i])) {
            continue;  // Already gone, possibly as a descendant of an earlier id.
        }
        const CgResult r = DestroyEntity(entities.data[i]);
        if (CgFailed(r) && CgSucceeded(worst)) {
            worst = r;
        }
    }
    return worst;
}

void SceneImpl::DestroyRecursive(EntityId id) {
    EntityImpl* e = FindEntity(id);
    if (!e) {
        return;
    }

    // Copy: the child list is destroyed along with its owner.
    const std::vector<EntityId> children = e->Children();
    for (const EntityId child : children) {
        DestroyRecursive(child);
    }

    selection_.OnEntityDestroyed(id);
    order_.erase(std::remove(order_.begin(), order_.end(), id), order_.end());
    entities_.erase(id.value);
}

void SceneImpl::Clear() {
    selection_.Clear();
    commands_.Clear();
    entities_.clear();
    order_.clear();
    roots_.clear();
    BumpRevision();
    CG_DEBUG("scene cleared");
}

bool SceneImpl::Exists(EntityId entity) const {
    return entities_.find(entity.value) != entities_.end();
}

EntityImpl* SceneImpl::FindEntity(EntityId id) {
    const auto it = entities_.find(id.value);
    return it != entities_.end() ? it->second.get() : nullptr;
}

const EntityImpl* SceneImpl::FindEntity(EntityId id) const {
    const auto it = entities_.find(id.value);
    return it != entities_.end() ? it->second.get() : nullptr;
}

IEntity* SceneImpl::GetEntity(EntityId entity) {
    return FindEntity(entity);
}

const IEntity* SceneImpl::GetEntity(EntityId entity) const {
    return FindEntity(entity);
}

uint32_t SceneImpl::GetEntityCount() const {
    return static_cast<uint32_t>(order_.size());
}

EntityId SceneImpl::GetEntityAt(uint32_t index) const {
    return index < order_.size() ? order_[index] : kInvalidEntity;
}

uint32_t SceneImpl::GetRootCount() const {
    return static_cast<uint32_t>(roots_.size());
}

EntityId SceneImpl::GetRootAt(uint32_t index) const {
    return index < roots_.size() ? roots_[index] : kInvalidEntity;
}

bool SceneImpl::WouldCreateCycle(EntityId entity, EntityId newParent) const {
    EntityId cursor = newParent;
    while (IsValid(cursor)) {
        if (cursor == entity) {
            return true;
        }
        const EntityImpl* p = FindEntity(cursor);
        if (!p) {
            return false;
        }
        cursor = p->GetParent();
    }
    return false;
}

CgResult SceneImpl::SetParent(EntityId entity, EntityId parent) {
    EntityImpl* e = FindEntity(entity);
    if (!e) {
        return core::SetError(CgResult::InvalidHandle, "SetParent: unknown entity %llu",
                              static_cast<unsigned long long>(entity.value));
    }
    if (IsValid(parent) && !Exists(parent)) {
        return core::SetError(CgResult::InvalidHandle, "SetParent: unknown parent %llu",
                              static_cast<unsigned long long>(parent.value));
    }
    if (WouldCreateCycle(entity, parent)) {
        return core::SetError(CgResult::InvalidArgument,
                              "SetParent: would make entity %llu its own ancestor",
                              static_cast<unsigned long long>(entity.value));
    }

    const EntityId oldParent = e->GetParent();
    if (oldParent == parent) {
        return CgResult::Ok;
    }

    if (IsValid(oldParent)) {
        FindEntity(oldParent)->RemoveChild(entity);
    } else {
        roots_.erase(std::remove(roots_.begin(), roots_.end(), entity), roots_.end());
    }

    e->SetParentId(parent);

    if (IsValid(parent)) {
        FindEntity(parent)->AddChild(entity);
    } else {
        roots_.push_back(entity);
    }

    // Reparenting is documented as preserving the world transform. Doing that
    // properly needs the inverse of the new parent's world matrix, which comes
    // with the transform-propagation pass in M1; until then the local transform
    // is carried over unchanged.
    MarkTransformDirty(entity);
    return CgResult::Ok;
}

bool SceneImpl::GetBounds(Aabb& out) const {
    Aabb acc = AabbEmpty();
    bool any = false;
    for (const auto& [id, entity] : entities_) {
        Aabb b{};
        if (entity->IsVisible() && entity->GetWorldBounds(b)) {
            Expand(acc, b);
            any = true;
        }
    }
    if (!any) {
        return false;
    }
    out = acc;
    return true;
}

bool SceneImpl::Raycast(const Ray& ray, uint32_t pickFilter, PickResult& out) const {
    (void)ray;
    (void)pickFilter;
    (void)out;
    core::SetError(CgResult::NotImplemented,
                   "Raycast needs the BVH and the picker, which land in milestone M3");
    return false;
}

ISelection* SceneImpl::GetSelection() {
    return &selection_;
}

const ISelection* SceneImpl::GetSelection() const {
    return &selection_;
}

ICommandStack* SceneImpl::GetCommandStack() {
    return &commands_;
}

uint64_t SceneImpl::GetRevision() const {
    return revision_;
}

void SceneImpl::MarkTransformDirty(EntityId entity) {
    // M1 turns this into a real dirty-set walked once per frame. For now the
    // revision counter is enough for a host to know something moved.
    (void)entity;
    BumpRevision();
}

} // namespace cadgeom::api
