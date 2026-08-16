#include "api/GeometryBuilderImpl.h"

#include "api/SceneImpl.h"
#include "core/Error.h"

namespace cadgeom::api {

GeometryBuilderImpl::GeometryBuilderImpl(SceneImpl& scene) : scene_(scene) {}

GeometryBuilderImpl::~GeometryBuilderImpl() = default;

EntityId GeometryBuilderImpl::NotYet(const char* what) {
    core::SetError(CgResult::NotImplemented,
                   "%s requires the geometry kernel, which lands in milestone M2", what);
    return kInvalidEntity;
}

EntityId GeometryBuilderImpl::MakePoint(const Vec3d& position) {
    (void)position;
    return NotYet("MakePoint");
}

EntityId GeometryBuilderImpl::MakeLine(const Vec3d& start, const Vec3d& end) {
    (void)start;
    (void)end;
    return NotYet("MakeLine");
}

EntityId GeometryBuilderImpl::MakeCircle(const Plane& plane, double radius) {
    (void)plane;
    (void)radius;
    return NotYet("MakeCircle");
}

EntityId GeometryBuilderImpl::MakeArc(const Plane& plane, double radius, double startAngle,
                                      double sweepAngle) {
    (void)plane;
    (void)radius;
    (void)startAngle;
    (void)sweepAngle;
    return NotYet("MakeArc");
}

EntityId GeometryBuilderImpl::MakeRectangle(const Plane& plane, const Vec3d& uAxis, double width,
                                            double height) {
    (void)plane;
    (void)uAxis;
    (void)width;
    (void)height;
    return NotYet("MakeRectangle");
}

EntityId GeometryBuilderImpl::MakePolyline(CgSpan<const Vec3d> points, bool closed) {
    (void)points;
    (void)closed;
    return NotYet("MakePolyline");
}

EntityId GeometryBuilderImpl::Extrude(EntityId profile, const Vec3d& direction, double distance,
                                      const ExtrudeOptions& options) {
    (void)profile;
    (void)direction;
    (void)distance;
    (void)options;
    return NotYet("Extrude");
}

bool GeometryBuilderImpl::GetParams(EntityId entity, ShapeParams& out) const {
    (void)entity;
    (void)out;
    return false;
}

CgResult GeometryBuilderImpl::SetParams(EntityId entity, const ShapeParams& params) {
    (void)entity;
    (void)params;
    return core::SetError(CgResult::NotImplemented,
                          "SetParams requires the geometry kernel (milestone M2)");
}

void GeometryBuilderImpl::SetTessParams(const TessParams& params) {
    // Non-positive means "keep the default" rather than "tessellate infinitely".
    if (params.chordTolerance > 0.0) {
        tess_.chordTolerance = params.chordTolerance;
    }
    if (params.angularTolerance > 0.0) {
        tess_.angularTolerance = params.angularTolerance;
    }
    scene_.BumpRevision();
}

void GeometryBuilderImpl::GetTessParams(TessParams& out) const {
    out = tess_;
}

} // namespace cadgeom::api
