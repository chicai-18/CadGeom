// One viewport's frame loop (docs/architecture.md §4.1).
//
// Composes into an off-screen half-float target and blits the result onto the
// swapchain. That indirection costs one full-screen copy and buys three things:
// correct sRGB output for free, a place to put MSAA and post-processing later,
// and a headless mode that is the same code path minus the presentation.
#pragma once

#include "render/FrameData.h"
#include "render/GpuScene.h"
#include "render/RenderSystem.h"
#include "render/Surface.h"
#include "render/vk/Swapchain.h"

#include <vector>

namespace cadgeom::render {

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    CgResult Initialize(RenderSystem& system, Surface& surface, const ViewportDesc& desc);
    void Shutdown();

    /// Renders and presents one frame. Ok when a frame went out, Ok-with-no-op
    /// when the window is minimised, DeviceLost when the caller has to rebuild.
    CgResult Render(const RenderView& view, const SceneSnapshot& snapshot);

    /// Marks the swapchain stale. The rebuild happens at the start of the next
    /// Render() so it lands between frames rather than inside one.
    void RequestRecreate() { needsRecreate_ = true; }

    void GetExtent(uint32_t& width, uint32_t& height) const {
        width = extent_.width;
        height = extent_.height;
    }

    void SetVsync(bool vsync);

    /// Reads back the last frame and writes it as a PNG.
    CgResult SaveScreenshot(const char* utf8Path);

private:
    struct Frame {
        VkCommandPool pool{VK_NULL_HANDLE};
        VkCommandBuffer cmd{VK_NULL_HANDLE};
        VkSemaphore imageAvailable{VK_NULL_HANDLE};
        VkFence inFlight{VK_NULL_HANDLE};
        vk::Buffer uniforms;
        VkDescriptorSet descriptor{VK_NULL_HANDLE};
    };

    CgResult CreateFrames();
    void DestroyFrames();

    CgResult RecreateTargets();
    void DestroyTargets();

    void RecordFrame(VkCommandBuffer cmd, const RenderView& view, const SceneSnapshot& snapshot,
                     VkImage presentImage);

    RenderSystem* system_{nullptr};
    Surface* surface_{nullptr};

    VkSurfaceKHR vkSurface_{VK_NULL_HANDLE};
    vk::Swapchain swapchain_;
    bool presents_{true};
    bool vsync_{true};

    vk::Image color_;
    vk::Image depth_;
    VkExtent2D extent_{0, 0};

    VkDescriptorPool descriptorPool_{VK_NULL_HANDLE};
    std::vector<Frame> frames_;
    /// One per swapchain image, not per frame in flight: the wait happens at
    /// present time, which is tied to the image, and reusing a per-frame
    /// semaphore for a different image is exactly the sort of thing that only
    /// misbehaves on someone else's driver.
    std::vector<VkSemaphore> presentSemaphores_;

    uint32_t frameSlot_{0};
    bool needsRecreate_{false};
    bool hasRenderedFrame_{false};
};

} // namespace cadgeom::render
