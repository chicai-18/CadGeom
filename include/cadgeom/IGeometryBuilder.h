// CadGeom — parametric geometry creation.
//
// Every call here creates an entity whose geometry is a *parametric*
// definition, not a triangle list. Changing a circle's radius regenerates the
// mesh; it never edits triangles (docs/architecture.md §3.1). The parameters
// are the single source of truth and the mesh is a derived cache.
#ifndef CADGEOM_IGEOMETRYBUILDER_H
#define CADGEOM_IGEOMETRYBUILDER_H

#include <cadgeom/Export.h>
#include <cadgeom/Types.h>

namespace cadgeom {

/// Parametric definition of one shape. The active member is selected by
/// ShapeType; a union keeps this POD and cheap to pass by pointer.
struct ShapeParams {
    struct PointParams {
        Vec3d position;
    };
    struct LineParams {
        Vec3d start;
        Vec3d end;
    };
    struct CircleParams {
        Plane plane;  ///< origin = center, normal = axis
        double radius;
    };
    struct ArcParams {
        Plane plane;
        double radius;
        double startAngle;  ///< Radians, measured from the plane's reference axis.
        double sweepAngle;  ///< Radians, signed; CCW about the normal is positive.
    };
    struct RectangleParams {
        Plane plane;      ///< origin = center
        Vec3d uAxis;      ///< In-plane direction of `width`.
        double width;
        double height;
    };
    struct ExtrudeParams {
        ShapeId profile;
        Vec3d direction;
        double distance;
        ExtrudeOptions options;
    };

    ShapeType type{ShapeType::None};
    // `point` carries the initializer only so the union has a default
    // constructor at all: ExtrudeParams embeds ExtrudeOptions, which has
    // in-class initializers, and a union with such a variant member is
    // default-deleted unless some member is initialized here.
    union {
        PointParams point{};
        LineParams line;
        CircleParams circle;
        ArcParams arc;
        RectangleParams rectangle;
        ExtrudeParams extrude;
    };
};

/// Creates entities in the scene that owns this builder. Every method pushes a
/// command, so everything built here is undoable.
///
/// All of these return kInvalidEntity on failure; ask the engine for
/// GetLastError()/GetLastErrorMessage() for the reason.
class CADGEOM_API IGeometryBuilder {
public:
    virtual EntityId MakePoint(const Vec3d& position) = 0;
    virtual EntityId MakeLine(const Vec3d& start, const Vec3d& end) = 0;
    virtual EntityId MakeCircle(const Plane& plane, double radius) = 0;
    virtual EntityId MakeArc(const Plane& plane, double radius, double startAngle,
                             double sweepAngle) = 0;
    virtual EntityId MakeRectangle(const Plane& plane, const Vec3d& uAxis, double width,
                                   double height) = 0;
    virtual EntityId MakePolyline(CgSpan<const Vec3d> points, bool closed) = 0;

    /// Sweeps a closed profile into a solid, with topology (faces/edges/verts)
    /// so the result can be picked per-face and, later, fed to booleans.
    virtual EntityId Extrude(EntityId profile, const Vec3d& direction, double distance,
                             const ExtrudeOptions& options) = 0;

    /// Reads back the parametric definition. False if the entity carries no
    /// parametric shape (an imported mesh, for instance).
    virtual bool GetParams(EntityId entity, ShapeParams& out) const = 0;

    /// Edits a shape in place: marks the mesh cache dirty and re-tessellates
    /// before the next frame. `type` must match the existing shape's type.
    virtual CgResult SetParams(EntityId entity, const ShapeParams& params) = 0;

    /// Global tessellation quality. Changing it invalidates every mesh cache.
    virtual void SetTessParams(const TessParams& params) = 0;
    virtual void GetTessParams(TessParams& out) const = 0;

protected:
    virtual ~IGeometryBuilder() = default;
};

} // namespace cadgeom

#endif // CADGEOM_IGEOMETRYBUILDER_H
