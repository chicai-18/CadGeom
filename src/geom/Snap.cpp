#include "geom/Snap.h"

#include "geom/Curve.h"
#include "geom/Intersect.h"

#include <algorithm>

namespace cadgeom::geom {
namespace {

void AddPoint(std::vector<SnapPoint>& out, uint32_t mask, SnapType type, const Mat4d& world,
              const Vec3d& local) {
    if ((mask & type) != 0) {
        out.push_back(SnapPoint{type, TransformPoint(world, local)});
    }
}

/// 圆/圆弧的四个象限点，按扫描角过滤：整圆四个都在，圆弧只留真正扫到的那几个。
void AddQuadrants(std::vector<SnapPoint>& out, const Mat4d& world, const Plane& plane,
                  double radius, double startAngle, double sweepAngle) {
    const WorkPlane frame = FrameOf(plane);
    for (int q = 0; q < 4; ++q) {
        const double angle = kHalfPi * static_cast<double>(q);
        if (std::fabs(sweepAngle) < kTwoPi - kEpsilon) {
            // 把象限角折算到扫描区间上；负向扫描先翻转再比。
            double offset = angle - startAngle;
            if (sweepAngle < 0.0) {
                offset = -offset;
            }
            offset = std::fmod(offset, kTwoPi);
            if (offset < 0.0) {
                offset += kTwoPi;
            }
            if (offset > std::fabs(sweepAngle)) {
                continue;
            }
        }
        const Vec3d local = frame.origin + frame.uAxis * (radius * std::cos(angle)) +
                            frame.vAxis * (radius * std::sin(angle));
        out.push_back(SnapPoint{Snap_Quadrant, TransformPoint(world, local)});
    }
}

/// 一条曲线上落在查询球内的线段，端点已变换到世界空间。收窄到球内是交点吸附
/// 能便宜的原因：两个圆各六十来段，全配对是几千次求解，只留光标附近的通常剩
/// 不到五段。
void NearSegments(const PolylineData& wire, const Mat4d& world, const Vec3d& center, double radius,
                  std::vector<std::pair<Vec3d, Vec3d>>& out) {
    const uint32_t segments = wire.SegmentCount();
    for (uint32_t s = 0; s < segments; ++s) {
        const Vec3d a = TransformPoint(world, wire.positions[wire.indices[s * 2]]);
        const Vec3d b = TransformPoint(world, wire.positions[wire.indices[s * 2 + 1]]);

        const Vec3d seg = b - a;
        const double lenSq = LengthSq(seg);
        const double t = lenSq > kEpsilon ? std::clamp(Dot(center - a, seg) / lenSq, 0.0, 1.0) : 0.0;
        if (Distance(a + seg * t, center) <= radius) {
            out.emplace_back(a, b);
        }
    }
}

} // namespace

void CollectSnapPoints(const Shape& shape, const Mat4d& world, uint32_t mask,
                       std::vector<SnapPoint>& out) {
    const ShapeDef& def = shape.def;
    const ShapeParams& p = def.params;
    switch (p.type) {
        case ShapeType::Point:
            // 一个点自己就是端点 —— 别的类型它都没有。
            AddPoint(out, mask, Snap_Endpoint, world, p.point.position);
            break;

        case ShapeType::Line:
            AddPoint(out, mask, Snap_Endpoint, world, p.line.start);
            AddPoint(out, mask, Snap_Endpoint, world, p.line.end);
            AddPoint(out, mask, Snap_Midpoint, world, (p.line.start + p.line.end) * 0.5);
            break;

        case ShapeType::Circle:
            AddPoint(out, mask, Snap_Center, world, p.circle.plane.origin);
            if ((mask & Snap_Quadrant) != 0) {
                AddQuadrants(out, world, p.circle.plane, p.circle.radius, 0.0, kTwoPi);
            }
            break;

        case ShapeType::Arc: {
            AddPoint(out, mask, Snap_Center, world, p.arc.plane.origin);
            const WorkPlane frame = FrameOf(p.arc.plane);
            const auto at = [&](double angle) {
                return frame.origin + frame.uAxis * (p.arc.radius * std::cos(angle)) +
                       frame.vAxis * (p.arc.radius * std::sin(angle));
            };
            AddPoint(out, mask, Snap_Endpoint, world, at(p.arc.startAngle));
            AddPoint(out, mask, Snap_Endpoint, world, at(p.arc.startAngle + p.arc.sweepAngle));
            AddPoint(out, mask, Snap_Midpoint, world, at(p.arc.startAngle + 0.5 * p.arc.sweepAngle));
            if ((mask & Snap_Quadrant) != 0) {
                AddQuadrants(out, world, p.arc.plane, p.arc.radius, p.arc.startAngle,
                             p.arc.sweepAngle);
            }
            break;
        }

        case ShapeType::Rectangle: {
            AddPoint(out, mask, Snap_Center, world, p.rectangle.plane.origin);
            Vec3d corners[4];
            RectangleCorners(p.rectangle.plane, p.rectangle.uAxis, p.rectangle.width,
                             p.rectangle.height, corners);
            for (int i = 0; i < 4; ++i) {
                AddPoint(out, mask, Snap_Endpoint, world, corners[i]);
                AddPoint(out, mask, Snap_Midpoint, world, (corners[i] + corners[(i + 1) & 3]) * 0.5);
            }
            break;
        }

        case ShapeType::Polyline: {
            const std::vector<Vec3d>& points = def.points;
            for (size_t i = 0; i < points.size(); ++i) {
                AddPoint(out, mask, Snap_Endpoint, world, points[i]);
                const bool hasNext = i + 1 < points.size();
                if (hasNext) {
                    AddPoint(out, mask, Snap_Midpoint, world, (points[i] + points[i + 1]) * 0.5);
                } else if (def.closed && points.size() >= 3) {
                    AddPoint(out, mask, Snap_Midpoint, world, (points[i] + points[0]) * 0.5);
                }
            }
            break;
        }

        case ShapeType::Solid: {
            const Topology& topo = shape.topology;
            for (const uint32_t index : topo.vertices) {
                if (index < shape.wire.positions.size()) {
                    AddPoint(out, mask, Snap_Endpoint, world, shape.wire.positions[index]);
                }
            }
            // 只有单段的边才取中点：一条竖直棱的中点是零件上一个真的位置，而底环
            // 的「中点」不过是六十四段里某一段的一半，吸上去毫无意义。
            for (const Topology::Edge& edge : topo.edges) {
                if (edge.segmentCount != 1) {
                    continue;
                }
                const uint32_t base = edge.firstSegment * 2;
                if (base + 1 >= shape.wire.indices.size()) {
                    continue;
                }
                const Vec3d& a = shape.wire.positions[shape.wire.indices[base]];
                const Vec3d& b = shape.wire.positions[shape.wire.indices[base + 1]];
                AddPoint(out, mask, Snap_Midpoint, world, (a + b) * 0.5);
            }
            break;
        }

        case ShapeType::Mesh:
        case ShapeType::None:
        default:
            // 导入网格没有参数化定义可谈，特征点得等 M5 从拓扑重建出来。
            break;
    }
}

void IntersectWires(const PolylineData& a, const Mat4d& worldA, const PolylineData& b,
                    const Mat4d& worldB, const Vec3d& center, double radius,
                    std::vector<Vec3d>& out) {
    if (!(radius > 0.0) || a.IsEmpty() || b.IsEmpty()) {
        return;
    }

    std::vector<std::pair<Vec3d, Vec3d>> nearA;
    std::vector<std::pair<Vec3d, Vec3d>> nearB;
    NearSegments(a, worldA, center, radius, nearA);
    if (nearA.empty()) {
        return;
    }
    NearSegments(b, worldB, center, radius, nearB);

    for (const auto& segA : nearA) {
        for (const auto& segB : nearB) {
            Vec3d point{};
            double distance = 0.0;
            ClosestSegmentSegment(segA.first, segA.second, segB.first, segB.second, point,
                                  distance);
            if (distance <= radius && Distance(point, center) <= radius) {
                out.push_back(point);
            }
        }
    }
}

} // namespace cadgeom::geom
