/**
 * @file Extrude.h
 * @brief 闭合轮廓 → 实体：网格、特征边和拓扑（docs/architecture.md §3.3）。
 *
 * ```
 * 闭合轮廓 Profile
 *    ├─→ 底面三角形（法线 = -扫掠方向）
 *    ├─→ 顶面三角形（顶点 += dir*dist，法线 = +扫掠方向，绕序翻转）
 *    ├─→ 侧面：相邻两点各自偏移 → 两个三角形，法线由四边形自身求出
 *    └─→ 拓扑：faces = { bottom, top, side[..] }，edges = { 底环、顶环、竖直棱 }
 * ```
 *
 * 圆的侧面是**一个**光滑面，法线逐顶点插值，竖直棱一根都不产出 —— 圆柱身上那些
 * 线是细分的痕迹，不是零件上的边。矩形和多段线的每一段侧面各是一个平面面，棱是
 * 真的，画出来。
 */
#pragma once

#include <cadgeom/Types.h>

#include "geom/MeshData.h"
#include "geom/Profile.h"

#include <vector>

namespace cadgeom::geom {

/// @brief 拔模角的上限（弧度）。超过它侧壁比顶面还平，斜接偏移会把轮廓翻出去。
inline constexpr double kMaxDraftAngle = 80.0 * kDegToRad;

/// @brief 扫掠的两个端环 —— 拉伸真正动的就是这两圈点。
struct SweepLoops {
    std::vector<Vec3d> base;
    std::vector<Vec3d> top;
    /// 扫掠是顺着轮廓法线（+1）还是逆着（-1）。端面朝向和侧面绕序都看它。
    double side{1.0};
};

/// @brief 求出扫掠的两个端环，顺带把这组参数校验一遍。
/// @return 失败时的原因已写进错误槽 —— 方向躺在轮廓平面里、距离为零、拔模角过大
///         或者拔模把环收得翻了过来。
/// @note 建形状时先单独调它：只有 O(n) 的代价，却能在压入撤销栈之前就把拉不出来
///       的参数挡掉，而不是等到细分时画不出东西再报警告。
/// @warning 拔模走的是逐顶点斜接偏移。环收过了头会被认出来（有一条边掉了头），但
///          凹轮廓在缺口处自交这种事认不出来 —— 那需要一整套直骨架或者多边形裁剪，
///          留给真正需要它的那一天。
CgResult BuildSweepLoops(const Profile& profile, const Vec3d& direction, double distance,
                         const ExtrudeOptions& options, SweepLoops& out);

/// @brief 沿 `direction` 把 `profile` 扫掠成实体。
/// @param profile   闭合平面轮廓，绕自身法线逆时针。
/// @param direction 扫掠方向，不必归一化；不能落在轮廓平面内。
/// @param distance  扫掠距离（沿 `direction`，模型单位），可以为负。
/// @param options   拔模角 / 双向 / 是否封端。
/// @param mesh      三角网格，函数自己先清空。
/// @param wire      特征边线框：底环、顶环，以及硬角轮廓的竖直棱。EdgePass 画的
///                  就是它。
/// @param topo      面 / 边 / 顶点，索引指向上面两个。
/// @return false 表示这组参数拉不出实体（方向躺在轮廓平面里、拔模把环翻了过来、
///         轮廓自交……），原因已写进错误槽。
bool BuildExtrusion(const Profile& profile, const Vec3d& direction, double distance,
                    const ExtrudeOptions& options, MeshData& mesh, PolylineData& wire,
                    Topology& topo);

} // namespace cadgeom::geom
