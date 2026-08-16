#include "api/EntityImpl.h"

#include "api/SceneImpl.h"
#include "core/Math.h"

#include <algorithm>

namespace cadgeom::api {

EntityImpl::EntityImpl(SceneImpl& scene, EntityId id, const char* name, EntityId parent)
    : scene_(scene), id_(id), parent_(parent), name_(name ? name : "") {}

EntityImpl::~EntityImpl() {
    // Still attached means nobody claimed it, so the geometry dies with the
    // entity. A delete that intends to be undoable detaches first.
    if (IsValid(shape_)) {
        scene_.Kernel().Destroy(shape_);
    }
}

EntityId EntityImpl::GetId() const {
    return id_;
}

const char* EntityImpl::GetName() const {
    return name_.c_str();
}

void EntityImpl::SetName(const char* utf8Name) {
    name_ = utf8Name ? utf8Name : "";
    scene_.BumpRevision();
}

ShapeId EntityImpl::GetShape() const {
    return shape_;
}

ShapeType EntityImpl::GetShapeType() const {
    return shapeType_;
}

void EntityImpl::GetLocalTransform(Transform& out) const {
    out = local_;
}

void EntityImpl::SetLocalTransform(const Transform& xform) {
    local_ = xform;
    scene_.MarkTransformDirty(id_);
}

void EntityImpl::GetWorldTransform(Mat4d& out) const {
    // Walked on demand rather than cached. Hierarchies are shallow in CAD
    // assemblies and M0 has no per-frame consumer; the cached world matrix
    // arrives with the dirty-propagation flush in M1.
    out = ToMatrix(local_);
    EntityId cursor = parent_;
    while (IsValid(cursor)) {
        const EntityImpl* p = scene_.FindEntity(cursor);
        if (!p) {
            break;
        }
        out = ToMatrix(p->local_) * out;
        cursor = p->parent_;
    }
}

bool EntityImpl::GetWorldBounds(Aabb& out) const {
    if (!IsValid(shape_)) {
        return false;  // A group node has no geometry of its own.
    }

    // Object-space bounds from the kernel, which tessellates on demand if the
    // parameters have moved since anyone last looked.
    Aabb local{};
    if (!scene_.Kernel().GetBounds(shape_, local)) {
        return false;
    }

    Mat4d world{};
    GetWorldTransform(world);
    out = Transformed(local, world);
    return true;
}

void EntityImpl::SetShape(ShapeId shape, ShapeType type) {
    if (IsValid(shape_) && shape_ != shape) {
        scene_.Kernel().Destroy(shape_);
    }
    shape_ = shape;
    shapeType_ = type;
    scene_.BumpRevision();
}

ShapeId EntityImpl::DetachShape() {
    const ShapeId detached = shape_;
    shape_ = kInvalidShape;
    shapeType_ = ShapeType::None;
    return detached;
}

void EntityImpl::GetStyle(EntityStyle& out) const {
    out = style_;
}

void EntityImpl::SetStyle(const EntityStyle& style) {
    style_ = style;
    scene_.BumpRevision();
}

bool EntityImpl::IsVisible() const {
    return style_.visible;
}

void EntityImpl::SetVisible(bool visible) {
    if (style_.visible != visible) {
        style_.visible = visible;
        scene_.BumpRevision();
    }
}

EntityId EntityImpl::GetParent() const {
    return parent_;
}

uint32_t EntityImpl::GetChildCount() const {
    return static_cast<uint32_t>(children_.size());
}

EntityId EntityImpl::GetChild(uint32_t index) const {
    return index < children_.size() ? children_[index] : kInvalidEntity;
}

void EntityImpl::AddChild(EntityId child) {
    if (std::find(children_.begin(), children_.end(), child) == children_.end()) {
        children_.push_back(child);
    }
}

void EntityImpl::RemoveChild(EntityId child) {
    children_.erase(std::remove(children_.begin(), children_.end(), child), children_.end());
}

} // namespace cadgeom::api
