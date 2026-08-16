// tinyobjloader 唯一的实现翻译单元。
//
// 单独放一个文件的理由和 VmaImpl.cpp 一样：几千行第三方代码只编译一次，它需要的
// 警告抑制也就锁在这一处，不必把整个工程的 /W4 关掉。开关和声明侧共用
// io/ObjCommon.h，两边看到的必须是同一个 tinyobjloader。

#define TINYOBJLOADER_IMPLEMENTATION
#include "io/ObjCommon.h"
