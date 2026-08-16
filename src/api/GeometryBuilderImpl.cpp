#include "api/GeometryBuilderImpl.h"

#include "api/Commands.h"
#include "api/SceneImpl.h"
#include "core/Error.h"
#include "geom/Kernel.h"

#include <stdio.h>

namespace cadgeom::api {
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
        default:                   return "Shape";
    }
}

} // namespace

GeometryBuilderImpl::GeometryBuilderImpl(SceneImpl& scene) : scene_(scene) {}

GeometryBuilderImpl::~GeometryBuilderImpl() = default;

void GeometryBuilderImpl::MakeName(ShapeType type, char* buffer, size_t bufferSize) {
    const auto index = static_cast<size_t>(type);
    const uint32_t n =
        index < (sizeof(nameCounters_) / sizeof(nameCounters_[0])) ? ++nameCounters_[index] : 1u;
    snprintf(buffer, bufferSize, "%s %u", TypeName(type), n);
}

EntityId GeometryBuilderImpl::Build(geom::ShapeDef def) {
    // Rejected before a command is built around it, so a bad call costs an
    // allocation of nothing and the host gets the specific reason.
    const CgResult valid = geom::ValidateShapeDef(def);
    if (CgFailed(valid)) {
        return kInvalidEntity;
    }

    char name[64];
    MakeName(def.Type(), name, sizeof(name));

    auto* command = new CreateShapeCommand(scene_, std::move(def), name, kInvalidEntity);
    // Push executes, and on failure releases: reading `command` afterwards is
    // only safe on the success path, where the stack now owns it.
    if (CgFailed(scene_.GetCommandStack()->Push(command))) {
        return kInvalidEntity;
    }
    return command->Entity();
}

EntityId GeometryBuilderImpl::MakePoint(const Vec3d& position) {
    geom::ShapeDef def{};
    def.params.type = ShapeType::Point;
    def.params.point.position = position;
    return Build(std::move(def));
}

EntityId GeometryBuilderImpl::MakeLine(const Vec3d& start, const Vec3d& end) {
    geom::ShapeDef def{};
    def.params.type = ShapeType::Line;
    def.params.line.start = start;
    def.params.line.end = end;
    return Build(std::move(def));
}

EntityId GeometryBuilderImpl::MakeCircle(const Plane& plane, double radius) {
    geom::ShapeDef def{};
    def.params.type = ShapeType::Circle;
    def.params.circle.plane = plane;
    def.params.circle.radius = radius;
    return Build(std::move(def));
}

EntityId GeometryBuilderImpl::MakeArc(const Plane& plane, double radius, double startAngle,
                                      double sweepAngle) {
    geom::ShapeDef def{};
    def.params.type = ShapeType::Arc;
    def.params.arc.plane = plane;
    def.params.arc.radius = radius;
    def.params.arc.startAngle = startAngle;
    def.params.arc.sweepAngle = sweepAngle;
    return Build(std::move(def));
}

EntityId GeometryBuilderImpl::MakeRectangle(const Plane& plane, const Vec3d& uAxis, double width,
                                            double height) {
    geom::ShapeDef def{};
    def.params.type = ShapeType::Rectangle;
    def.params.rectangle.plane = plane;
    def.params.rectangle.uAxis = uAxis;
    def.params.rectangle.width = width;
    def.params.rectangle.height = height;
    return Build(std::move(def));
}

EntityId GeometryBuilderImpl::MakePolyline(CgSpan<const Vec3d> points, bool closed) {
    if (!points.data && points.count > 0) {
        core::SetError(CgResult::InvalidArgument, "MakePolyline: null point span");
        return kInvalidEntity;
    }

    geom::ShapeDef def{};
    def.params.type = ShapeType::Polyline;
    def.points.assign(points.data, points.data + points.count);
    def.closed = closed;
    return Build(std::move(def));
}

EntityId GeometryBuilderImpl::Extrude(EntityId profile, const Vec3d& direction, double distance,
                                      const ExtrudeOptions& options) {
    (void)profile;
    (void)direction;
    (void)distance;
    (void)options;
    core::SetError(CgResult::NotImplemented,
                   "Extrude needs profile triangulation and solid topology, which land in "
                   "milestone M4");
    return kInvalidEntity;
}

bool GeometryBuilderImpl::GetParams(EntityId entity, ShapeParams& out) const {
    const EntityImpl* e = scene_.FindEntity(entity);
    if (!e) {
        core::SetError(CgResult::InvalidHandle, "GetParams: unknown entity %llu",
                       static_cast<unsigned long long>(entity.value));
        return false;
    }
    if (!IsValid(e->GetShape())) {
        return false;  // A group node. Documented: no parametric shape, no params.
    }

    geom::ShapeDef def{};
    if (!scene_.Kernel().GetDef(e->GetShape(), def)) {
        return false;
    }
    // A polyline comes back with its type set and the union unused: the point
    // list is variable-length and ShapeParams is a frozen POD (§2.2 rule 1), so
    // there is nowhere in it for the points to go. Switch on `type` and there is
    // nothing to misread.
    out = def.params;
    return true;
}

CgResult GeometryBuilderImpl::SetParams(EntityId entity, const ShapeParams& params) {
    const EntityImpl* e = scene_.FindEntity(entity);
    if (!e) {
        return core::SetError(CgResult::InvalidHandle, "SetParams: unknown entity %llu",
                              static_cast<unsigned long long>(entity.value));
    }
    const ShapeId shape = e->GetShape();
    if (!IsValid(shape)) {
        return core::SetError(CgResult::InvalidState,
                              "SetParams: entity %llu carries no parametric shape",
                              static_cast<unsigned long long>(entity.value));
    }

    geom::ShapeDef previous{};
    if (!scene_.Kernel().GetDef(shape, previous)) {
        return core::SetError(CgResult::InvalidHandle, "SetParams: shape %llu is not in the kernel",
                              static_cast<unsigned long long>(shape.value));
    }
    if (previous.Type() != params.type) {
        return core::SetError(CgResult::InvalidArgument,
                              "SetParams: entity %llu is a %s, not a %s; changing what a shape is "
                              "means creating a new one",
                              static_cast<unsigned long long>(entity.value),
                              TypeName(previous.Type()), TypeName(params.type));
    }
    if (params.type == ShapeType::Polyline) {
        return core::SetError(CgResult::NotSupported,
                              "SetParams: a polyline's points cannot travel in ShapeParams, which "
                              "is a frozen POD union; recreate the polyline instead");
    }

    geom::ShapeDef next = previous;
    next.params = params;
    const CgResult valid = geom::ValidateShapeDef(next);
    if (CgFailed(valid)) {
        return valid;
    }

    return scene_.GetCommandStack()->Push(
        new SetShapeParamsCommand(scene_, entity, std::move(previous), std::move(next)));
}

void GeometryBuilderImpl::SetTessParams(const TessParams& params) {
    // Non-positive means "keep the default" rather than "tessellate infinitely".
    TessParams merged{};
    scene_.Kernel().GetTessParams(merged);
    if (params.chordTolerance > 0.0) {
        merged.chordTolerance = params.chordTolerance;
    }
    if (params.angularTolerance > 0.0) {
        merged.angularTolerance = params.angularTolerance;
    }

    // Invalidates every cached tessellation, which is why this bumps the
    // revision: the renderer's copy of the geometry is now out of date too.
    scene_.Kernel().SetTessParams(merged);
    scene_.BumpRevision();
}

void GeometryBuilderImpl::GetTessParams(TessParams& out) const {
    scene_.Kernel().GetTessParams(out);
}

} // namespace cadgeom::api
