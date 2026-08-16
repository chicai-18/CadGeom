/**
 * @file Profile.h
 * @brief 闭合平面轮廓及其三角化（docs/architecture.md §3.3 第 1、2 步）。
 *
 * 拉伸的前两步都在这里：把一个参数化图元（圆 / 矩形 / 闭合多段线）按弦高容差离
 * 散成一圈平面上的点，再把那圈点三角化成端面。两步都只认识几何 —— 不认识场景，
 * 也不认识 GPU。
 *
 * 离散用的是和曲线细分同一份 TessParams，这一点是有意的：同一个圆当轮廓拉伸和
 * 当曲线画出来，分段数必须一致，否则圆柱的底面会和它自己的轮廓线错开一圈。
 */
#pragma once

#include <cadgeom/Types.h>

#include "core/Math.h"
#include "geom/Shape.h"

#include <vector>

namespace cadgeom::geom {

/// @brief 离散之后的闭合平面轮廓。
struct Profile {
    /// 环上的点，首点不重复，绕 `plane.normal` 逆时针。
    std::vector<Vec3d> points;
    /// 承载平面；`normal` 已归一化，`origin` 是环的形心。
    Plane plane{Vec3d{0.0, 0.0, 0.0}, Vec3d{0.0, 0.0, 1.0}};
    /// 侧面该不该光滑着色。圆和整圈圆弧为 true —— 它上面每个点都是细分的产物，
    /// 不是真的棱；矩形和多段线为 false，那里每个角都是真的。
    bool smooth{false};

    bool IsEmpty() const { return points.size() < 3; }
};

/// @brief 把一个形状定义离散成闭合轮廓。
/// @param def  轮廓的参数化定义。能当轮廓的只有圆、整圈的圆弧、矩形和闭合多段线。
/// @param tess 细分容差，和曲线细分共用一份。
/// @param out  成功时被完全覆盖。
/// @return false 表示这个形状当不了轮廓，原因已写进错误槽。
/// @note 多段线还要求共面 —— 不共面的环没有唯一的「轮廓平面」可言，硬拉出来的
///       侧面会自相交。
bool BuildProfile(const ShapeDef& def, const TessParams& tess, Profile& out);

/// @brief 平面多边形三角化（耳切法）。
/// @param profile 闭合轮廓。
/// @param out     追加 3 * (n - 2) 个下标，指向 `profile.points`，绕
///                `profile.plane.normal` 逆时针。
/// @return false 表示轮廓自交或退化 —— 耳切法找不到可裁的耳朵；原因已写进错误槽。
/// @note 耳切而不是扇形剖分：扇形只对凸轮廓成立，而一块带缺口的板子是再普通不过
///       的零件。
bool TriangulateProfile(const Profile& profile, std::vector<uint32_t>& out);

/// @brief 闭合环在自己平面上的有向面积，绕 `normal` 逆时针为正。
/// @note 绕向判据和退化判据都用它：面积的符号是绕向，绝对值是「这个环还算不算
///       一个面」。
double ProfileSignedArea(const std::vector<Vec3d>& points, const Vec3d& normal);

} // namespace cadgeom::geom
