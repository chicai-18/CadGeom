/**
 * @file MeasureTool.h
 * @brief 两点测距（docs/architecture.md §9，M6「带屏幕读数的 Measure」）。
 *
 * 它是唯一一个**什么都不改**的工具：不建几何、不推命令、不碰选择集。点两个点，
 * 屏幕上出现一条标注线和一串数字，仅此而已。所以它也是叠加层文字第一个真正的
 * 用户 —— 一个量不出数来的测量工具没有任何意义。
 *
 * 两个点都走吸附，而且第一个点落下之后会成为垂足吸附的参考点，「量这个角到那条
 * 边的垂直距离」因此是点两下就能做到的事。
 *
 * 结果记在 `ToolSettings` 里，宿主通过 `ICadEngine2::GetMeasurement` 取。
 */
#pragma once

namespace cadgeom {
class IToolManager;
}

namespace cadgeom::interact {

struct ToolSettings;

/// @brief 创建并注册 Measure。管理器接管所有权。
void RegisterMeasureTool(IToolManager& manager, ToolSettings& settings);

} // namespace cadgeom::interact
