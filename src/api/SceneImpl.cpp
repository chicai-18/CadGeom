#include "api/SceneImpl.h"

#include "api/Commands.h"
#include "core/Error.h"
#include "core/Log.h"
#include "core/Math.h"
#include "geom/Snap.h"

#include <algorithm>

namespace cadgeom::api {
namespace {

/// IScene::Raycast 没有相机可问，只好按场景尺寸给一个容差：包围盒对角线的这个
/// 比例。一个纯几何的射线查询本来就没有「6 个像素」可言，而零容差意味着永远选
/// 不中一条零厚度的曲线。视口拾取走的是 RaycastWithTolerance，那里有真的像素。
constexpr double kSceneRelativeTolerance = 0.004;

} // namespace

SceneImpl::SceneImpl()
    : kernel_(geom::CreateSimpleKernel()), selection_(*this), commands_(*this), builder_(*this) {}

SceneImpl::~SceneImpl() {
    // Drop commands before entities: a command may hold ids it wants to report
    // on, and releasing in the other order would have it looking at rubble. A
    // command holding a detached shape gives it back to the kernel here, which
    // is why the kernel is the last member standing.
    commands_.Clear();
    entities_.clear();
    order_.clear();
    roots_.clear();
}

IGeometryBuilder* SceneImpl::GetGeometryBuilder() {
    return &builder_;
}

EntityId SceneImpl::CreateEntityInternal(const char* utf8Name, EntityId parent,
                                         EntityId preferredId) {
    if (IsValid(parent) && !Exists(parent)) {
        core::SetError(CgResult::InvalidHandle, "parent %llu does not exist",
                       static_cast<unsigned long long>(parent.value));
        return kInvalidEntity;
    }

    EntityId id = preferredId;
    if (IsValid(id)) {
        if (Exists(id)) {
            core::SetError(CgResult::InvalidState, "entity id %llu is already in use",
                           static_cast<unsigned long long>(id.value));
            return kInvalidEntity;
        }
        // Keep the counter ahead of anything ever handed out, so a fresh id can
        // never collide with one an undo is holding in reserve.
        nextEntityId_ = std::max(nextEntityId_, id.value + 1);
    } else {
        id = EntityId{nextEntityId_++};
    }

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

EntityId SceneImpl::CreateGroup(const char* utf8Name, EntityId parent) {
    return CreateEntityInternal(utf8Name, parent, kInvalidEntity);
}

CgResult SceneImpl::DestroyEntityInternal(EntityId entity) {
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

CgResult SceneImpl::DestroyEntity(EntityId entity) {
    return DestroyEntities(CgSpan<const EntityId>{&entity, 1});
}

CgResult SceneImpl::DestroyEntities(CgSpan<const EntityId> entities) {
    if (!entities.data && entities.count > 0) {
        return core::SetError(CgResult::InvalidArgument, "DestroyEntities: null span");
    }

    // Through a command, so deleting is as undoable as creating. The command
    // reduces the request to its outermost entities, which is what lets a
    // caller pass a selection holding both a parent and its child.
    auto* command = new DestroyEntitiesCommand(*this, entities);
    if (!command->HasTargets()) {
        command->Release();
        return core::SetError(CgResult::InvalidHandle,
                              "DestroyEntities: none of the %zu id(s) name a live entity",
                              entities.count);
    }
    return commands_.Push(command);
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

    // Captured before the relink, because the old parent chain is what defines
    // the world transform the entity has to keep.
    Mat4d world{};
    e->GetWorldTransform(world);

    e->SetParentId(parent);

    if (IsValid(parent)) {
        FindEntity(parent)->AddChild(entity);
    } else {
        roots_.push_back(entity);
    }

    // Rebase the local transform so the entity does not move on screen. A
    // sheared parent chain cannot be expressed as a TRS and the decomposition
    // says so; the entity then keeps its old local transform rather than being
    // handed a silently wrong one.
    Mat4d parentWorld = Mat4Identity();
    if (IsValid(parent)) {
        FindEntity(parent)->GetWorldTransform(parentWorld);
    }
    Transform rebased{};
    if (Decompose(Inverse(parentWorld) * world, rebased)) {
        e->SetLocalTransform(rebased);
    } else {
        CG_WARN("SetParent: the new parent's transform does not decompose; entity %llu keeps "
                "its local transform and will move",
                static_cast<unsigned long long>(entity.value));
    }

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

// ---------------------------------------------------------------------------
// 拾取与吸附
// ---------------------------------------------------------------------------

bool SceneImpl::IsEffectivelyVisible(EntityId entity) const {
    EntityId cursor = entity;
    while (IsValid(cursor)) {
        const EntityImpl* e = FindEntity(cursor);
        if (!e || !e->IsVisible()) {
            return false;
        }
        cursor = e->GetParent();
    }
    return true;
}

void SceneImpl::RefreshBvh() const {
    // 跟几何 revision 而不是显示 revision：换个高亮不该让整棵树重建。
    if (bvhValid_ && bvhRevision_ == geometryRevision_) {
        return;
    }

    std::vector<scene::BvhItem> items;
    items.reserve(order_.size());
    for (const EntityId id : order_) {
        const EntityImpl* entity = FindEntity(id);
        if (!entity || !IsValid(entity->GetShape())) {
            continue;  // 组节点自己没有几何，它的子节点各自入索引。
        }
        EntityStyle style{};
        entity->GetStyle(style);
        if (!style.selectable || !IsEffectivelyVisible(id)) {
            continue;
        }
        Aabb bounds{};
        if (!entity->GetWorldBounds(bounds) || IsEmpty(bounds)) {
            continue;
        }
        items.push_back(scene::BvhItem{id, bounds});
    }

    const size_t count = items.size();
    bvh_.Build(std::move(items));
    bvhRevision_ = geometryRevision_;
    bvhValid_ = true;
    CG_TRACE("bvh rebuilt: %zu selectable entities at geometry revision %llu", count,
             static_cast<unsigned long long>(geometryRevision_));
}

bool SceneImpl::GetPickTarget(EntityId entity, interact::PickTarget& out) const {
    const EntityImpl* e = FindEntity(entity);
    if (!e || !IsValid(e->GetShape())) {
        return false;
    }

    // Resolve 会按需细分。它交回来的指针在形状参数变动之前一直有效，而参数一变
    // 就会推进 revision，正好也是索引重建的时刻。
    const geom::Shape* shape = LazyKernel().Resolve(e->GetShape());
    if (!shape) {
        return false;
    }

    e->GetWorldTransform(out.world);
    // 按 positions 判空而不是按 IsEmpty()：一个 Point 图元只有一个顶点、没有
    // 任何线段，而它恰恰是最该被顶点拾取选中的东西。
    out.wire = shape->wire.positions.empty() ? nullptr : &shape->wire;
    out.mesh = shape->mesh.IsEmpty() ? nullptr : &shape->mesh;

    const ShapeParams& params = shape->def.params;
    switch (params.type) {
        case ShapeType::Circle:
            out.planeNormal = params.circle.plane.normal;
            out.hasPlane = true;
            break;
        case ShapeType::Arc:
            out.planeNormal = params.arc.plane.normal;
            out.hasPlane = true;
            break;
        case ShapeType::Rectangle:
            out.planeNormal = params.rectangle.plane.normal;
            out.hasPlane = true;
            break;
        default:
            // 直线、点和多段线没有唯一的承载平面，拾取会退回面向观察者的法线。
            out.hasPlane = false;
            break;
    }
    return true;
}

bool SceneImpl::RaycastWithTolerance(const Ray& ray, uint32_t pickFilter,
                                     const interact::PickTolerance& tolerance,
                                     PickResult& out) const {
    RefreshBvh();
    return interact::Raycast(bvh_, *this, ray, pickFilter, tolerance, out);
}

bool SceneImpl::Raycast(const Ray& ray, uint32_t pickFilter, PickResult& out) const {
    RefreshBvh();

    interact::PickTolerance tolerance{};
    if (!IsEmpty(bvh_.Bounds())) {
        tolerance.base = kSceneRelativeTolerance * DiagonalLength(bvh_.Bounds());
    }
    return interact::Raycast(bvh_, *this, ray, pickFilter, tolerance, out);
}

void SceneImpl::CollectSnapCandidates(const Vec3d& center, double radius, uint32_t mask,
                                      std::vector<interact::SnapCandidate>& out) const {
    if (!(radius > 0.0) || mask == 0) {
        return;
    }
    RefreshBvh();

    // 先把光标附近的实体拿出来：特征点和交点都要用，而交点还需要两两配对。
    std::vector<EntityId> nearby;
    bvh_.QuerySphere(center, radius, [&](const scene::BvhItem& item) {
        nearby.push_back(item.entity);
    });
    if (nearby.empty()) {
        return;
    }

    std::vector<geom::SnapPoint> points;
    for (const EntityId id : nearby) {
        const EntityImpl* entity = FindEntity(id);
        if (!entity) {
            continue;
        }
        const geom::Shape* shape = LazyKernel().Resolve(entity->GetShape());
        if (!shape) {
            continue;
        }
        Mat4d world{};
        entity->GetWorldTransform(world);

        points.clear();
        geom::CollectSnapPoints(shape->def, world, mask, points);
        for (const geom::SnapPoint& point : points) {
            if (Distance(point.point, center) <= radius) {
                out.push_back(interact::SnapCandidate{point.type, point.point, id});
            }
        }
    }

    if ((mask & Snap_Intersection) == 0 || nearby.size() < 2) {
        return;
    }
    // 交点是两条曲线的事，所以得配对。附近的实体通常只有两三个，而每条曲线又只
    // 有落在搜索球里的那几段参与，配对的代价因此是常数级的。
    std::vector<Vec3d> hits;
    for (size_t i = 0; i < nearby.size(); ++i) {
        const EntityImpl* a = FindEntity(nearby[i]);
        const geom::Shape* shapeA = a ? LazyKernel().Resolve(a->GetShape()) : nullptr;
        if (!shapeA || shapeA->wire.IsEmpty()) {
            continue;
        }
        Mat4d worldA{};
        a->GetWorldTransform(worldA);

        for (size_t j = i + 1; j < nearby.size(); ++j) {
            const EntityImpl* b = FindEntity(nearby[j]);
            const geom::Shape* shapeB = b ? LazyKernel().Resolve(b->GetShape()) : nullptr;
            if (!shapeB || shapeB->wire.IsEmpty()) {
                continue;
            }
            Mat4d worldB{};
            b->GetWorldTransform(worldB);

            hits.clear();
            geom::IntersectWires(shapeA->wire, worldA, shapeB->wire, worldB, center, radius, hits);
            for (const Vec3d& hit : hits) {
                out.push_back(interact::SnapCandidate{Snap_Intersection, hit, nearby[i]});
            }
        }
    }
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
    // World transforms are still walked on demand rather than cached, so there
    // is no dirty set to add to yet — the revision counter is what tells a
    // renderer its snapshot went stale. The cached-world-matrix flush belongs
    // with the SoA EntityStore, and neither pays for itself until the scene is
    // big enough for the walk to show up in a profile.
    (void)entity;
    BumpRevision();
}

} // namespace cadgeom::api
