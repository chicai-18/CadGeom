#include "interact/Snap.h"

#include <cadgeom/ICamera.h>

#include "interact/WorkPlane.h"

#include <algorithm>

namespace cadgeom::interact {
namespace {

/// 吸附类型的优先级，数字小的赢。同一个位置上端点和中点常常都在容差里，靠距离
/// 分不出来，得有一个固定的先后 —— 否则光标抖一个像素结果就换一个，画出来的
/// 东西对不上用户的预期。
int Priority(SnapType type) {
    switch (type) {
        case Snap_Endpoint:     return 0;
        case Snap_Intersection: return 1;
        case Snap_Midpoint:     return 2;
        case Snap_Center:       return 3;
        case Snap_Quadrant:     return 4;
        case Snap_Perpendicular:return 5;
        default:                return 6;
    }
}

/// 几何吸附一共有哪些位。Snap_Grid 不需要几何，单独处理。
constexpr uint32_t kGeometryMask = Snap_Endpoint | Snap_Midpoint | Snap_Center | Snap_Quadrant |
                                   Snap_Intersection | Snap_Perpendicular;

} // namespace

bool FindSnap(const ISnapSource* source, const ICamera& camera, const WorkPlane& plane,
              double screenX, double screenY, const SnapQuery& query, SnapResult& out) {
    Ray ray{};
    camera.ScreenToRay(screenX, screenY, ray);

    Vec3d raw{};
    if (!RayHitsWorkPlane(plane, ray, raw)) {
        return false;
    }

    out.type = Snap_None;
    out.point = raw;
    out.entity = kInvalidEntity;

    const uint32_t geometryMask = query.mask & kGeometryMask;
    if (source && geometryMask != 0 && query.tolerancePixels > 0.0) {
        // 搜索半径从原始落点处的像素尺寸换算 —— 候选点还没找到，没有别的地方
        // 可以量这个比例。
        const double radius = camera.GetPixelWorldSize(raw) * query.tolerancePixels;

        SnapQuery narrowed = query;
        narrowed.mask = geometryMask;

        std::vector<SnapCandidate> candidates;
        source->CollectSnapCandidates(raw, radius, narrowed, candidates);

        const SnapCandidate* best = nullptr;
        int bestPriority = 0;
        double bestPixels = 0.0;
        for (const SnapCandidate& candidate : candidates) {
            Vec2d pixel{};
            if (!camera.WorldToScreen(candidate.point, pixel)) {
                continue;  // 在相机背后，用户看不见它，也就不该吸到它。
            }
            const double dx = pixel.x - screenX;
            const double dy = pixel.y - screenY;
            const double pixels = std::sqrt(dx * dx + dy * dy);
            if (pixels > query.tolerancePixels) {
                continue;
            }
            const int priority = Priority(candidate.type);
            if (!best || priority < bestPriority ||
                (priority == bestPriority && pixels < bestPixels)) {
                best = &candidate;
                bestPriority = priority;
                bestPixels = pixels;
            }
        }

        if (best) {
            out.type = best->type;
            out.point = best->point;
            out.entity = best->entity;
            return true;
        }
    }

    // 几何吸附没命中才轮到网格：吸到一条看得见的几何上，永远比吸到一个想象中的
    // 格点更接近用户的意图。
    if ((query.mask & Snap_Grid) != 0 && query.gridSpacing > 0.0) {
        out.point = SnapToGrid(plane, raw, query.gridSpacing);
        out.type = Snap_Grid;
    }
    return true;
}

} // namespace cadgeom::interact
