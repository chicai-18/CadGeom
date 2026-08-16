/**
 * @file Snap.h
 * @brief 一个形状能提供哪些特征点，以及两条离散曲线的交点
 *        （docs/architecture.md §6.3）。
 *
 * 特征点是从**参数化定义**上算出来的，不是从三角形或线段缓存上找出来的：圆心
 * 不是网格上的任何一个顶点，象限点也不一定落在离散后的某个采样点上。这条和
 * 「params 是 SSOT」是同一件事 —— 吸附到缓存上会随细分容差漂移，吸附到参数上
 * 不会。
 *
 * 只有交点是个例外：它是两条曲线离散之后的产物，本来就没有解析形式。
 */
#pragma once

#include <cadgeom/Types.h>

#include "core/Math.h"
#include "geom/Shape.h"

#include <vector>

namespace cadgeom::geom {

/// @brief 一个候选吸附点，已经在世界空间。
struct SnapPoint {
    SnapType type{Snap_None};
    Vec3d point{0.0, 0.0, 0.0};
};

/// @brief 收集一个形状的特征点，按 `mask` 过滤，追加到 `out`。
/// @param def   参数化定义 —— 特征点从它算，不从网格缓存里找。
/// @param world 该形状所属实体的世界变换。
/// @param mask  哪些 SnapType 是打开的；Snap_Grid / Snap_Intersection 在这里
///              没有意义，会被忽略。
/// @note 多段线的顶点走 Snap_Endpoint、每段中点走 Snap_Midpoint；它没有圆心，
///       所以不产生 Snap_Center。
void CollectSnapPoints(const ShapeDef& def, const Mat4d& world, uint32_t mask,
                       std::vector<SnapPoint>& out);

/// @brief 两条离散曲线在 `center` 附近 `radius` 内的交点。
/// @param a      第一条曲线的线框，@param worldA 它的世界变换。
/// @param b      第二条曲线的线框，@param worldB 它的世界变换。
/// @param center 只关心这个点附近 —— 光标位置。
/// @param radius 搜索半径（世界单位），同时也是「两段离得多近算相交」的容差。
/// @note 三维里两条曲线几乎不会精确相交，所以判据是最近接近距离而不是求解。
///       同一条曲线的自交不在这里处理。
void IntersectWires(const PolylineData& a, const Mat4d& worldA, const PolylineData& b,
                    const Mat4d& worldB, const Vec3d& center, double radius,
                    std::vector<Vec3d>& out);

} // namespace cadgeom::geom
