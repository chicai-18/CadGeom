#include "geom/Profile.h"

#include "core/Error.h"
#include "geom/Curve.h"

#include <algorithm>
#include <numeric>

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

Vec3d Centroid(const std::vector<Vec3d>& points) {
    Vec3d sum{0.0, 0.0, 0.0};
    for (const Vec3d& p : points) {
        sum += p;
    }
    return points.empty() ? sum : sum / static_cast<double>(points.size());
}

/// 环里最长的一条对角线的量级 —— 平面度和退化判据都相对它取，绝对 epsilon 对
/// CAD 是错的：以米为单位和以微米为单位的模型都合法。
double LoopScale(const std::vector<Vec3d>& points) {
    Aabb bounds = AabbEmpty();
    for (const Vec3d& p : points) {
        Expand(bounds, p);
    }
    return DiagonalLength(bounds);
}

/// Newell 法：对一个平面环，各边叉积之和是 2 * 面积 * 法线。它对采样噪声不敏感，
/// 这正是要拿它而不是拿任意三个点求法线的原因 —— 三个几乎共线的点给出的方向可以
/// 差出九十度。
Vec3d NewellNormal(const std::vector<Vec3d>& points) {
    Vec3d n{0.0, 0.0, 0.0};
    const size_t count = points.size();
    for (size_t i = 0; i < count; ++i) {
        n += Cross(points[i], points[(i + 1) % count]);
    }
    return n;
}

double Cross2(const Vec2d& a, const Vec2d& b) {
    return a.x * b.y - a.y * b.x;
}

/// 严格内部判定。落在边上的点不算「挡住」这只耳朵：共线的顶点会裁出零面积三角
/// 形，而拒绝裁它会让整个环卡死。
bool StrictlyInside(const Vec2d& p, const Vec2d& a, const Vec2d& b, const Vec2d& c) {
    const double d0 = Cross2(Vec2d{b.x - a.x, b.y - a.y}, Vec2d{p.x - a.x, p.y - a.y});
    const double d1 = Cross2(Vec2d{c.x - b.x, c.y - b.y}, Vec2d{p.x - b.x, p.y - b.y});
    const double d2 = Cross2(Vec2d{a.x - c.x, a.y - c.y}, Vec2d{p.x - c.x, p.y - c.y});
    return d0 > 0.0 && d1 > 0.0 && d2 > 0.0;
}

} // namespace

double ProfileSignedArea(const std::vector<Vec3d>& points, const Vec3d& normal) {
    return 0.5 * Dot(NewellNormal(points), Normalized(normal));
}

