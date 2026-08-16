/**
 * @file Axis.h
 * @brief Z-up ↔ Y-up 换算。**整个引擎里只有 IO 层做这件事。**
 *
 * CadGeom 是 Z-up 右手系（CAD 行业惯例），glTF 规范规定 Y-up，而 OBJ 虽然没有规范
 * 可言，事实上的惯例（Blender、Maya、绝大多数查看器的默认设置）也是 Y-up。
 * 换算就是绕 X 轴转 ±90°，一个纯旋转，长度和角度一样都不动。
 *
 * 内核、场景、渲染器一概不知道有这回事 —— 越界一次，就得在每个地方都记得它
 * （docs/architecture.md §0.1、§7）。
 */
#pragma once

#include <cadgeom/Types.h>

#include "core/Math.h"

namespace cadgeom::io {

/// cos(45°)，也就是 ±90° 旋转四元数里那个分量。
inline constexpr double kAxisSwapCos = 0.7071067811865476;

/// @brief Z-up 的向量换成 Y-up。
inline Vec3d ToYUp(const Vec3d& v) {
    return Vec3d{v.x, v.z, -v.y};
}

/// @brief Y-up 的向量换回 Z-up。
inline Vec3d ToZUp(const Vec3d& v) {
    return Vec3d{v.x, -v.z, v.y};
}

/// @brief 把 Z-up 的内容摆进 Y-up 世界的那个旋转（绕 X 轴 -90°）。
/// @note 导出时挂在根节点上，而不是烤进顶点里：顶点保持在对象空间，`extras` 里的
///       参数化定义（同样是对象空间的）因此和它对得上，往返一遍分毫不差。
inline Quatd YUpRotation() {
    return Quatd{-kAxisSwapCos, 0.0, 0.0, kAxisSwapCos};
}

/// @brief 反过来：Y-up 的内容摆回 Z-up（绕 X 轴 +90°）。
inline Quatd ZUpRotation() {
    return Quatd{kAxisSwapCos, 0.0, 0.0, kAxisSwapCos};
}

} // namespace cadgeom::io
