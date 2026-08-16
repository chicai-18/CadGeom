#pragma once

#include <cadgeom/IScene.h>

#include "api/CommandStackImpl.h"
#include "api/EntityImpl.h"
#include "api/GeometryBuilderImpl.h"
#include "api/SelectionImpl.h"
#include "core/ObjectTracker.h"
#include "geom/Kernel.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace cadgeom::api {

class SceneImpl final : public IScene {
public:
    SceneImpl();
    ~SceneImpl() override;

    // -- IScene -------------------------------------------------------------

    IGeometryBuilder* GetGeometryBuilder() override;

    EntityId CreateGroup(const char* utf8Name, EntityId parent) override;
    CgResult DestroyEntity(EntityId entity) override;
    CgResult DestroyEntities(CgSpan<const EntityId> entities) override;
    void Clear() override;

    bool Exists(EntityId entity) const override;
    IEntity* GetEntity(EntityId entity) override;
    const IEntity* GetEntity(EntityId entity) const override;

    uint32_t GetEntityCount() const override;
    EntityId GetEntityAt(uint32_t index) const override;
    uint32_t GetRootCount() const override;
    EntityId GetRootAt(uint32_t index) const override;

    CgResult SetParent(EntityId entity, EntityId parent) override;

    bool GetBounds(Aabb& out) const override;
    bool Raycast(const Ray& ray, uint32_t pickFilter, PickResult& out) const override;

    ISelection* GetSelection() override;
    const ISelection* GetSelection() const override;
    ICommandStack* GetCommandStack() override;

    uint64_t GetRevision() const override;

    // -- Engine-side access -------------------------------------------------

    EntityImpl* FindEntity(EntityId id);
    const EntityImpl* FindEntity(EntityId id) const;

    /// The geometry kernel every shape in this scene lives in.
    geom::IGeometryKernel& Kernel() { return *kernel_; }
    const geom::IGeometryKernel& Kernel() const { return *kernel_; }

    /// Bump whenever anything a renderer or a host would observe changes.
    void BumpRevision() { ++revision_; }

    void MarkTransformDirty(EntityId entity);

    /// Adds an entity without going through the undo stack — the primitive the
    /// engine's own commands are built from.
    ///
    /// `preferredId` is how undo puts an entity back under the id it had:
    /// anything else holding that id (a later command, a host's bookkeeping)
    /// would otherwise be left pointing at nothing. Pass kInvalidEntity to
    /// allocate a fresh one.
    EntityId CreateEntityInternal(const char* utf8Name, EntityId parent, EntityId preferredId);

    /// Destroys an entity and its descendants without touching the undo stack.
    /// Any shape still attached goes back to the kernel with it.
    CgResult DestroyEntityInternal(EntityId entity);

    /// For handing `this` to ICommand callbacks without a cast at every site.
    IScene* AsInterface() { return this; }

private:
    void DestroyRecursive(EntityId id);
    bool WouldCreateCycle(EntityId entity, EntityId newParent) const;

    core::ObjectTracker tracker_;

    /// Declared first so it outlives every entity below it: an entity hands its
    /// shape back on destruction, and doing that to a dead kernel would be the
    /// last thing this scene ever did.
    std::unique_ptr<geom::IGeometryKernel> kernel_;

    std::unordered_map<uint64_t, std::unique_ptr<EntityImpl>> entities_;
    /// Insertion order for stable index-based enumeration across the boundary.
    std::vector<EntityId> order_;
    std::vector<EntityId> roots_;

    /// Monotonic and never recycled: a stale id must fail a lookup, not
    /// silently resolve to a different entity.
    uint64_t nextEntityId_{1};
    uint64_t revision_{1};

    SelectionImpl selection_;
    CommandStackImpl commands_;
    GeometryBuilderImpl builder_;
};

} // namespace cadgeom::api
