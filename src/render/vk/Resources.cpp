#include "render/vk/Resources.h"

#include <vk_mem_alloc.h>

#include <string.h>

namespace cadgeom::render::vk {
namespace {

struct LayoutSync {
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
};

LayoutSync SyncForLayout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return {VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0};
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT};
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT};
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT};
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            // Presentation is synchronised by the present semaphore, not by the
            // barrier, so nothing to wait on here.
            return {VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0};
        default:
            return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    }
}

VmaAllocationCreateInfo AllocationInfoFor(BufferDomain domain) {
    VmaAllocationCreateInfo info{};
    switch (domain) {
        case BufferDomain::Device:
            info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;
        case BufferDomain::HostUpload:
            info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case BufferDomain::HostReadback:
            info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
    }
    return info;
}

} // namespace

CgResult CreateBuffer(Context& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                      BufferDomain domain, Buffer& out) {
    out = Buffer{};
    if (size == 0) {
        return core::SetError(CgResult::InvalidArgument, "CreateBuffer: zero-sized buffer");
    }

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    const VmaAllocationCreateInfo allocInfo = AllocationInfoFor(domain);
    VmaAllocationInfo result{};
    CG_VK_REQUIRE(vmaCreateBuffer(ctx.Allocator(), &info, &allocInfo, &out.handle, &out.allocation,
                                  &result),
                  "vmaCreateBuffer");

    out.size = size;
    out.mapped = result.pMappedData;
    return CgResult::Ok;
}

void DestroyBuffer(Context& ctx, Buffer& buffer) {
    if (buffer.handle != VK_NULL_HANDLE) {
        vmaDestroyBuffer(ctx.Allocator(), buffer.handle, buffer.allocation);
    }
    buffer = Buffer{};
}

CgResult CreateImage2D(Context& ctx, VkFormat format, VkExtent2D extent, VkImageUsageFlags usage,
                       VkImageAspectFlags aspect, Image& out, VkSampleCountFlagBits samples) {
    out = Image{};

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {extent.width, extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = samples != 0 ? samples : VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    CG_VK_REQUIRE(vmaCreateImage(ctx.Allocator(), &info, &allocInfo, &out.handle, &out.allocation,
                                 nullptr),
                  "vmaCreateImage");

    // A pure transfer image — the screenshot staging target, say — cannot have
    // a view at all, and asking for one is an error rather than a no-op.
    constexpr VkImageUsageFlags kViewableUsage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    if ((usage & kViewableUsage) == 0) {
        out.format = format;
        out.extent = extent;
        return CgResult::Ok;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = out.handle;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    const VkResult viewResult = vkCreateImageView(ctx.Device(), &viewInfo, nullptr, &out.view);
    if (viewResult != VK_SUCCESS) {
        vmaDestroyImage(ctx.Allocator(), out.handle, out.allocation);
        out = Image{};
        return core::SetError(ToCgResult(viewResult), "vkCreateImageView failed: %s",
                              ResultString(viewResult));
    }

    out.format = format;
    out.extent = extent;
    return CgResult::Ok;
}

void DestroyImage(Context& ctx, Image& image) {
    if (image.view != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx.Device(), image.view, nullptr);
    }
    if (image.handle != VK_NULL_HANDLE) {
        vmaDestroyImage(ctx.Allocator(), image.handle, image.allocation);
    }
    image = Image{};
}

CgResult UploadToDeviceBuffer(Context& ctx, const void* data, VkDeviceSize bytes,
                              VkBufferUsageFlags usage, Buffer& out) {
    Buffer staging{};
    CgResult r = CreateBuffer(ctx, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              BufferDomain::HostUpload, staging);
    if (CgFailed(r)) {
        return r;
    }
    if (!staging.mapped) {
        DestroyBuffer(ctx, staging);
        return core::SetError(CgResult::RenderError, "staging buffer came back unmapped");
    }
    memcpy(staging.mapped, data, static_cast<size_t>(bytes));

    r = CreateBuffer(ctx, bytes, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferDomain::Device,
                     out);
    if (CgFailed(r)) {
        DestroyBuffer(ctx, staging);
        return r;
    }

    const VkBuffer src = staging.handle;
    const VkBuffer dst = out.handle;
    r = ctx.ImmediateSubmit([src, dst, bytes](VkCommandBuffer cmd) {
        VkBufferCopy copy{};
        copy.size = bytes;
        vkCmdCopyBuffer(cmd, src, dst, 1, &copy);
    });

    DestroyBuffer(ctx, staging);
    if (CgFailed(r)) {
        DestroyBuffer(ctx, out);
    }
    return r;
}

CgResult FlushBuffer(Context& ctx, const Buffer& buffer) {
    if (!buffer.IsValid()) {
        return CgResult::Ok;
    }
    CG_VK_REQUIRE(vmaFlushAllocation(ctx.Allocator(), buffer.allocation, 0, VK_WHOLE_SIZE),
                  "vmaFlushAllocation");
    return CgResult::Ok;
}

CgResult InvalidateBuffer(Context& ctx, const Buffer& buffer) {
    if (!buffer.IsValid()) {
        return CgResult::Ok;
    }
    CG_VK_REQUIRE(vmaInvalidateAllocation(ctx.Allocator(), buffer.allocation, 0, VK_WHOLE_SIZE),
                  "vmaInvalidateAllocation");
    return CgResult::Ok;
}

void TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout oldLayout, VkImageLayout newLayout) {
    const LayoutSync before = SyncForLayout(oldLayout);
    const LayoutSync after = SyncForLayout(newLayout);

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = before.stage;
    barrier.srcAccessMask = before.access;
    barrier.dstStageMask = after.stage;
    barrier.dstAccessMask = after.access;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &dep);
}

void BlitImage(VkCommandBuffer cmd, VkImage src, VkExtent2D srcExtent, VkImage dst,
               VkExtent2D dstExtent) {
    VkImageBlit2 region{};
    region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.srcOffsets[1] = {static_cast<int32_t>(srcExtent.width),
                            static_cast<int32_t>(srcExtent.height), 1};
    region.dstOffsets[1] = {static_cast<int32_t>(dstExtent.width),
                            static_cast<int32_t>(dstExtent.height), 1};

    VkBlitImageInfo2 info{};
    info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    info.srcImage = src;
    info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    info.dstImage = dst;
    info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    info.regionCount = 1;
    info.pRegions = &region;
    info.filter = VK_FILTER_LINEAR;

    vkCmdBlitImage2(cmd, &info);
}

} // namespace cadgeom::render::vk
