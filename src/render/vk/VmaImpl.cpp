// The one translation unit that instantiates the Vulkan Memory Allocator.
// Kept alone so its 20k lines are compiled once, and so the warning
// suppression it needs is scoped to it rather than turned off project-wide.
//
// The disables are listed individually rather than as a blanket /w: a blanket
// disable on the command line makes the compiler complain about overriding /W4,
// and a future VMA version emitting something genuinely worth reading would go
// unnoticed under it.
#include "render/vk/Common.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100)  // unreferenced formal parameter
#pragma warning(disable : 4127)  // conditional expression is constant
#pragma warning(disable : 4189)  // local variable initialised but not referenced
#pragma warning(disable : 4324)  // structure padded due to alignment specifier
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
