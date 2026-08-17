/**
 * @file TransformTools.h
 * @brief 变换工具：Move、Rotate 与 Scale（docs/architecture.md §6.2、§6.5）。
 *
 * 三个都是同一套流程：选中对象 → Gizmo 出现在选择集包围盒中心 → 抓住一个手柄
 * 拖 → 松手才产生一个 Command。拖拽过程直接改实体的局部变换，不进撤销栈，所以
 * 一次拖拽在历史里是一步而不是几百步。
 *
 * 没抓到手柄的那一次点击会退化成一次普通拾取，用户因此可以在 Move 里换选对象，
 * 不必先切回 Select 再切回来。
 *
 * @note Scale 绕 Gizmo 原点、沿**世界**轴缩放。实体自己转过身的话，这在数学上会
 *       产生错切，而 TRS 表示不了错切 —— 那种情况下工具会说明并让实体留在原地，
 *       而不是给一个看着合理其实是错的姿态。
 */
#pragma once

namespace cadgeom {
class IToolManager;
}

namespace cadgeom::interact {

struct ToolSettings;

/// @brief 创建并注册 Move、Rotate 与 Scale。管理器接管它们的所有权；`settings`
///        必须比管理器活得久 —— 它就是管理器自己的成员。
void RegisterTransformTools(IToolManager& manager, ToolSettings& settings);

} // namespace cadgeom::interact
