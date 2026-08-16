/**
 * @file GltfCommon.h
 * @brief 引入 tinygltf 的唯一入口，把它的编译开关钉死在一处。
 *
 * 必须走这里，不能各个 .cpp 各自 `#include <tiny_gltf.h>`：那几个 `TINYGLTF_NO_*`
 * 宏会改变 `tinygltf::TinyGLTF` 成员的默认初值，两个翻译单元用不同的宏看同一个类，
 * 是一个不会报错但会在运行期咬人的 ODR 违规。
 *
 * 图片相关的三样全关：CadGeom 的材质只有一个颜色，没有贴图可读也没有贴图可写，
 * 而 stb_image 会白搭进去两万行没人调用的代码。
 */
#pragma once

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE

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

#include <tiny_gltf.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
