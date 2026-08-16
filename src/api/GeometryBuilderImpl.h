#pragma once

#include <cadgeom/IGeometryBuilder.h>

#include "core/ObjectTracker.h"

namespace cadgeom::api {

class SceneImpl;

/// M0 stub. The contract is final; the implementation waits on the geometry
/// kernel (docs/architecture.md §3, milestone M2).
///
/// Every creation call fails with CgResult::NotImplemented and a message that
/// names the milestone, so a host wiring itself up against these headers today
/// gets a straight answer instead of a null-pointer mystery.
class GeometryBuilderImpl final : public IGeometryBuilder {
public:
    explicit GeometryBuilderImpl(SceneImpl& scene);
    ~GeometryBuilderImpl() override;

    EntityId MakePoint(const Vec3d& position) override;
    EntityId MakeLine(const Vec3d& start, const Vec3d& end) override;
    EntityId MakeCircle(const Plane& plane, double radius) override;
    EntityId MakeArc(const Plane& plane, double radius, double startAngle,
                     double sweepAngle) override;
    EntityId MakeRectangle(const Plane& plane, const Vec3d& uAxis, double width,
                           double height) override;
    EntityId MakePolyline(CgSpan<const Vec3d> points, bool closed) override;

    EntityId Extrude(EntityId profile, const Vec3d& direction, double distance,
                     const ExtrudeOptions& options) override;

    bool GetParams(EntityId entity, ShapeParams& out) const override;
    CgResult SetParams(EntityId entity, const ShapeParams& params) override;

    void SetTessParams(const TessParams& params) override;
    void GetTessParams(TessParams& out) const override;

private:
    static EntityId NotYet(const char* what);

    core::ObjectTracker tracker_;
    SceneImpl& scene_;

    /// Defaults chosen for a model measured in millimetres: a 0.01 mm chord
    /// deviation is well below what a display can resolve, and the 12-degree
    /// angular cap keeps small circles from collapsing into triangles.
    TessParams tess_{0.01, 12.0 * 3.14159265358979323846 / 180.0};
};

} // namespace cadgeom::api
