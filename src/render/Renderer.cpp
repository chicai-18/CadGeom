#include "render/Renderer.h"

#include "core/Png.h"
#include "render/vk/Resources.h"

#include <string.h>

#include <algorithm>

namespace cadgeom::render {
namespace {

bool SameColor(const Color& a, const Color& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

void NarrowRelative(const Vec3d& world, const Vec3d& cameraOrigin, float out[3]) {
    // Subtract in double, then narrow — the whole point of camera-relative
    // rendering, and the overlay is no more exempt from it than the scene is.
    out[0] = static_cast<float>(world.x - cameraOrigin.x);
    out[1] = static_cast<float>(world.y - cameraOrigin.y);
    out[2] = static_cast<float>(world.z - cameraOrigin.z);
}

/// Grows a persistently mapped buffer to hold `bytes`, reusing it when it is
/// already big enough. Never shrinks: an overlay's size swings with what the
/// user is drawing, and giving the memory back only to ask for it again next
/// frame is the one allocation pattern a frame loop cannot afford.
CgResult EnsureUploadBuffer(vk::Context& ctx, vk::Buffer& buffer, VkDeviceSize bytes) {
    if (buffer.IsValid() && buffer.size >= bytes) {
        return CgResult::Ok;
    }
    const VkDeviceSize grown = std::max<VkDeviceSize>(bytes, buffer.size * 2);
    // Safe to destroy outright: the caller has already waited on this slot's
    // fence, so nothing on the GPU is still reading the old allocation.
    vk::DestroyBuffer(ctx, buffer);
    return vk::CreateBuffer(ctx, grown, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            vk::BufferDomain::HostUpload, buffer);
}

} // namespace

Renderer::~Renderer() {
    Shutdown();
}

CgResult Renderer::Initialize(RenderSystem& system, Surface& surface, const ViewportDesc& desc) {
    system_ = &system;
    surface_ = &surface;
    presents_ = surface.PresentsToScreen();
    vsync_ = desc.vsync;

    vk::Context& ctx = system.Context();

    if (presents_) {
        CgResult r = surface.CreateVkSurface(ctx.Instance(), vkSurface_);
        if (CgFailed(r)) {
            return r;
        }

        VkBool32 supported = VK_FALSE;
        CG_VK_REQUIRE(vkGetPhysicalDeviceSurfaceSupportKHR(ctx.PhysicalDevice(),
                                                           ctx.GraphicsFamily(), vkSurface_,
                                                           &supported),
                      "vkGetPhysicalDeviceSurfaceSupportKHR");
        if (!supported) {
            return core::SetError(CgResult::NotSupported,
                                  "%s cannot present to this surface from its graphics queue",
                                  ctx.DeviceName());
        }
    }

    CgResult r = CreateFrames();
    if (CgFailed(r)) {
        Shutdown();
        return r;
    }
    r = RecreateTargets();
    if (CgFailed(r)) {
        Shutdown();
        return r;
    }
    return CgResult::Ok;
}

CgResult Renderer::CreateFrames() {
    vk::Context& ctx = system_->Context();

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = kFramesInFlight;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kFramesInFlight;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    CG_VK_REQUIRE(vkCreateDescriptorPool(ctx.Device(), &poolInfo, nullptr, &descriptorPool_),
                  "vkCreateDescriptorPool");

    frames_.resize(kFramesInFlight);
    for (Frame& frame : frames_) {
        VkCommandPoolCreateInfo cmdPoolInfo{};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.queueFamilyIndex = ctx.GraphicsFamily();
        // No RESET_COMMAND_BUFFER flag: the whole pool is reset once per frame,
        // which is one call instead of one per buffer and lets the driver reuse
        // the block rather than fragment it.
        CG_VK_REQUIRE(vkCreateCommandPool(ctx.Device(), &cmdPoolInfo, nullptr, &frame.pool),
                      "vkCreateCommandPool");

        VkCommandBufferAllocateInfo cmdInfo{};
        cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdInfo.commandPool = frame.pool;
        cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdInfo.commandBufferCount = 1;
        CG_VK_REQUIRE(vkAllocateCommandBuffers(ctx.Device(), &cmdInfo, &frame.cmd),
                      "vkAllocateCommandBuffers");

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        CG_VK_REQUIRE(
            vkCreateSemaphore(ctx.Device(), &semaphoreInfo, nullptr, &frame.imageAvailable),
            "vkCreateSemaphore");

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        // Created signalled so the first frame does not wait on a fence that
        // nothing will ever signal.
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        CG_VK_REQUIRE(vkCreateFence(ctx.Device(), &fenceInfo, nullptr, &frame.inFlight),
                      "vkCreateFence");

        CgResult r = vk::CreateBuffer(ctx, sizeof(FrameUniforms),
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                      vk::BufferDomain::HostUpload, frame.uniforms);
        if (CgFailed(r)) {
            return r;
        }

        VkDescriptorSetLayout layout = system_->SceneSetLayout();
        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptorPool_;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &layout;
        CG_VK_REQUIRE(vkAllocateDescriptorSets(ctx.Device(), &setInfo, &frame.descriptor),
                      "vkAllocateDescriptorSets");

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = frame.uniforms.handle;
        bufferInfo.range = sizeof(FrameUniforms);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = frame.descriptor;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(ctx.Device(), 1, &write, 0, nullptr);
    }
    return CgResult::Ok;
}

void Renderer::DestroyFrames() {
    if (!system_ || !system_->Context().IsValid()) {
        return;
    }
    vk::Context& ctx = system_->Context();

    for (Frame& frame : frames_) {
        if (frame.inFlight != VK_NULL_HANDLE) {
            vkDestroyFence(ctx.Device(), frame.inFlight, nullptr);
        }
        if (frame.imageAvailable != VK_NULL_HANDLE) {
            vkDestroySemaphore(ctx.Device(), frame.imageAvailable, nullptr);
        }
        if (frame.pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(ctx.Device(), frame.pool, nullptr);
        }
        vk::DestroyBuffer(ctx, frame.uniforms);
        vk::DestroyBuffer(ctx, frame.overlayLines);
        vk::DestroyBuffer(ctx, frame.overlayPoints);
    }
    frames_.clear();

    if (descriptorPool_ != VK_NULL_HANDLE) {
        // Frees every set allocated from it, so the descriptors need no
        // separate teardown.
        vkDestroyDescriptorPool(ctx.Device(), descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
}

CgResult Renderer::RecreateTargets() {
    vk::Context& ctx = system_->Context();
    ctx.WaitIdle();

    uint32_t width = 0;
    uint32_t height = 0;
    surface_->GetExtent(width, height);

    DestroyTargets();
    needsRecreate_ = false;
    hasRenderedFrame_ = false;

    if (width == 0 || height == 0) {
        extent_ = {0, 0};
        return CgResult::Ok;  // Minimised: nothing to build, nothing to draw.
    }

    if (presents_) {
        CgResult r = swapchain_.Create(ctx, vkSurface_, width, height, vsync_);
        if (CgFailed(r)) {
            return r;
        }
        if (!swapchain_.IsValid()) {
            extent_ = {0, 0};
            return CgResult::Ok;
        }
        // The surface may have had its own opinion about the size.
        extent_ = swapchain_.Extent();

        presentSemaphores_.resize(swapchain_.ImageCount(), VK_NULL_HANDLE);
        for (VkSemaphore& semaphore : presentSemaphores_) {
            VkSemaphoreCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            CG_VK_REQUIRE(vkCreateSemaphore(ctx.Device(), &info, nullptr, &semaphore),
                          "vkCreateSemaphore (present)");
        }
    } else {
        extent_ = {width, height};
    }

    CgResult r = vk::CreateImage2D(ctx, kColorFormat, extent_,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                   VK_IMAGE_ASPECT_COLOR_BIT, color_);
    if (CgFailed(r)) {
        return r;
    }
    r = vk::CreateImage2D(ctx, kDepthFormat, extent_,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
                          depth_);
    if (CgFailed(r)) {
        return r;
    }

    CG_DEBUG("render targets %ux%u", extent_.width, extent_.height);
    return CgResult::Ok;
}

void Renderer::DestroyTargets() {
    if (!system_ || !system_->Context().IsValid()) {
        return;
    }
    vk::Context& ctx = system_->Context();

    for (VkSemaphore semaphore : presentSemaphores_) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(ctx.Device(), semaphore, nullptr);
        }
    }
    presentSemaphores_.clear();

    vk::DestroyImage(ctx, color_);
    vk::DestroyImage(ctx, depth_);
    swapchain_.Destroy(ctx);
    extent_ = {0, 0};
}

void Renderer::SetVsync(bool vsync) {
    if (vsync_ != vsync) {
        vsync_ = vsync;
        needsRecreate_ = true;
    }
}

CgResult Renderer::PrepareOverlay(Frame& frame, const RenderView& view,
                                  const OverlayData& overlay) {
    overlayLineRuns_.clear();
    overlayPointRuns_.clear();

    vk::Context& ctx = system_->Context();

    if (!overlay.lines.empty()) {
        const CgResult r = EnsureUploadBuffer(
            ctx, frame.overlayLines, overlay.lines.size() * sizeof(GpuLineInstance));
        if (CgFailed(r)) {
            return r;
        }

        auto* dst = static_cast<GpuLineInstance*>(frame.overlayLines.mapped);
        for (size_t i = 0; i < overlay.lines.size(); ++i) {
            const OverlayLine& line = overlay.lines[i];
            GpuLineInstance instance{};
            NarrowRelative(line.a, view.cameraOrigin, instance.a);
            NarrowRelative(line.b, view.cameraOrigin, instance.b);
            instance.arcA = static_cast<float>(line.arcA);
            instance.arcB = static_cast<float>(line.arcB);
            dst[i] = instance;

            // Everything a run carries lives in push constants, so a change in
            // any of it starts a new draw. A previewed circle is one run.
            const bool continues = !overlayLineRuns_.empty() &&
                                   SameColor(overlayLineRuns_.back().color, line.color) &&
                                   overlayLineRuns_.back().size == line.width &&
                                   overlayLineRuns_.back().style == line.style;
            if (continues) {
                ++overlayLineRuns_.back().instanceCount;
            } else {
                OverlayRun run{};
                run.firstInstance = static_cast<uint32_t>(i);
                run.instanceCount = 1;
                run.color = line.color;
                run.size = line.width;
                run.style = line.style;
                overlayLineRuns_.push_back(run);
            }
        }
        const CgResult flushed = vk::FlushBuffer(ctx, frame.overlayLines);
        if (CgFailed(flushed)) {
            return flushed;
        }
    }

    if (!overlay.points.empty()) {
        const CgResult r = EnsureUploadBuffer(
            ctx, frame.overlayPoints, overlay.points.size() * sizeof(GpuPointInstance));
        if (CgFailed(r)) {
            return r;
        }

        auto* dst = static_cast<GpuPointInstance*>(frame.overlayPoints.mapped);
        for (size_t i = 0; i < overlay.points.size(); ++i) {
            const OverlayPoint& point = overlay.points[i];
            NarrowRelative(point.position, view.cameraOrigin, dst[i].position);

            const bool continues = !overlayPointRuns_.empty() &&
                                   SameColor(overlayPointRuns_.back().color, point.color) &&
                                   overlayPointRuns_.back().size == point.size;
            if (continues) {
                ++overlayPointRuns_.back().instanceCount;
            } else {
                OverlayRun run{};
                run.firstInstance = static_cast<uint32_t>(i);
                run.instanceCount = 1;
                run.color = point.color;
                run.size = point.size;
                overlayPointRuns_.push_back(run);
            }
        }
        const CgResult flushed = vk::FlushBuffer(ctx, frame.overlayPoints);
        if (CgFailed(flushed)) {
            return flushed;
        }
    }

    return CgResult::Ok;
}

void Renderer::RecordFrame(VkCommandBuffer cmd, const RenderView& view,
                           const SceneSnapshot& snapshot, const Frame& frame,
                           VkImage presentImage) {
    // UNDEFINED as the old layout on purpose: the previous contents are being
    // cleared anyway, and asking the driver to preserve them would be a lie.
    vk::TransitionImage(cmd, color_.handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vk::TransitionImage(cmd, depth_.handle, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = color_.view;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{view.background.r, view.background.g, view.background.b,
                                         view.background.a}};

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depth_.view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // Nothing reads depth after the frame; M3's picking uses the CPU-side BVH,
    // not a depth buffer.
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = extent_;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &colorAttachment;
    rendering.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &rendering);

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent_;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const VkPipelineLayout layout = system_->PipelineLayout();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &frame.descriptor,
                            0, nullptr);

