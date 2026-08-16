/**
 * @file Intersect.h
 * @brief 射线与基本图元求交 —— 拾取的窄阶段和 Gizmo 拖拽共用的纯数学
 *        （docs/architecture.md §6.3、§6.5）。
 *
 * 这里全是 double、不分配、不认识场景也不认识 GPU，和内核其余部分同一个理由：
 * 拾取要能脱离 Vulkan 设备做单元测试。
 */
#pragma once

#include <cadgeom/Types.h>

#include "core/Math.h"

namespace cadgeom::geom {

/// @brief 射线与轴对齐包围盒求交（slab 法）。
/// @param ray   射线；`dir` 需已归一化。
/// @param box   包围盒；退化成一片或一点也能正确处理 —— 一个 Point 图元的包围盒
///              三个方向都是零厚度，而它恰恰是最需要被拾取到的东西。
/// @param tNear 命中区间近端的射线参数；起点落在盒内时为负。
/// @param tFar  命中区间远端的射线参数。
/// @return 区间存在、且不完全落在射线起点之后时为 true。
bool RayAabb(const Ray& ray, const Aabb& box, double& tNear, double& tFar);

/// @brief 射线与三角形求交（Möller–Trumbore）。
/// @param t 命中点的射线参数，仅在返回 true 时写入。
/// @return 命中且在射线正方向上时为 true。
/// @note 不做背面剔除是有意的：CAD 里从背面点选一个面是常规操作，剔除会让一块
///       薄板从另一侧变得无法拾取。
bool RayTriangle(const Ray& ray, const Vec3d& a, const Vec3d& b, const Vec3d& c, double& t);

/// @brief 射线与线段的最近接近。
/// @param rayT     射线上最近点的参数，已钳到 >= 0。
/// @param segT     线段上最近点的参数，落在 [0, 1]。
/// @param distance 两个最近点之间的距离。
void ClosestRaySegment(const Ray& ray, const Vec3d& a, const Vec3d& b, double& rayT, double& segT,
                       double& distance);

/// @brief 点到射线的距离。
/// @param rayT 射线上垂足的参数，已钳到 >= 0。
/// @return 点到该垂足的距离。
double RayPointDistance(const Ray& ray, const Vec3d& p, double& rayT);

/// @brief 两条无限直线各自最近点的参数。
/// @param p0 第一条直线上一点，@param d0 它的方向（不必归一化）。
/// @param p1 第二条直线上一点，@param d1 它的方向。
/// @return 两条直线接近平行时为 false，此时 `s0`/`s1` 不被写入 —— 沿轴拖拽的
///         Gizmo 靠这个返回值判断当前视角下这根轴是不是没法拖。
bool ClosestLineLine(const Vec3d& p0, const Vec3d& d0, const Vec3d& p1, const Vec3d& d1, double& s0,
                     double& s1);

/// @brief 两条线段的最近接近（三维）。
/// @param point    两个最近点的中点 —— 交点吸附取的就是它。
/// @param distance 两个最近点之间的距离；真正相交时为 0。
/// @note 三维里两条离散曲线几乎不会精确相交，所以交点吸附问的是「有多近」而不是
///       「是否相交」，由调用方拿容差去判。
void ClosestSegmentSegment(const Vec3d& a0, const Vec3d& a1, const Vec3d& b0, const Vec3d& b1,
                           Vec3d& point, double& distance);

} // namespace cadgeom::geom
