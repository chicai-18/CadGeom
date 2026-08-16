/**
 * @file Snap.h
 * @brief 吸附：把光标拉到附近几何的特征点上（docs/architecture.md §6.3）。
 *
 * 判据是**屏幕距离**，不是世界距离。用户是照着屏幕点的，容差也是照着屏幕给的
 * （「8 个像素以内」），换成世界距离之后一缩放就不是一回事了。
 *
 * 和 Picker 一样，候选点得由 api/ 那边喂下来 —— 参数化定义在内核里，而内核归
 * 场景管。这里只声明 ISnapSource，并负责「挑哪一个」这件纯交互的事。
 */
#pragma once

#include <cadgeom/Types.h>

#include "core/Math.h"

#include <vector>

namespace cadgeom {
class ICamera;
}

namespace cadgeom::interact {

/// @brief 一个候选吸附点，带上它来自哪个实体。
struct SnapCandidate {
    SnapType type{Snap_None};
    Vec3d point{0.0, 0.0, 0.0};
    EntityId entity{kInvalidEntity};
};

/// @brief 吸附向场景要候选点的通道，由 api/ 实现。
class ISnapSource {
public:
    virtual ~ISnapSource() = default;

    /// @brief 追加 `center` 周围 `radius` 内、类型在 `mask` 里的候选点。
    /// @note Snap_Grid 不在这里产生 —— 网格只要有工作平面就能算，不需要几何。
    /// @note Snap_Perpendicular 也不在这里产生：垂足是相对**上一个点**说的，而
    ///       IToolContext::SnapAt 的签名已经冻结，没有地方传那个参考点。它要等
    ///       M6 随吸附面板一起，用一个扩展接口补上。
    virtual void CollectSnapCandidates(const Vec3d& center, double radius, uint32_t mask,
                                       std::vector<SnapCandidate>& out) const = 0;
};

/// @brief 求光标此刻应该落在哪儿。
/// @param source          候选点来源；为 null 时只剩网格吸附和原始落点。
/// @param camera          用来投射射线，以及把候选点投回屏幕量距离。
/// @param plane           当前工作平面。
/// @param screenX,screenY 视口像素坐标，左上原点。
/// @param mask            打开了哪些 SnapType。
/// @param tolerancePixels 吸附搜索半径，像素。
/// @param gridSpacing     网格吸附对齐到的间距，模型单位；<= 0 表示不对齐。
/// @param out             命中的类型、点和实体；没有任何吸附时 type 为
///                        Snap_None、point 为工作平面上的原始落点。
/// @return false 表示光标根本没指着工作平面（视线与之平行，或落在相机背后），
///         此时 `out` 不被写入 —— 这种情况没有任何合理的点可以报。
bool FindSnap(const ISnapSource* source, const ICamera& camera, const WorkPlane& plane,
              double screenX, double screenY, uint32_t mask, double tolerancePixels,
              double gridSpacing, SnapResult& out);

} // namespace cadgeom::interact