    // Grid first: it is the only opaque-ish surface that blends, so drawing it
    // before the solids lets the depth test resolve the rest normally. Then
    // solids, then wires and points on top of them, then the overlay, which is
    // not depth tested at all.
    if (view.showGrid) {
        system_->Grid().Record(cmd);
    }
    system_->Mesh().Record(cmd, layout, system_->Geometry(), snapshot, view);
    system_->Lines().Record(cmd, layout, system_->Geometry(), snapshot, view);
    system_->Points().Record(cmd, layout, system_->Geometry(), snapshot, view);

    system_->Lines().RecordOverlay(cmd, layout, frame.overlayLines.handle, overlayLineRuns_);
    system_->Points().RecordOverlay(cmd, layout, frame.overlayPoints.handle, overlayPointRuns_);

    vkCmdEndRendering(cmd);

    vk::TransitionImage(cmd, color_.handle, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    if (presentImage != VK_NULL_HANDLE) {
        vk::TransitionImage(cmd, presentImage, VK_IMAGE_ASPECT_COLOR_BIT,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        // The blit is where linear light becomes sRGB: the swapchain format is
        // sRGB and the transfer applies the encode.
        vk::BlitImage(cmd, color_.handle, extent_, presentImage, swapchain_.Extent());
        vk::TransitionImage(cmd, presentImage, VK_IMAGE_ASPECT_COLOR_BIT,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
}

CgResult Renderer::Render(const RenderView& view, const SceneSnapshot& snapshot,
                          const OverlayData& overlay) {
    if (!system_ || !system_->Context().IsValid()) {
        return core::SetError(CgResult::InvalidState, "Render() on an uninitialised viewport");
    }

    if (needsRecreate_ || (presents_ && !swapchain_.IsValid()) || !color_.IsValid()) {
        const CgResult r = RecreateTargets();
        if (CgFailed(r)) {
            return r;
        }
    }
    if (extent_.width == 0 || extent_.height == 0) {
        return CgResult::Ok;  // Minimised. Not an error, just nothing to do.
    }

    vk::Context& ctx = system_->Context();
    Frame& frame = frames_[frameSlot_];

    CG_VK_REQUIRE(vkWaitForFences(ctx.Device(), 1, &frame.inFlight, VK_TRUE, UINT64_MAX),
                  "vkWaitForFences");

    uint32_t imageIndex = 0;
    if (presents_) {
        const VkResult acquired =
            vkAcquireNextImageKHR(ctx.Device(), swapchain_.Handle(), UINT64_MAX,
                                  frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            // The window changed size between frames. Rebuild and let the host
            // ask for the next frame; skipping one is invisible.
            needsRecreate_ = true;
            return CgResult::Ok;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
            return core::SetError(vk::ToCgResult(acquired), "vkAcquireNextImageKHR failed: %s",
                                  vk::ResultString(acquired));
        }
        if (acquired == VK_SUBOPTIMAL_KHR) {
            needsRecreate_ = true;  // Present this frame anyway, rebuild after.
        }
    }

    FrameUniforms uniforms{};
    FillFrameUniforms(view, extent_.width, extent_.height, uniforms);
    memcpy(frame.uniforms.mapped, &uniforms, sizeof(uniforms));
    vk::FlushBuffer(ctx, frame.uniforms);

    // After the fence wait above, so rewriting this slot's overlay buffers
    // cannot race a submission still reading them.
    const CgResult prepared = PrepareOverlay(frame, view, overlay);
    if (CgFailed(prepared)) {
        return prepared;
    }

    CG_VK_REQUIRE(vkResetFences(ctx.Device(), 1, &frame.inFlight), "vkResetFences");
    CG_VK_REQUIRE(vkResetCommandPool(ctx.Device(), frame.pool, 0), "vkResetCommandPool");

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CG_VK_REQUIRE(vkBeginCommandBuffer(frame.cmd, &begin), "vkBeginCommandBuffer");

    RecordFrame(frame.cmd, view, snapshot, frame,
                presents_ ? swapchain_.ImageAt(imageIndex) : VK_NULL_HANDLE);

    CG_VK_REQUIRE(vkEndCommandBuffer(frame.cmd), "vkEndCommandBuffer");

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = frame.cmd;

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = frame.imageAvailable;
    // The acquired image is only touched by the blit at the very end, so there
    // is no reason to stall the whole pipeline waiting for it.
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;

    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = presents_ ? presentSemaphores_[imageIndex] : VK_NULL_HANDLE;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    if (presents_) {
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &waitInfo;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signalInfo;
    }

    CG_VK_REQUIRE(vkQueueSubmit2(ctx.GraphicsQueue(), 1, &submit, frame.inFlight),
                  "vkQueueSubmit2");

    if (presents_) {
        VkSwapchainKHR swapchain = swapchain_.Handle();
        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &presentSemaphores_[imageIndex];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &imageIndex;

        const VkResult presented = vkQueuePresentKHR(ctx.GraphicsQueue(), &present);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
            needsRecreate_ = true;
        } else if (presented != VK_SUCCESS) {
            return core::SetError(vk::ToCgResult(presented), "vkQueuePresentKHR failed: %s",
                                  vk::ResultString(presented));
        }
    }

    frameSlot_ = (frameSlot_ + 1) % kFramesInFlight;
    hasRenderedFrame_ = true;
    return CgResult::Ok;
}

CgResult Renderer::SaveScreenshot(const char* utf8Path) {
    if (!utf8Path || !*utf8Path) {
        return core::SetError(CgResult::InvalidArgument, "SaveScreenshot: empty path");
    }
    if (!hasRenderedFrame_ || !color_.IsValid()) {
        return core::SetError(CgResult::InvalidState,
                              "SaveScreenshot: no frame has been rendered yet");
    }

    vk::Context& ctx = system_->Context();
    CgResult r = ctx.WaitIdle();
    if (CgFailed(r)) {
        return r;
    }

    // Via an 8-bit sRGB image rather than straight out of the float target: the
    // blit performs both the narrowing and the sRGB encode, so the PNG matches
    // what was on screen instead of being a linear-light image that looks dark.
    vk::Image staging{};
    r = vk::CreateImage2D(ctx, VK_FORMAT_R8G8B8A8_SRGB, extent_,
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                          VK_IMAGE_ASPECT_COLOR_BIT, staging);
    if (CgFailed(r)) {
        return r;
    }

    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(extent_.width) * extent_.height * 4;
    vk::Buffer readback{};
    r = vk::CreateBuffer(ctx, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         vk::BufferDomain::HostReadback, readback);
    if (CgFailed(r)) {
        vk::DestroyImage(ctx, staging);
        return r;
    }

    const VkImage source = color_.handle;
    const VkExtent2D extent = extent_;
    r = ctx.ImmediateSubmit([&](VkCommandBuffer cmd) {
        vk::TransitionImage(cmd, staging.handle, VK_IMAGE_ASPECT_COLOR_BIT,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vk::BlitImage(cmd, source, extent, staging.handle, extent);
        vk::TransitionImage(cmd, staging.handle, VK_IMAGE_ASPECT_COLOR_BIT,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(cmd, staging.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback.handle, 1, &copy);
    });

    if (CgSucceeded(r)) {
        r = vk::InvalidateBuffer(ctx, readback);
    }
    if (CgSucceeded(r)) {
        r = core::WritePng(utf8Path, extent_.width, extent_.height,
                           static_cast<const uint8_t*>(readback.mapped),
                           static_cast<size_t>(extent_.width) * 4);
    }

    vk::DestroyBuffer(ctx, readback);
    vk::DestroyImage(ctx, staging);

    // The colour target went back to TRANSFER_SRC at the end of the last frame
    // and the blit above left it there, so the next frame's UNDEFINED
    // transition is still correct.
    if (CgSucceeded(r)) {
        CG_INFO("screenshot written to %s (%ux%u)", utf8Path, extent_.width, extent_.height);
    }
    return r;
}

void Renderer::Shutdown() {
    if (!system_ || !system_->Context().IsValid()) {
        system_ = nullptr;
        surface_ = nullptr;
        return;
    }

    vk::Context& ctx = system_->Context();
    ctx.WaitIdle();

    DestroyTargets();
    DestroyFrames();

    if (vkSurface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(ctx.Instance(), vkSurface_, nullptr);
        vkSurface_ = VK_NULL_HANDLE;
    }

    system_ = nullptr;
    surface_ = nullptr;
    hasRenderedFrame_ = false;
}

} // namespace cadgeom::render
