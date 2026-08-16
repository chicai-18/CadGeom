/**
 * @file Picker.h
 * @brief 射线拾取：BVH 粗筛 + 顶点/边/面优先级（docs/architecture.md §6.3）。
 *
 * 面拾取返回的 `faceIndex` 让「点某个面 → 设为工作平面 → 在上面画圆 → 拉伸」
 * 这条 CAD 核心工作流成立，所以拓扑信息从一开始就是拾取结果的一部分，不是
 * 以后再补的字段。
 *
 * 数据从哪儿来是这一层的一个折中：实体表和几何内核都归 api/ 管，而 api/ 在
 * interact/ 上面。所以这里只声明 IPickTargetSource，由 api/ 实现它把数据递
 * 下来 —— 依赖箭头依旧朝下，拾取算法本身仍然不认识场景图。
 */
#pragma once

#include <cadgeom/Types.h>

#include "core/Math.h"
#include "geom/MeshData.h"
#include "scene/Bvh.h"

namespace cadgeom::interact {

/// @brief 拾取一个实体所需要的全部东西。
struct PickTarget {
    /// 对象空间 → 世界空间。
    Mat4d world{Mat4Identity()};
    /// 线框；点图元只有一个顶点、没有线段。可以为空。
    const geom::PolylineData* wire{nullptr};
    /// 三角网格。只有实体和导入的网格才有，曲线一律为空。
    const geom::MeshData* mesh{nullptr};
    /// 实体的拓扑，用来把命中的三角形翻译成 `faceIndex`。为空时面拾取只好退回
    /// 报三角形下标 —— 对一堆没有面可言的三角形来说，那已经是最诚实的答案。
    const geom::Topology* topology{nullptr};
    /// 该形状的承载平面（圆/圆弧/矩形有，直线和点没有）。命中一条曲线时用它当
    /// 法线，所以 SetWorkPlaneFromPick 对草图和实体一样通 —— 一条平面曲线和一个
    /// 平面面都能说出自己的法线。
    Vec3d planeNormal{0.0, 0.0, 1.0};
    bool hasPlane{false};
};

/// @brief 拾取向场景要数据的通道，由 api/ 实现。
class IPickTargetSource {
public:
    virtual ~IPickTargetSource() = default;

    /// @return false 表示该实体已消失、不可见或不可选，拾取应当跳过它。
    virtual bool GetPickTarget(EntityId entity, PickTarget& out) const = 0;
};

/// @brief 像素容差换算到世界容差的线性模型：`base + slope * t`。
///
/// 正交相机下一个像素永远是同样大，slope 为 0；透视下它随深度线性变大，正好由
/// slope 表达。把两者写成同一个式子，窄阶段就不必知道自己在哪种投影里。
struct PickTolerance {
    double base{0.0};
    double slope{0.0};

    double At(double t) const { return base + slope * (t > 0.0 ? t : 0.0); }
};

/// @brief 对场景投一条射线。
/// @param bvh        场景的空间索引，必须与 `source` 看到的是同一批实体。
/// @param source     命中候选之后从哪儿取几何。
/// @param ray        世界空间射线，`dir` 需已归一化。
/// @param pickFilter PickFilter 位组合；PickFilter_None 表示「哪种子元素都行，
///                   只告诉我是哪个对象」，此时结果的 kind 是 PickKind::Entity。
/// @param tolerance  顶点与边的命中容差；面用精确三角形求交，不吃容差。
/// @param out        仅在返回 true 时写入。
/// @return 命中为 true。**什么都没选中不是错误**，不写错误槽 —— 鼠标划过空白
///         是最常见的情况，把它记成错误会让宿主每一帧都读到一条假故障。
bool Raycast(const scene::Bvh& bvh, const IPickTargetSource& source, const Ray& ray,
             uint32_t pickFilter, const PickTolerance& tolerance, PickResult& out);

} // namespace cadgeom::interact
