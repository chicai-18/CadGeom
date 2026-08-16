#include "geom/Kernel.h"

#include "core/Error.h"
#include "core/Log.h"
#include "geom/Curve.h"
#include "geom/Extrude.h"
#include "geom/Profile.h"
#include "geom/Tessellate.h"

#include <unordered_map>

namespace cadgeom::geom {
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
        default:                   return "None";
    }
}

/// Rejects a plane whose normal is too short to orient anything.
CgResult RequirePlane(const Plane& plane, const char* what) {
    if (!IsUsablePlane(plane)) {
        return core::SetError(CgResult::InvalidArgument,
                              "%s: the plane normal is zero-length", what);
    }
    return CgResult::Ok;
}

CgResult RequirePositive(double value, const char* what, const char* field) {
    if (!(value > 0.0)) {  // Also catches NaN.
        return core::SetError(CgResult::InvalidArgument, "%s: %s must be greater than zero (got %g)",
                              what, field, value);
    }
    return CgResult::Ok;
}

class SimpleKernel final : public IGeometryKernel {
public:
    ShapeId Create(const ShapeDef& def) override {
        const CgResult valid = ValidateShapeDef(def);
        if (CgFailed(valid)) {
            return kInvalidShape;
        }

        const ShapeId id{nextId_++};
        Shape& shape = shapes_[id.value];
        shape.id = id;
        shape.def = def;
        shape.dirty = true;
        return id;
    }

    CgResult Update(ShapeId id, const ShapeDef& def) override {
        Shape* shape = Find(id);
        if (!shape) {
            return core::SetError(CgResult::InvalidHandle, "no shape with id %llu",
                                  static_cast<unsigned long long>(id.value));
        }
        if (shape->def.Type() != def.Type()) {
            return core::SetError(CgResult::InvalidArgument,
                                  "cannot turn a %s into a %s; create a new shape instead",
                                  TypeName(shape->def.Type()), TypeName(def.Type()));
        }
        const CgResult valid = ValidateShapeDef(def);
        if (CgFailed(valid)) {
            return valid;
        }

        // Parameters are the source of truth; the cache is invalidated rather
        // than patched, and nothing but Tessellate() will write it again.
        shape->def = def;
        shape->dirty = true;
        return CgResult::Ok;
    }

    bool GetDef(ShapeId id, ShapeDef& out) const override {
        const Shape* shape = Find(id);
        if (!shape) {
            return false;
        }
        out = shape->def;
        return true;
    }

    void Destroy(ShapeId id) override { shapes_.erase(id.value); }

    bool Exists(ShapeId id) const override { return Find(id) != nullptr; }

    ShapeType TypeOf(ShapeId id) const override {
        const Shape* shape = Find(id);
        return shape ? shape->def.Type() : ShapeType::None;
    }

    const Shape* Resolve(ShapeId id) override {
        Shape* shape = Find(id);
        if (!shape) {
            return nullptr;
        }
        if (shape->dirty && !Tessellate(*shape, tess_)) {
            // 校验器放行的定义细分不出东西，只可能是内核还造不出这个类型。报一次
            // 就算了，把缓存留空，而不是每帧重试。
            CG_WARN("cannot tessellate a %s: %s", TypeName(shape->def.Type()),
                    core::LastErrorMessage());
        }
        return shape;
    }

    bool GetBounds(ShapeId id, Aabb& out) override {
        const Shape* shape = Resolve(id);
        if (!shape || IsEmpty(shape->bounds)) {
            return false;
        }
        out = shape->bounds;
        return true;
    }

    void SetTessParams(const TessParams& tess) override {
        tess_ = tess;
        // Every cache was built against the old tolerance, so every cache is
        // now stale. Marking rather than rebuilding keeps this O(shapes) and
        // defers the real work to whatever actually gets drawn next.
        for (auto& [key, shape] : shapes_) {
            shape.dirty = true;
        }
    }

    void GetTessParams(TessParams& out) const override { out = tess_; }

    uint32_t GetShapeCount() const override { return static_cast<uint32_t>(shapes_.size()); }

private:
    Shape* Find(ShapeId id) {
        const auto it = shapes_.find(id.value);
        return it != shapes_.end() ? &it->second : nullptr;
    }
    const Shape* Find(ShapeId id) const {
        const auto it = shapes_.find(id.value);
        return it != shapes_.end() ? &it->second : nullptr;
    }

    std::unordered_map<uint64_t, Shape> shapes_;

    /// Monotonic and never recycled, for the same reason entity ids are not: a
    /// stale handle has to fail a lookup rather than resolve to someone else's
    /// shape.
    uint64_t nextId_{1};

