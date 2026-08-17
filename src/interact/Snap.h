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

/// @brief 一次吸附查询的全部输入，除了「在哪个像素」。
///
/// 打包成一个结构而不是排成一串参数，是因为 M6 给它添了参考点：`FindSnap` 的形参
/// 已经排到第八个，再加两个就没人数得清哪个是哪个了。
struct SnapQuery {
    /// 打开了哪些 SnapType。
    uint32_t mask{Snap_None};
    /// 吸附搜索半径，像素。
    double tolerancePixels{8.0};
    /// 网格吸附对齐到的间距，模型单位；<= 0 表示不对齐。
    double gridSpacing{0.0};
    /// 垂足吸附的参考点 —— 「上一个点」。
    Vec3d reference{0.0, 0.0, 0.0};
    /// 没有参考点时 Snap_Perpendicular 不产生任何候选点：垂足是相对某个点说的，
    /// 没有那个点就没有这件事。
    bool hasReference{false};
};

/// @brief 吸附向场景要候选点的通道，由 api/ 实现。
class ISnapSource {
public:
    virtual ~ISnapSource() = default;

    /// @brief 追加 `center` 周围 `radius` 内、类型在 `query.mask` 里的候选点。
    /// @note Snap_Grid 不在这里产生 —— 网格只要有工作平面就能算，不需要几何。
    /// @note Snap_Perpendicular 要 `query.hasReference` 为真才产生：垂足是从参考
    ///       点向曲线作垂线得到的，而 `IToolContext::SnapAt` 的签名已经冻结，传不
    ///       进那个点。M6 的解法是把它变成引擎的一份状态
    ///       （`ICadEngine2::SetSnapReference`），签名因此一个字都不用改
    ///       （docs/architecture.md §6.3）。
    virtual void CollectSnapCandidates(const Vec3d& center, double radius, const SnapQuery& query,
                                       std::vector<SnapCandidate>& out) const = 0;
};

/// @brief 求光标此刻应该落在哪儿。
/// @param source          候选点来源；为 null 时只剩网格吸附和原始落点。
/// @param camera          用来投射射线，以及把候选点投回屏幕量距离。
/// @param plane           当前工作平面。
/// @param screenX,screenY 视口像素坐标，左上原点。
/// @param query           掩码、容差、网格间距与垂足参考点。
/// @param out             命中的类型、点和实体；没有任何吸附时 type 为
///                        Snap_None、point 为工作平面上的原始落点。
/// @return false 表示光标根本没指着工作平面（视线与之平行，或落在相机背后），
///         此时 `out` 不被写入 —— 这种情况没有任何合理的点可以报。
bool FindSnap(const ISnapSource* source, const ICamera& camera, const WorkPlane& plane,
              double screenX, double screenY, const SnapQuery& query, SnapResult& out);

} // namespace cadgeom::interact
