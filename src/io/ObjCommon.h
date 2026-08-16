/**
 * @file ObjCommon.h
 * @brief 引入 tinyobjloader 的唯一入口，把它的编译开关钉死在一处。
 *
 * `TINYOBJLOADER_USE_DOUBLE` 把 `tinyobj::real_t` 换成 double。这不是可选项：内核
 * 上下全是 double（docs/architecture.md §0.1），而文本里写着的坐标先落进 float 再
 * 转回来，一个坐标在 1e6 量级的模型就已经丢掉零点零几毫米 —— 那正是「大坐标不抖」
 * 这条约定要防的东西。
 *
 * 和 GltfCommon.h 同理，宏必须在每个看得见这个头的翻译单元里一致，否则 `attrib_t`
 * 的布局两头对不上，是一个不报错却会在运行期咬人的 ODR 违规。
 */
#pragma once

#define TINYOBJLOADER_USE_DOUBLE

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100)  // unreferenced formal parameter
#pragma warning(disable : 4127)  // conditional expression is constant
#pragma warning(disable : 4244)  // conversion, possible loss of data
#pragma warning(disable : 4267)  // size_t to smaller type
#pragma warning(disable : 4996)  // unsafe CRT function
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif

#include <tiny_obj_loader.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
