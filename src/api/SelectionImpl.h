#pragma once

#include <cadgeom/ISelection.h>

#include "core/ObjectTracker.h"

#include <unordered_set>
#include <vector>

namespace cadgeom::api {

class SceneImpl;

/// Ordered vector plus a lookup set: enumeration order stays stable for the
/// host while Contains() stays O(1) for the renderer's highlight pass.
class SelectionImpl final : public ISelection {
public:
    explicit SelectionImpl(SceneImpl& scene);
    ~SelectionImpl() override;

    void Clear() override;
    void Add(EntityId entity) override;
    void Remove(EntityId entity) override;
    void Toggle(EntityId entity) override;
    void Set(CgSpan<const EntityId> entities) override;

    bool Contains(EntityId entity) const override;
    uint32_t GetCount() const override;
    EntityId GetAt(uint32_t index) const override;

    void SetSubElement(PickKind kind, uint32_t subIndex) override;
    PickKind GetSubElementKind() const override;
    uint32_t GetSubElementIndex() const override;

    void SetHovered(EntityId entity) override;
    EntityId GetHovered() const override;

    bool GetBounds(Aabb& out) const override;

    /// Called when an entity disappears, so the selection cannot outlive it.
    void OnEntityDestroyed(EntityId entity);

private:
    void ClearSubElement();

    core::ObjectTracker tracker_;
    SceneImpl& scene_;

    std::vector<EntityId> order_;
    std::unordered_set<uint64_t> lookup_;

    EntityId hovered_{kInvalidEntity};
    PickKind subKind_{PickKind::None};
    uint32_t subIndex_{0};
};

} // namespace cadgeom::api
