// VMA-backed buffers and images, plus the layout-transition helper every pass
// ends up needing. Thin on purpose: these are handles with a destructor's worth
// of bookkeeping, not a resource system.
#pragma once

#include "render/vk/Common.h"
#include "render/vk/Context.h"

struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

namespace cadgeom::render::vk {

/// Where a buffer lives, expressed as intent rather than as heap flags.
enum class BufferDomain {
    /// GPU-only. Fast to read from a shader, has to be filled through a staging
    /// copy.
    Device,
    /// Host-visible and persistently mapped. For per-frame data the CPU writes
    /// every frame and the GPU reads once.
    HostUpload,
    /// Host-visible readback target for copies coming back from the GPU.
    HostReadback,
};

struct Buffer {
    VkBuffer handle{VK_NULL_HANDLE};
    VmaAllocation allocation{nullptr};
    VkDeviceSize size{0};
    /// Non-null for HostUpload / HostReadback buffers.
    void* mapped{nullptr};

    bool IsValid() const { return handle != VK_NULL_HANDLE; }
};

struct Image {
    VkImage handle{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VmaAllocation allocation{nullptr};
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkExtent2D extent{0, 0};

    bool IsValid() const { return handle != VK_NULL_HANDLE; }
};

CgResult CreateBuffer(Context& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                      BufferDomain domain, Buffer& out);
void DestroyBuffer(Context& ctx, Buffer& buffer);

CgResult CreateImage2D(Context& ctx, VkFormat format, VkExtent2D extent, VkImageUsageFlags usage,
                       VkImageAspectFlags aspect, Image& out);
void DestroyImage(Context& ctx, Image& image);

/// Uploads `bytes` into a device-local buffer through a staging copy.
CgResult UploadToDeviceBuffer(Context& ctx, const void* data, VkDeviceSize bytes,
                              VkBufferUsageFlags usage, Buffer& out);

/// Publishes CPU writes to a mapped upload buffer. A no-op on the coherent
/// memory VMA usually hands back, but the guarantee is not free-standing.
CgResult FlushBuffer(Context& ctx, const Buffer& buffer);

/// The other direction: makes GPU writes visible to the CPU for a readback.
CgResult InvalidateBuffer(Context& ctx, const Buffer& buffer);

/// Pipeline barrier for a layout change, with stage and access masks derived
/// from the layouts. Conservative rather than minimal: correctness first, and
/// M1 has nowhere near enough traffic for the difference to show.
void TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout oldLayout, VkImageLayout newLayout);

/// Blit with a linear filter, both images assumed already in the right layout.
void BlitImage(VkCommandBuffer cmd, VkImage src, VkExtent2D srcExtent, VkImage dst,
               VkExtent2D dstExtent);

} // namespace cadgeom::render::vk
