#include "geom/Tessellate.h"

#include "geom/Curve.h"

namespace cadgeom::geom {
namespace {

/// Appends the sampled points of a closed loop and wires them into a chain.
void AppendClosedLoop(PolylineData& out, const Vec3d* points, uint32_t count) {
    const auto first = static_cast<uint32_t>(out.positions.size());
    out.positions.insert(out.positions.end(), points, points + count);
    out.AddChain(first, count, /*closed=*/true);
}

} // namespace

void TessellateCircle(const Plane& plane, double radius, const TessParams& tess,
                      PolylineData& out) {
    const uint32_t segments = ArcSegmentCount(radius, kTwoPi, tess);
    const auto first = static_cast<uint32_t>(out.positions.size());
    SampleArc(plane, radius, 0.0, kTwoPi, segments, /*dropLastPoint=*/true, out.positions);
    out.AddChain(first, segments, /*closed=*/true);
}

bool Tessellate(Shape& shape, const TessParams& tess) {
    shape.mesh.Clear();
    shape.wire.Clear();
    shape.bounds = AabbEmpty();
    shape.dirty = false;

    const ShapeParams& p = shape.def.params;
    switch (p.type) {
        case ShapeType::Point: {
            // No segments — a point is a position and a style, and PointPass
            // draws it from the single vertex left here.
            shape.wire.positions.push_back(p.point.position);
            break;
        }

        case ShapeType::Line: {
            shape.wire.positions.push_back(p.line.start);
            shape.wire.positions.push_back(p.line.end);
            shape.wire.AddChain(0, 2, /*closed=*/false);
            break;
        }

        case ShapeType::Circle: {
            TessellateCircle(p.circle.plane, p.circle.radius, tess, shape.wire);
            break;
        }

        case ShapeType::Arc: {
            const uint32_t segments =
                ArcSegmentCount(p.arc.radius, p.arc.sweepAngle, tess);
            SampleArc(p.arc.plane, p.arc.radius, p.arc.startAngle, p.arc.sweepAngle, segments,
                      /*dropLastPoint=*/false, shape.wire.positions);
            shape.wire.AddChain(0, segments + 1, /*closed=*/false);
            break;
        }

        case ShapeType::Rectangle: {
            Vec3d corners[4];
            RectangleCorners(p.rectangle.plane, p.rectangle.uAxis, p.rectangle.width,
                             p.rectangle.height, corners);
            AppendClosedLoop(shape.wire, corners, 4);
            break;
        }

        case ShapeType::Polyline: {
            const std::vector<Vec3d>& points = shape.def.points;
            if (points.size() < 2) {
                return false;
            }
            shape.wire.positions = points;
            shape.wire.AddChain(0, static_cast<uint32_t>(points.size()), shape.def.closed);
            break;
        }

        case ShapeType::Mesh:
        case ShapeType::Solid:
            // Solids arrive with Extrude in M4; imported meshes with the IO
            // handlers in M5. Both fill `mesh` directly rather than coming
            // through here, so reaching this point means a shape was created
            // with a type the kernel does not build yet.
            return false;

        case ShapeType::None:
        default:
            return false;
    }

    shape.wire.RecomputeBounds();
    shape.bounds = shape.wire.bounds;
    if (!shape.mesh.IsEmpty()) {
        shape.mesh.RecomputeBounds();
        Expand(shape.bounds, shape.mesh.bounds);
    }
    return true;
}

} // namespace cadgeom::geom
