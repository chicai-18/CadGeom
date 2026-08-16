/**
 * @file Registry.h
 * @brief 内置格式处理器的工厂（docs/architecture.md §7）。
 *
 * 引擎自带的三种格式 —— OBJ、glTF(.gltf)、glTF 二进制(.glb) —— 走的是宿主注册自己
 * 格式时走的同一条路：`IIoRegistry::Register`。它们没有任何特权，宿主可以用自己的
 * 实现把其中任何一个覆盖掉。
 */
#pragma once

#include <cadgeom/IImportExport.h>

namespace cadgeom::io {

class ISceneSource;
class ISceneSink;

/// @brief 建出内置处理器并注册进 `registry`。
/// @param registry 引擎自己的注册表。
/// @param source   导出时读场景用。
/// @param sink     导入时写场景用。
/// @return 任何一项注册失败都直接返回它的结果；成功注册的那些留在表里。
/// @note 注册表接手所有权，销毁时逐个 `Release()`。
CgResult RegisterBuiltinFormats(IIoRegistry& registry, ISceneSource& source, ISceneSink& sink);

/// @brief Wavefront OBJ 读取器（tinyobjloader）。
IImporter* CreateObjImporter(ISceneSink& sink);
/// @brief Wavefront OBJ 写出器（自写，含 .mtl）。
IExporter* CreateObjExporter(ISceneSource& source);

/// @brief glTF 2.0 读取器（tinygltf，.gltf 与 .glb 同一个）。
IImporter* CreateGltfImporter(ISceneSink& sink);
/// @brief glTF 2.0 写出器。
/// @param binary true 写 .glb（单一二进制容器），false 写 .gltf（JSON + 内嵌 buffer）。
IExporter* CreateGltfExporter(ISceneSource& source, bool binary);

} // namespace cadgeom::io
