/**
 * @file File.h
 * @brief UTF-8 路径的文件读写与路径拆分。
 *
 * 存在的理由只有一条：Windows 上 `fopen` 收的是当前 ANSI 代码页，而引擎全线用
 * UTF-8（docs/architecture.md §2.2 第 1 条）。放在一个中文目录下的模型，narrow
 * `fopen` 根本打不开 —— 而这正是 IO 层每天都要碰的事。
 *
 * 第三方读写库（tinyobjloader / tinygltf）因此都不让它们自己碰文件：字节由这里
 * 读进来、交给它们解析，写出来的字节也由这里落盘。
 */
#pragma once

#include <cadgeom/Types.h>

#include <stdio.h>

#include <string>
#include <vector>

namespace cadgeom::core {

/// @brief 按 UTF-8 路径打开文件。
/// @param utf8Path UTF-8 路径。
/// @param mode     `fopen` 的模式串，如 "rb" / "wb"。
/// @return 失败返回 nullptr；不写错误槽 —— 失败的语境（读还是写）只有调用方知道。
FILE* OpenFile(const char* utf8Path, const char* mode);

/// @brief 把整个文件读进内存。
/// @return 打不开或读不全时为 CgResult::IoError，原因已写进错误槽。
CgResult ReadFile(const char* utf8Path, std::vector<uint8_t>& out);

/// @brief 把整个文件读成一个字符串，文本格式用。
CgResult ReadTextFile(const char* utf8Path, std::string& out);

/// @brief 整块写出，已存在则覆盖。
CgResult WriteFile(const char* utf8Path, const void* data, size_t size);

/// @brief 路径里最后一个分隔符之前的部分，末尾不带分隔符；没有目录时为空串。
std::string DirectoryOf(const char* utf8Path);

/// @brief 去掉目录和扩展名之后的文件名。
std::string StemOf(const char* utf8Path);

/// @brief 换掉扩展名。
/// @param newExtension 新扩展名，不带点。
std::string ReplaceExtension(const char* utf8Path, const char* newExtension);

/// @brief 拼一条路径。`directory` 为空时原样返回 `name`。
std::string JoinPath(const std::string& directory, const std::string& name);

} // namespace cadgeom::core