    /// Defaults chosen for a model measured in millimetres: a 0.01 mm chord
    /// deviation is well below what a display can resolve, and the 12-degree
    /// angular cap keeps small circles from collapsing into triangles.
    TessParams tess_{0.01, 12.0 * kDegToRad};
};

} // namespace

CgResult ValidateShapeDef(const ShapeDef& def) {
    const ShapeParams& p = def.params;
    switch (p.type) {
        case ShapeType::Point:
            return CgResult::Ok;

        case ShapeType::Line: {
            const double span = Distance(p.line.start, p.line.end);
            if (span <= LengthTolerance(span)) {
                return core::SetError(CgResult::InvalidArgument,
                                      "Line: start and end are the same point");
            }
            return CgResult::Ok;
        }

        case ShapeType::Circle: {
            CgResult r = RequirePlane(p.circle.plane, "Circle");
            if (CgFailed(r)) {
                return r;
            }
            return RequirePositive(p.circle.radius, "Circle", "radius");
        }

        case ShapeType::Arc: {
            CgResult r = RequirePlane(p.arc.plane, "Arc");
            if (CgFailed(r)) {
                return r;
            }
            r = RequirePositive(p.arc.radius, "Arc", "radius");
            if (CgFailed(r)) {
                return r;
            }
            if (!(std::fabs(p.arc.sweepAngle) > kEpsilon)) {
                return core::SetError(CgResult::InvalidArgument,
                                      "Arc: sweepAngle must be non-zero");
            }
            if (std::fabs(p.arc.sweepAngle) > kTwoPi + kEpsilon) {
                return core::SetError(CgResult::InvalidArgument,
                                      "Arc: sweepAngle %g exceeds a full turn; use a Circle",
                                      p.arc.sweepAngle);
            }
            return CgResult::Ok;
        }

        case ShapeType::Rectangle: {
            CgResult r = RequirePlane(p.rectangle.plane, "Rectangle");
            if (CgFailed(r)) {
                return r;
            }
            r = RequirePositive(p.rectangle.width, "Rectangle", "width");
            if (CgFailed(r)) {
                return r;
            }
            r = RequirePositive(p.rectangle.height, "Rectangle", "height");
            if (CgFailed(r)) {
                return r;
            }
            // The u axis gets projected into the plane, so it only has to be
            // *not parallel* to the normal — but parallel leaves nothing to
            // project and the rectangle would silently pick its own orientation.
            const Vec3d normal = Normalized(p.rectangle.plane.normal);
            const Vec3d inPlane = p.rectangle.uAxis - normal * Dot(p.rectangle.uAxis, normal);
            if (LengthSq(inPlane) <= kEpsilon) {
                return core::SetError(CgResult::InvalidArgument,
                                      "Rectangle: uAxis is parallel to the plane normal, so it "
                                      "cannot orient the rectangle");
            }
            return CgResult::Ok;
        }

        case ShapeType::Polyline: {
            if (def.points.size() < 2) {
                return core::SetError(CgResult::InvalidArgument,
                                      "Polyline: needs at least 2 points (got %zu)",
                                      def.points.size());
            }
            if (def.closed && def.points.size() < 3) {
                return core::SetError(CgResult::InvalidArgument,
                                      "Polyline: a closed polyline needs at least 3 points");
            }
            return CgResult::Ok;
        }

        case ShapeType::Solid: {
            if (!def.profile) {
                return core::SetError(CgResult::InvalidArgument,
                                      "Solid: no profile definition; a solid is built by Extrude, "
                                      "not created from parameters alone");
            }
            // 真的把轮廓离散一遍来校验，而不是只看类型：多段线共面与否、环有没有
            // 面积，都只有离散之后才知道，而这些恰恰是最容易出问题的地方。一次拉伸
            // 只在这里多花一遍离散，换的是「建得出来就一定画得出来」。
            Profile profile{};
            if (!BuildProfile(*def.profile, TessParams{}, profile)) {
                const CgResult recorded = core::LastError();
                return CgFailed(recorded) ? recorded : CgResult::GeometryError;
            }

            SweepLoops loops{};
            return BuildSweepLoops(profile, p.extrude.direction, p.extrude.distance,
                                   p.extrude.options, loops);
        }

        case ShapeType::Mesh:
            return core::SetError(CgResult::NotImplemented,
                                  "Mesh geometry is built by the IO handlers, which land in "
                                  "milestone M5");

        case ShapeType::None:
        default:
            return core::SetError(CgResult::InvalidArgument,
                                  "ShapeType::None is not a shape anything can build");
    }
}

std::unique_ptr<IGeometryKernel> CreateSimpleKernel() {
    return std::make_unique<SimpleKernel>();
}

} // namespace cadgeom::geom
