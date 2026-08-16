// Presentation chain for one surface. Recreated wholesale on resize — there is
// no incremental path worth the complexity at this size.
#pragma once

#include "render/vk/Common.h"
#include "render/vk/Context.h"

#include <vector>

namespace cadgeom::render::vk {

class Swapchain {
public:
    Swapchain() = default;
    ~Swapchain() = default;

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    /// Creates or replaces the chain. Passing a zero extent is not an error: it
    /// destroys the chain and reports Ok, which is how a minimised window is
    /// handled without special cases at the call site.
    CgResult Create(Context& ctx, VkSurfaceKHR surface, uint32_t width, uint32_t height,
                    bool vsync);
    void Destroy(Context& ctx);

    bool IsValid() const { return handle_ != VK_NULL_HANDLE; }

    VkSwapchainKHR Handle() const { return handle_; }
    VkFormat Format() const { return format_; }
    VkExtent2D Extent() const { return extent_; }
    uint32_t ImageCount() const { return static_cast<uint32_t>(images_.size()); }
    VkImage ImageAt(uint32_t index) const { return images_[index]; }

private:
    VkSwapchainKHR handle_{VK_NULL_HANDLE};
    VkFormat format_{VK_FORMAT_UNDEFINED};
    VkExtent2D extent_{0, 0};
    std::vector<VkImage> images_;
};

} // namespace cadgeom::render::vk