bool BuildProfile(const ShapeDef& def, const TessParams& tess, Profile& out) {
    out.points.clear();
    out.smooth = false;

    const ShapeParams& p = def.params;
    switch (p.type) {
        case ShapeType::Circle: {
            const uint32_t segments = ArcSegmentCount(p.circle.radius, kTwoPi, tess);
            SampleArc(p.circle.plane, p.circle.radius, 0.0, kTwoPi, segments,
                      /*dropLastPoint=*/true, out.points);
            out.plane.normal = Normalized(p.circle.plane.normal);
            out.smooth = true;
            break;
        }

        case ShapeType::Arc: {
            // 整整一圈的圆弧就是一个圆，接受它；差一点都不行 —— 开口的弧围不出
            // 一个面，端面无从谈起。
            if (std::fabs(p.arc.sweepAngle) < kTwoPi - 1e-6) {
                core::SetError(CgResult::InvalidArgument,
                               "Extrude: an open arc is not a closed profile (sweep %g rad)",
                               p.arc.sweepAngle);
                return false;
            }
            const uint32_t segments = ArcSegmentCount(p.arc.radius, kTwoPi, tess);
            SampleArc(p.arc.plane, p.arc.radius, p.arc.startAngle, p.arc.sweepAngle, segments,
                      /*dropLastPoint=*/true, out.points);
            out.plane.normal = Normalized(p.arc.plane.normal);
            out.smooth = true;
            break;
        }

        case ShapeType::Rectangle: {
            Vec3d corners[4];
            RectangleCorners(p.rectangle.plane, p.rectangle.uAxis, p.rectangle.width,
                             p.rectangle.height, corners);
            out.points.assign(corners, corners + 4);
            out.plane.normal = Normalized(p.rectangle.plane.normal);
            break;
        }

        case ShapeType::Polyline: {
            if (!def.closed) {
                core::SetError(CgResult::InvalidArgument,
                               "Extrude: an open polyline is not a closed profile");
                return false;
            }
            if (def.points.size() < 3) {
                core::SetError(CgResult::InvalidArgument,
                               "Extrude: a profile needs at least 3 points (got %zu)",
                               def.points.size());
                return false;
            }
            out.points = def.points;
            // 多段线没有自带平面，法线只能从环本身求。
            out.plane.normal = Normalized(NewellNormal(out.points));
            if (LengthSq(out.plane.normal) < kEpsilon) {
                core::SetError(CgResult::GeometryError,
                               "Extrude: the polyline encloses no area, so it has no plane");
                return false;
            }
            break;
        }

        case ShapeType::Point:
        case ShapeType::Line:
        case ShapeType::Mesh:
        case ShapeType::Solid:
        case ShapeType::None:
        default:
            core::SetError(CgResult::InvalidArgument,
                           "Extrude: a %s is not a closed profile; use a circle, a rectangle or a "
                           "closed polyline",
                           TypeName(p.type));
            return false;
    }

    if (out.points.size() < 3) {
        core::SetError(CgResult::GeometryError, "Extrude: the profile discretised to %zu point(s)",
                       out.points.size());
        return false;
    }

    out.plane.origin = Centroid(out.points);

    const double scale = LoopScale(out.points);
    const double planarTolerance = std::max(1e-9 * scale, kEpsilon);
    for (const Vec3d& point : out.points) {
        if (std::fabs(Dot(point - out.plane.origin, out.plane.normal)) > planarTolerance) {
            core::SetError(CgResult::GeometryError,
                           "Extrude: the profile is not planar, so it has no unique sweep plane");
            return false;
        }
    }

    const double area = ProfileSignedArea(out.points, out.plane.normal);
    if (std::fabs(area) <= planarTolerance * scale) {
        core::SetError(CgResult::GeometryError, "Extrude: the profile encloses no area");
        return false;
    }
    if (area < 0.0) {
        // 下游的每一条绕向规则 —— 端面的正反、侧面的外法线 —— 都建立在「环绕法线
        // 逆时针」上，所以在这里把它扳正一次，比在四个地方各判一次符号强。
        std::reverse(out.points.begin(), out.points.end());
    }
    return true;
}

bool TriangulateProfile(const Profile& profile, std::vector<uint32_t>& out) {
    const size_t count = profile.points.size();
    if (count < 3) {
        core::SetError(CgResult::GeometryError, "Extrude: a face needs at least 3 points");
        return false;
    }

    // 投到平面坐标系里做二维耳切。(uAxis, vAxis, normal) 是右手系，所以绕法线逆
    // 时针的环在这里就是逆时针的二维多边形，叉积为正即凸角。
    const WorkPlane frame = MakeWorkPlane(profile.plane.origin, profile.plane.normal);
    std::vector<Vec2d> uv(count);
    for (size_t i = 0; i < count; ++i) {
        const Vec3d d = profile.points[i] - frame.origin;
        uv[i] = Vec2d{Dot(d, frame.uAxis), Dot(d, frame.vAxis)};
    }

    std::vector<uint32_t> remaining(count);
    std::iota(remaining.begin(), remaining.end(), 0u);
    out.reserve(out.size() + 3 * (count - 2));

    while (remaining.size() > 3) {
        bool clipped = false;
        for (size_t i = 0; i < remaining.size(); ++i) {
            const size_t n = remaining.size();
            const uint32_t ia = remaining[(i + n - 1) % n];
            const uint32_t ib = remaining[i];
            const uint32_t ic = remaining[(i + 1) % n];
            const Vec2d& a = uv[ia];
            const Vec2d& b = uv[ib];
            const Vec2d& c = uv[ic];

            if (Cross2(Vec2d{b.x - a.x, b.y - a.y}, Vec2d{c.x - b.x, c.y - b.y}) <= 0.0) {
                continue;  // 凹角，切下去会切到轮廓外面。
            }
            bool blocked = false;
            for (const uint32_t other : remaining) {
                if (other != ia && other != ib && other != ic &&
                    StrictlyInside(uv[other], a, b, c)) {
                    blocked = true;
                    break;
                }
            }
            if (blocked) {
                continue;
            }

            out.push_back(ia);
            out.push_back(ib);
            out.push_back(ic);
            remaining.erase(remaining.begin() + static_cast<ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) {
            // 简单多边形总有耳朵，所以走到这里说明输入本来就不简单。
            core::SetError(CgResult::GeometryError,
                           "Extrude: the profile is self-intersecting or degenerate, so it cannot "
                           "be triangulated");
            return false;
        }
    }

    out.push_back(remaining[0]);
    out.push_back(remaining[1]);
    out.push_back(remaining[2]);
    return true;
}

} // namespace cadgeom::geom
