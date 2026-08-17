// 拉伸工具（docs/architecture.md §6.2）。
//
//   选中闭合轮廓 → 沿轮廓法线拖拽高度 → 确定 → 一步可撤销的拉伸
//
// 和几个绘制工具一样，点-点和按-拖-放两种习惯都认：区别在于按下和松开之间指针
// 有没有移出几个像素。
#pragma once

namespace cadgeom {
class IToolManager;
}

namespace cadgeom::interact {

struct ToolSettings;

/// 创建并注册 Extrude 工具。manager 接管它的所有权；`settings` 必须活得比
/// manager 久 —— 它本来就是 manager 自己的成员。
void RegisterExtrudeTool(IToolManager& manager, ToolSettings& settings);

} // namespace cadgeom::interact
