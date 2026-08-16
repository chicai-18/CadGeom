#pragma once

#include <cadgeom/IGeometryBuilder.h>

#include "core/ObjectTracker.h"
#include "geom/Shape.h"

namespace cadgeom::api {

class SceneImpl;

/// Parametric creation, on top of the kernel and through the undo stack.
///
/// Every method here builds a geom::ShapeDef, validates it, and pushes a
/// CreateShapeCommand — so the shape exists, the entity referencing it exists,
/// and Ctrl+Z takes both away again, without any caller having to arrange that.
///
/// The parameters are the source of truth from this point on. Nothing in this
/// class produces triangles; the kernel tessellates on demand and re-tessellates
/// when a parameter moves (docs/architecture.md §3.1).
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
    /// Validates, names and commits one definition. kInvalidEntity on failure,
    /// with the reason already in the thread's error slot.
    EntityId Build(geom::ShapeDef def);

    /// "Circle 3" — a per-type running number, so a fresh scene reads as a
    /// parts list rather than as nine objects all called "Circle".
    void MakeName(ShapeType type, char* buffer, size_t bufferSize);

    core::ObjectTracker tracker_;
    SceneImpl& scene_;

    /// One counter per ShapeType, indexed by its enum value.
    uint32_t nameCounters_[16]{};
};

} // namespace cadgeom::api
