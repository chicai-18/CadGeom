/**
 * @file TextStroke.h
 * @brief 笔画字体：把一串文本变成折线（docs/architecture.md §9，M6「叠加层文字」）。
 *
 * `IOverlayBuilder::AddText` 一直是空的，因为「画字」听起来就意味着字形图集、
 * 采样器、一条带纹理的管线和一套描述符——为了状态栏上的一行提示。
 *
 * 但线条渲染这件事这个引擎已经做完了：一条线段在顶点着色器里撑成屏幕空间的四边
 * 形，宽度按像素给，永远正对观察者（§4.2）。**笔画字体正好是一堆线段**，所以文字
 * 走的是已经在跑的那条路，一个新管线、一张新纹理都不需要。
 *
 * 这也不是将就：CAD 图纸上的字本来就是笔画字体（AutoCAD 的 txt.shx 就是），因为
 * 它无论放多大都不会糊，而且和图线用同一支笔。
 *
 * 字形定义在一个 4 宽 6 高的整数格子里，基线 y = 0、大写高度 y = 6，输出时按大写
 * 高度归一化成 em（cap height = 1.0）。
 *
 * @note 只有 ASCII，而且**小写按大写画** —— 图纸上的字历来如此，也省掉半套字形。
 *       认不出来的字符不画，不画方框：屏幕上多一个占位符比少一个字母更碍事。
 */
#pragma once

#include <cadgeom/Types.h>

#include "core/Math.h"

#include <vector>

namespace cadgeom::interact {

/// @brief `BuildStrokeText` 输出里的一条折线，索引进同一次调用的点表。
struct StrokeRun {
    uint32_t first{0};
    uint32_t count{0};
};

/// @brief 把 `utf8Text` 拆成笔画。
/// @param points  追加折线顶点，单位是 em：x 从 0 向右，y 从基线向上，大写高度 1.0。
/// @param runs    追加每条折线在 `points` 里的区间。
/// @note 追加而不是覆盖，所以一次可以摆好几行；两个容器都不会被清空。
void BuildStrokeText(const char* utf8Text, std::vector<Vec2d>& points,
                     std::vector<StrokeRun>& runs);

/// @brief 这串文本占多宽，em。用来把文字对齐到右边或居中。
double StrokeTextWidth(const char* utf8Text);

/// @brief 一行到下一行的基线间距，em。
inline constexpr double kStrokeLineHeight = 1.55;

} // namespace cadgeom::interact
