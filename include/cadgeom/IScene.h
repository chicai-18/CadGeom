// CadGeom — scene graph, geometry store, selection, undo.
#ifndef CADGEOM_ISCENE_H
#define CADGEOM_ISCENE_H

#include <cadgeom/Export.h>
#include <cadgeom/Types.h>

namespace cadgeom {

class IEntity;
class IGeometryBuilder;
class ISelection;
class ICommandStack;

/// Owned by the engine — do not release it.
///
/// The scene stores entities in a flat SoA table keyed by EntityId, with a
/// parent link for hierarchy and a BVH for picking and frustum culling. Ids are
/// never reused within a session, so a stale id fails a lookup instead of
/// silently addressing someone else's entity.
class CADGEOM_API IScene {
public:
    // -- Creation & lifetime ------------------------------------------------

    /// Geometry creation lives on the builder; it is owned by the scene.
    virtual IGeometryBuilder* GetGeometryBuilder() = 0;

    /// Empty entity with no geometry — useful as a group/assembly node.
    virtual EntityId CreateGroup(const char* utf8Name, EntityId parent) = 0;

    /// Undoable. Destroys descendants as well.
    virtual CgResult DestroyEntity(EntityId entity) = 0;
    virtual CgResult DestroyEntities(CgSpan<const EntityId> entities) = 0;

    /// Drops every entity, the undo stack and the selection.
    virtual void Clear() = 0;

    // -- Access -------------------------------------------------------------

    virtual bool Exists(EntityId entity) const = 0;

    /// Borrowed pointer, null when the id is unknown. Valid until that entity
    /// is destroyed — see IEntity's lifetime note.
    virtual IEntity* GetEntity(EntityId entity) = 0;
    virtual const IEntity* GetEntity(EntityId entity) const = 0;

    /// Flat enumeration over every entity, root-level or not.
    virtual uint32_t GetEntityCount() const = 0;
    virtual EntityId GetEntityAt(uint32_t index) const = 0;

    /// Roots are entities with no parent.
    virtual uint32_t GetRootCount() const = 0;
    virtual EntityId GetRootAt(uint32_t index) const = 0;

    /// Undoable. Keeps the entity's world transform by rebasing its local one.
    virtual CgResult SetParent(EntityId entity, EntityId parent) = 0;

    // -- Queries ------------------------------------------------------------

    /// World bounds of everything visible. False when the scene is empty.
    virtual bool GetBounds(Aabb& out) const = 0;

    /// Ray cast against the BVH. Priority is vertex, then edge, then face
    /// (docs/architecture.md §6.3). False when nothing is hit.
    virtual bool Raycast(const Ray& ray, uint32_t pickFilter, PickResult& out) const = 0;

    // -- Sub-systems --------------------------------------------------------

    virtual ISelection* GetSelection() = 0;
    virtual const ISelection* GetSelection() const = 0;
    virtual ICommandStack* GetCommandStack() = 0;

    // -- Change tracking ----------------------------------------------------

    /// Bumped on every structural or geometric change. A host can poll this to
    /// decide whether to redraw instead of rendering unconditionally.
    virtual uint64_t GetRevision() const = 0;

protected:
    virtual ~IScene() = default;
};

} // namespace cadgeom

#endif // CADGEOM_ISCENE_H
