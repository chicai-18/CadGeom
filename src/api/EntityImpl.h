#pragma once

#include <cadgeom/IEntity.h>

#include "core/ObjectTracker.h"

#include <string>
#include <vector>

namespace cadgeom::api {

class SceneImpl;

/// M0 scaffolding: one heap node per entity, owned by the scene.
///
/// The architecture calls for a struct-of-arrays EntityStore with dirty flags
/// (docs/architecture.md §5) — that lands in M1/M2 alongside GpuScene, which is
/// the consumer that makes SoA worth the bookkeeping. What matters now is that
/// the *interface* is already the final one, so swapping the storage out later
/// touches nothing the host can see.
class EntityImpl final : public IEntity {
public:
    EntityImpl(SceneImpl& scene, EntityId id, const char* name, EntityId parent);
    ~EntityImpl() override;

    EntityId GetId() const override;

    const char* GetName() const override;
    void SetName(const char* utf8Name) override;

    ShapeId GetShape() const override;
    ShapeType GetShapeType() const override;

    void GetLocalTransform(Transform& out) const override;
    void SetLocalTransform(const Transform& xform) override;
    void GetWorldTransform(Mat4d& out) const override;

    bool GetWorldBounds(Aabb& out) const override;

    void GetStyle(EntityStyle& out) const override;
    void SetStyle(const EntityStyle& style) override;

    bool IsVisible() const override;
    void SetVisible(bool visible) override;

    EntityId GetParent() const override;
    uint32_t GetChildCount() const override;
    EntityId GetChild(uint32_t index) const override;

    // -- Engine-side access -------------------------------------------------

    void SetParentId(EntityId parent) { parent_ = parent; }
    void AddChild(EntityId child);
    void RemoveChild(EntityId child);
    const std::vector<EntityId>& Children() const { return children_; }

    void SetShape(ShapeId shape, ShapeType type) {
        shape_ = shape;
        shapeType_ = type;
    }

private:
    core::ObjectTracker tracker_;
    SceneImpl& scene_;

    EntityId id_;
    EntityId parent_;
    std::vector<EntityId> children_;

    std::string name_;
    Transform local_{};
    EntityStyle style_{};
    ShapeId shape_{kInvalidShape};
    ShapeType shapeType_{ShapeType::None};
    bool visible_{true};
};

} // namespace cadgeom::api
