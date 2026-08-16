#include "interact/Picker.h"

#include "geom/Intersect.h"

#include <algorithm>

namespace cadgeom::interact {
namespace {

/// 一种子元素上目前最好的命中。
struct Candidate {
    bool hit{false};
    double distance{0.0};  ///< 沿射线，越小越靠前。
    EntityId entity{kInvalidEntity};
    uint32_t subIndex{0};
    Vec3d point{0.0, 0.0, 0.0};
    Vec3d normal{0.0, 0.0, 1.0};

    /// 同类里只留最近的一个。
    bool Better(double candidateDistance) const { return !hit || candidateDistance < distance; }
};

/// 法线要走逆转置：非均匀缩放下直接变换法线会把它拧歪。
Vec3d WorldNormal(const Mat4d& world, const Vec3d& local) {
    return Normalized(TransformDirection(Transpose(Inverse(world)), local));
}

} // namespace

bool Raycast(const scene::Bvh& bvh, const IPickTargetSource& source, const Ray& ray,
             uint32_t pickFilter, const PickTolerance& tolerance, PickResult& out) {
    if (bvh.IsEmpty()) {
        return false;
    }

    // PickFilter_None 的意思是「别管子元素，告诉我是哪个对象」：内部照样全查，
    // 只是最后报成 PickKind::Entity。
    const bool wholeEntity = (pickFilter & PickFilter_All) == 0;
    const uint32_t filter = wholeEntity ? PickFilter_All : pickFilter;

    // 粗筛的外扩量要对整条射线都够大，否则远处的曲线会在进窄阶段之前就被包围盒
    // 挡掉。射线最远也只穿得过根包围盒，拿它算一个上界，然后窄阶段再用每个命中
    // 点处的精确容差去判。
    double padding = tolerance.base;
    if (tolerance.slope > 0.0 && !IsEmpty(bvh.Bounds())) {
        const double reach =
            Distance(ray.origin, Center(bvh.Bounds())) + 0.5 * DiagonalLength(bvh.Bounds());
        padding = tolerance.At(reach);
    }

    Candidate vertex;
    Candidate edge;
    Candidate face;

    bvh.Raycast(ray, padding, [&](const scene::BvhItem& item) {
        PickTarget target{};
        if (!source.GetPickTarget(item.entity, target)) {
            return;
        }

        // 曲线的法线取它的承载平面；没有承载平面（直线、点）就用面向观察者的
        // 方向，这样 SetWorkPlaneFromPick 拿到的至少是一个可用的平面。
        const Vec3d curveNormal =
            target.hasPlane ? WorldNormal(target.world, target.planeNormal) : -ray.dir;

        if (const geom::PolylineData* wire = target.wire) {
            if ((filter & PickFilter_Vertex) != 0) {
                for (size_t i = 0; i < wire->positions.size(); ++i) {
                    const Vec3d world = TransformPoint(target.world, wire->positions[i]);
                    double t = 0.0;
                    const double d = geom::RayPointDistance(ray, world, t);
                    if (d <= tolerance.At(t) && vertex.Better(t)) {
                        vertex = Candidate{true,  t, item.entity, static_cast<uint32_t>(i),
                                           world, curveNormal};
                    }
                }
            }

            if ((filter & PickFilter_Edge) != 0) {
                const uint32_t segments = wire->SegmentCount();
                for (uint32_t s = 0; s < segments; ++s) {
                    const Vec3d a = TransformPoint(target.world, wire->positions[wire->indices[s * 2]]);
                    const Vec3d b =
                        TransformPoint(target.world, wire->positions[wire->indices[s * 2 + 1]]);
                    double t = 0.0;
                    double segT = 0.0;
                    double d = 0.0;
                    geom::ClosestRaySegment(ray, a, b, t, segT, d);
                    if (d <= tolerance.At(t) && edge.Better(t)) {
                        edge = Candidate{true, t, item.entity, s, a + (b - a) * segT, curveNormal};
                    }
                }
            }
        }

        if (target.mesh && (filter & PickFilter_Face) != 0) {
            const std::vector<uint32_t>& indices = target.mesh->indices;
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                const Vec3d a = TransformPoint(target.world, target.mesh->vertices[indices[i]].position);
                const Vec3d b =
                    TransformPoint(target.world, target.mesh->vertices[indices[i + 1]].position);
                const Vec3d c =
                    TransformPoint(target.world, target.mesh->vertices[indices[i + 2]].position);
                double t = 0.0;
                if (geom::RayTriangle(ray, a, b, c, t) && face.Better(t)) {
                    face = Candidate{true,
                                     t,
                                     item.entity,
                                     static_cast<uint32_t>(i / 3),
                                     ray.origin + ray.dir * t,
                                     Normalized(Cross(b - a, c - a))};
                }
            }
        }
    });

    // 严格优先级：顶点 > 边 > 面（§6.3）。它不比谁离得近 —— 一个面总是比它自己
    // 的轮廓边先被射线碰到，按距离排就永远选不中边，而选边恰恰是用户点在轮廓上
    // 时想要的。容差已经保证了「顶点在附近」这件事本身成立。
    const Candidate* chosen = nullptr;
    PickKind kind = PickKind::None;
    if (vertex.hit && (filter & PickFilter_Vertex) != 0) {
        chosen = &vertex;
        kind = PickKind::Vertex;
    } else if (edge.hit && (filter & PickFilter_Edge) != 0) {
        chosen = &edge;
        kind = PickKind::Edge;
    } else if (face.hit && (filter & PickFilter_Face) != 0) {
        chosen = &face;
        kind = PickKind::Face;
    }
    if (!chosen) {
        return false;
    }

    out.entity = chosen->entity;
    out.kind = wholeEntity ? PickKind::Entity : kind;
    out.subIndex = chosen->subIndex;
    out.point = chosen->point;
    out.normal = chosen->normal;
    out.distance = chosen->distance;
    return true;
}

} // namespace cadgeom::interact
