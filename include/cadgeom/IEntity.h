// CadGeom — scene entity accessor.
#ifndef CADGEOM_IENTITY_H
#define CADGEOM_IENTITY_H

#include <cadgeom/Export.h>
#include <cadgeom/Types.h>

namespace cadgeom {

/// A borrowed view onto one entity.
///
/// The scene owns entities; there is no Release() here and calling delete on
/// one is undefined. A pointer stays valid until that entity is destroyed or
/// the scene is cleared — hold the EntityId across frames, not the pointer.
///
/// An entity is an id with components attached, never a base class to derive
/// from (docs/architecture.md §5). Geometry lives in the kernel and is
/// referenced through GetShape().
class CADGEOM_API IEntity {
public:
    virtual EntityId GetId() const = 0;

    /// UTF-8, never null. Owned by the engine, valid until the next SetName().
    virtual const char* GetName() const = 0;
    virtual void SetName(const char* utf8Name) = 0;

    virtual ShapeId GetShape() const = 0;
    virtual ShapeType GetShapeType() const = 0;

    /// Transform relative to the parent.
    virtual void GetLocalTransform(Transform& out) const = 0;
    virtual void SetLocalTransform(const Transform& xform) = 0;

    /// Composed transform. Reflects edits made this frame; the engine flushes
    /// the dirty hierarchy on demand rather than making the caller wait a frame.
    virtual void GetWorldTransform(Mat4d& out) const = 0;

    /// World-space bounds. Returns false when the entity has no geometry yet.
    virtual bool GetWorldBounds(Aabb& out) const = 0;

    virtual void GetStyle(EntityStyle& out) const = 0;
    virtual void SetStyle(const EntityStyle& style) = 0;

    virtual bool IsVisible() const = 0;
    virtual void SetVisible(bool visible) = 0;

    virtual EntityId GetParent() const = 0;
    virtual uint32_t GetChildCount() const = 0;
    virtual EntityId GetChild(uint32_t index) const = 0;

protected:
    virtual ~IEntity() = default;
};

} // namespace cadgeom

#endif // CADGEOM_IENTITY_H
