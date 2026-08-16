/**
 * @file TransformTools.h
 * @brief 变换工具：Move 与 Rotate（docs/architecture.md §6.2、§6.5）。
 *
 * 两个都是同一套流程：选中对象 → Gizmo 出现在选择集包围盒中心 → 抓住一个手柄
 * 拖 → 松手才产生一个 Command。拖拽过程直接改实体的局部变换，不进撤销栈，所以
 * 一次拖拽在历史里是一步而不是几百步。
 *
 * 没抓到手柄的那一次点击会退化成一次普通拾取，用户因此可以在 Move 里换选对象，
 * 不必先切回 Select 再切回来。
 */
#pragma once

namespace cadgeom {
class IToolManager;
}

namespace cadgeom::interact {

struct ToolSettings;

/// @brief 创建并注册 Move 与 Rotate。管理器接管它们的所有权；`settings` 必须比
///        管理器活得久 —— 它就是管理器自己的成员。
void RegisterTransformTools(IToolManager& manager, const ToolSettings& settings);

} // namespace cadgeom::interact
