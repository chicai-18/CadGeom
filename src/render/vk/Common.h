// Shared Vulkan plumbing: the single include point, result translation and the
// two macros that keep the RHI from drowning in `if (r != VK_SUCCESS)`.
//
// Nothing here allocates or owns anything. Everything in render/vk stays free
// of scene and interaction concepts (docs/architecture.md §1).
#pragma once

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif

#include <vulkan/vulkan.h>

#include <cadgeom/Types.h>

#include "core/Error.h"
#include "core/Log.h"

namespace cadgeom::render::vk {

/// Short name for a VkResult, for log lines and error messages.
const char* ResultString(VkResult result);

/// Maps a VkResult onto the public result enum. Device loss stays
/// distinguishable because a host has to react to it differently from a plain
/// failure — it has to rebuild every viewport.
CgResult ToCgResult(VkResult result);

} // namespace cadgeom::render::vk

/// For functions returning CgResult: record the failure and bail.
#define CG_VK_REQUIRE(expr, what)                                                  \
    do {                                                                           \
        const VkResult cg_vk_r_ = (expr);                                          \
        if (cg_vk_r_ != VK_SUCCESS) {                                              \
            return ::cadgeom::core::SetError(                                       \
                ::cadgeom::render::vk::ToCgResult(cg_vk_r_), "%s failed: %s", (what), \
                ::cadgeom::render::vk::ResultString(cg_vk_r_));                     \
        }                                                                          \
    } while (false)

/// For teardown paths and other places where there is nothing useful to do
/// about a failure except say so.
#define CG_VK_LOG_IF_FAILED(expr, what)                                            \
    do {                                                                           \
        const VkResult cg_vk_r_ = (expr);                                          \
        if (cg_vk_r_ != VK_SUCCESS) {                                              \
            CG_WARN("%s failed: %s", (what), ::cadgeom::render::vk::ResultString(cg_vk_r_)); \
        }                                                                          \
    } while (false)
