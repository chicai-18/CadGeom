#include "render/vk/Swapchain.h"

#include <algorithm>

namespace cadgeom::render::vk {
namespace {

/// An sRGB swapchain is what makes the final blit apply the encode for us: the
/// renderer works in linear light in a 16-bit float target, and the transfer
/// out of it converts. Picking a UNORM format instead would silently ship
/// linear values to the display and everything would look washed out.
VkSurfaceFormatKHR ChooseFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const VkSurfaceFormatKHR& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    CG_WARN("no sRGB swapchain format available; colours will be slightly off");
    return formats.front();
}

VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync) {
    if (vsync) {
        return VK_PRESENT_MODE_FIFO_KHR;  // Always supported.
    }
    const auto has = [&modes](VkPresentModeKHR m) {
        return std::find(modes.begin(), modes.end(), m) != modes.end();
    };
    if (has(VK_PRESENT_MODE_MAILBOX_KHR)) {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

} // namespace

CgResult Swapchain::Create(Context& ctx, VkSurfaceKHR surface, uint32_t width, uint32_t height,
                           bool vsync) {
    VkSurfaceCapabilitiesKHR caps{};
    CG_VK_REQUIRE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.PhysicalDevice(), surface, &caps),
                  "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    VkExtent2D extent{width, height};
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width = std::clamp(extent.width, caps.minImageExtent.width,
                                  caps.maxImageExtent.width);
        extent.height = std::clamp(extent.height, caps.minImageExtent.height,
                                   caps.maxImageExtent.height);
    }

    if (extent.width == 0 || extent.height == 0) {
        // Minimised. Tearing the chain down and reporting success keeps the
        // caller's frame loop free of platform special cases.
        Destroy(ctx);
        return CgResult::Ok;
    }

    // The frame is composed off-screen and blitted in, so the chain has to
    // accept a transfer. Every driver supports it, but a clear error beats a
    // validation message if one ever does not.
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
        return core::SetError(CgResult::NotSupported,
                              "this surface cannot be a transfer destination, so the offscreen "
                              "frame cannot be blitted to it");
    }

    uint32_t formatCount = 0;
    CG_VK_REQUIRE(
        vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.PhysicalDevice(), surface, &formatCount, nullptr),
        "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (formatCount == 0) {
        return core::SetError(CgResult::NotSupported, "the surface exposes no formats");
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    CG_VK_REQUIRE(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.PhysicalDevice(), surface, &formatCount,
                                                       formats.data()),
                  "vkGetPhysicalDeviceSurfaceFormatsKHR");

    uint32_t modeCount = 0;
    CG_VK_REQUIRE(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.PhysicalDevice(), surface,
                                                            &modeCount, nullptr),
                  "vkGetPhysicalDeviceSurfacePresentModesKHR");
    std::vector<VkPresentModeKHR> modes(modeCount);
    if (modeCount > 0) {
        CG_VK_REQUIRE(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.PhysicalDevice(), surface,
                                                                &modeCount, modes.data()),
                      "vkGetPhysicalDeviceSurfacePresentModesKHR");
    }

    const VkSurfaceFormatKHR surfaceFormat = ChooseFormat(formats);

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) {
        imageCount = std::min(imageCount, caps.maxImageCount);
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface;
    info.minImageCount = imageCount;
    info.imageFormat = surfaceFormat.format;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = ChoosePresentMode(modes, vsync);
    info.clipped = VK_TRUE;
    info.oldSwapchain = handle_;

    VkSwapchainKHR created = VK_NULL_HANDLE;
    const VkResult result = vkCreateSwapchainKHR(ctx.Device(), &info, nullptr, &created);

    // The old chain is retired either way: on success it has been handed over,
    // on failure it is no longer usable.
    Destroy(ctx);
    if (result != VK_SUCCESS) {
        return core::SetError(ToCgResult(result), "vkCreateSwapchainKHR failed: %s",
                              ResultString(result));
    }

    handle_ = created;
    format_ = surfaceFormat.format;
    extent_ = extent;

    uint32_t actualCount = 0;
    CG_VK_REQUIRE(vkGetSwapchainImagesKHR(ctx.Device(), handle_, &actualCount, nullptr),
                  "vkGetSwapchainImagesKHR");
    images_.resize(actualCount);
    CG_VK_REQUIRE(vkGetSwapchainImagesKHR(ctx.Device(), handle_, &actualCount, images_.data()),
                  "vkGetSwapchainImagesKHR");

    CG_DEBUG("swapchain %ux%u, %u images, %s", extent_.width, extent_.height, actualCount,
             vsync ? "vsync" : "no vsync");
    return CgResult::Ok;
}

void Swapchain::Destroy(Context& ctx) {
    if (handle_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(ctx.Device(), handle_, nullptr);
        handle_ = VK_NULL_HANDLE;
    }
    // The images belong to the chain; there is nothing of ours to free.
    images_.clear();
    extent_ = {0, 0};
}

} // namespace cadgeom::render::vk
