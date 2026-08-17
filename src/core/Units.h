/**
 * @file Units.h
 * @brief 显示单位的换算与格式化（docs/architecture.md §9，M6「单位系统」）。
 *
 * 引擎内部只有一种长度：模型单位。几何、包围盒、拾取容差、文件里的坐标全用它，
 * 这里做的只是把那个数变成人读的数字 —— **换显示单位不会动任何一个顶点**，这条
 * 和「params 是 SSOT」是同一个道理：显示是派生的，几何不是。
 *
 * 放在 core/ 是因为它只依赖 <cadgeom/Types.h> 和 C 运行时，上面每一层都能用：
 * HUD 要它、Measure 工具要它、ICadEngine2 把它转给宿主。
 */
#pragma once

#include <cadgeom/Types.h>

namespace cadgeom::core {

/// @brief 一个单位等于多少米。单位系统全部的物理事实就这一张表。
/// @note 英制是国际英寸（1 in = 25.4 mm 整），不是老的美制测量英寸。
double MetresPerUnit(LengthUnit unit);

/// @brief 单位后缀，UTF-8，静态存储期。
const char* UnitSuffix(LengthUnit unit);

/// @brief 模型单位 → 显示单位。
double ToDisplay(const UnitSettings& settings, double modelUnits);

/// @brief 显示单位 → 模型单位。宿主的输入框走这条路。
double ToModel(const UnitSettings& settings, double displayValue);

/// @brief 把模型单位的长度格式化成 "12.50 mm" 这样的串。
/// @param buffer   目标缓冲；容量不够也会写一个 '\0' 结尾的截断结果。
/// @param capacity buffer 的字节数，含结尾的 '\0'。
/// @return 写入的字节数，不含结尾的 '\0'。
uint32_t FormatLength(const UnitSettings& settings, double modelUnits, char* buffer,
                      uint32_t capacity);

/// @brief 把弧度格式化成 UnitSettings::angleUnit 指定的样子（"45.0°" 或 "0.785 rad"）。
uint32_t FormatAngle(const UnitSettings& settings, double radians, char* buffer,
                     uint32_t capacity);

} // namespace cadgeom::core
