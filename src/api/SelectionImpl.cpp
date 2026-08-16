#include "api/SelectionImpl.h"

#include "api/SceneImpl.h"
#include "core/Math.h"

#include <algorithm>

namespace cadgeom::api {

SelectionImpl::SelectionImpl(SceneImpl& scene) : scene_(scene) {}

SelectionImpl::~SelectionImpl() = default;

void SelectionImpl::Clear() {
    if (order_.empty() && !IsValid(hovered_)) {
        return;
    }
    order_.clear();
    lookup_.clear();
    ClearSubElement();
    scene_.BumpDisplayRevision();
}

void SelectionImpl::Add(EntityId entity) {
    if (!IsValid(entity) || !scene_.Exists(entity)) {
        return;
    }
    if (lookup_.insert(entity.value).second) {
        order_.push_back(entity);
        ClearSubElement();
        scene_.BumpDisplayRevision();
    }
}

void SelectionImpl::Remove(EntityId entity) {
    if (lookup_.erase(entity.value) == 0) {
        return;
    }
    order_.erase(std::remove(order_.begin(), order_.end(), entity), order_.end());
    ClearSubElement();
    scene_.BumpDisplayRevision();
}

void SelectionImpl::Toggle(EntityId entity) {
    if (Contains(entity)) {
        Remove(entity);
    } else {
        Add(entity);
    }
}

void SelectionImpl::Set(CgSpan<const EntityId> entities) {
    order_.clear();
    lookup_.clear();
    ClearSubElement();
    if (entities.data) {
        for (size_t i = 0; i < entities.count; ++i) {
            const EntityId id = entities.data[i];
            if (IsValid(id) && scene_.Exists(id) && lookup_.insert(id.value).second) {
                order_.push_back(id);
            }
        }
    }
    scene_.BumpDisplayRevision();
}

bool SelectionImpl::Contains(EntityId entity) const {
    return lookup_.find(entity.value) != lookup_.end();
}

uint32_t SelectionImpl::GetCount() const {
    return static_cast<uint32_t>(order_.size());
}

EntityId SelectionImpl::GetAt(uint32_t index) const {
    return index < order_.size() ? order_[index] : kInvalidEntity;
}

void SelectionImpl::SetSubElement(PickKind kind, uint32_t subIndex) {
    // Sub-element selection only means something for a single entity: "the
    // third face" is ambiguous across a multi-selection.
    if (order_.size() != 1) {
        ClearSubElement();
        return;
    }
    subKind_ = kind;
    subIndex_ = subIndex;
    scene_.BumpDisplayRevision();
}

PickKind SelectionImpl::GetSubElementKind() const {
    return subKind_;
}

uint32_t SelectionImpl::GetSubElementIndex() const {
    return subIndex_;
}

void SelectionImpl::SetHovered(EntityId entity) {
    const EntityId next = (IsValid(entity) && scene_.Exists(entity)) ? entity : kInvalidEntity;
    if (next != hovered_) {
        hovered_ = next;
        scene_.BumpDisplayRevision();
    }
}

EntityId SelectionImpl::GetHovered() const {
    return hovered_;
}

bool SelectionImpl::GetBounds(Aabb& out) const {
    Aabb acc = AabbEmpty();
    bool any = false;
    for (const EntityId id : order_) {
        const EntityImpl* e = scene_.FindEntity(id);
        Aabb b{};
        if (e && e->GetWorldBounds(b)) {
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

void SelectionImpl::OnEntityDestroyed(EntityId entity) {
    if (lookup_.erase(entity.value) > 0) {
        order_.erase(std::remove(order_.begin(), order_.end(), entity), order_.end());
        ClearSubElement();
    }
    if (hovered_ == entity) {
        hovered_ = kInvalidEntity;
    }
}

void SelectionImpl::ClearSubElement() {
    subKind_ = PickKind::None;
    subIndex_ = 0;
}

} // namespace cadgeom::api
