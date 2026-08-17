// One viewport's frame loop (docs/architecture.md §4.1).
//
// Composes into an off-screen half-float target and blits the result onto the
// swapchain. That indirection costs one full-screen copy and buys three things:
// correct sRGB output for free, somewhere for MSAA and post-processing to live,
// and a headless mode that is the same code path minus the presentation.
//
// M6 cashed in the second one. With `ViewportDesc::sampleCount > 1` the frame is
// rasterised into a multisampled colour/depth pair and resolved into the same
// single-sample target the blit and the screenshot already read from, so nothing
// downstream of the resolve had to change.
#pragma once

#include "render/FrameData.h"
#include "render/GpuScene.h"
#include "render/Overlay.h"
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
    ///
    /// `overlay` is this frame's preview geometry and is consumed here: unlike
    /// the snapshot it is not cached, because it is rebuilt from the active
    /// tool's state every single frame.
    CgResult Render(const RenderView& view, const SceneSnapshot& snapshot,
                    const OverlayData& overlay);

    /// Marks the swapchain stale. The rebuild happens at the start of the next
    /// Render() so it lands between frames rather than inside one.
    void RequestRecreate() { needsRecreate_ = true; }

    void GetExtent(uint32_t& width, uint32_t& height) const {
        width = extent_.width;
        height = extent_.height;
    }

    void SetVsync(bool vsync);

    /// @brief 这个视口实际用的采样数。宿主要的那个数可能被设备的能力压低过。
    uint32_t SampleCount() const { return static_cast<uint32_t>(samples_); }

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

        /// Host-visible and rewritten every frame. Per slot rather than shared,
        /// for the same reason the uniform buffer is: the GPU may still be
        /// reading the other slot's copy.
        vk::Buffer overlayLines;
        vk::Buffer overlayPoints;
    };

    CgResult CreateFrames();
    void DestroyFrames();

    CgResult RecreateTargets();
    void DestroyTargets();

    /// Narrows this frame's overlay into `frame`'s host-visible buffers and
    /// groups it into as few instanced draws as its styles allow. Only safe once
    /// the slot's fence has been waited on.
    CgResult PrepareOverlay(Frame& frame, const RenderView& view, const OverlayData& overlay);

    void RecordFrame(VkCommandBuffer cmd, const RenderView& view, const SceneSnapshot& snapshot,
                     const Frame& frame, VkImage presentImage);

    RenderSystem* system_{nullptr};
    Surface* surface_{nullptr};

    VkSurfaceKHR vkSurface_{VK_NULL_HANDLE};
    vk::Swapchain swapchain_;
    bool presents_{true};
    bool vsync_{true};

    /// 单采样的合成目标。blit 和截图都读它；开了 MSAA 时它是 resolve 的落点，
    /// 没开时就直接画在上面 —— 所以 resolve 之后的每一步都不必关心有没有 MSAA。
    vk::Image color_;
    /// 多采样的颜色附件，只在 samples_ > 1 时存在。
    vk::Image colorMs_;
    vk::Image depth_;
    VkExtent2D extent_{0, 0};

    VkSampleCountFlagBits samples_{VK_SAMPLE_COUNT_1_BIT};
    /// 这个采样数对应的那一套 pass，视口初始化时定下来，之后不变。
    const PassSet* passes_{nullptr};

    VkDescriptorPool descriptorPool_{VK_NULL_HANDLE};
    std::vector<Frame> frames_;
    /// One per swapchain image, not per frame in flight: the wait happens at
    /// present time, which is tied to the image, and reusing a per-frame
    /// semaphore for a different image is exactly the sort of thing that only
    /// misbehaves on someone else's driver.
    std::vector<VkSemaphore> presentSemaphores_;

    /// Rebuilt every frame by PrepareOverlay and consumed immediately by
    /// RecordFrame. Members rather than locals so the allocation happens once.
    std::vector<OverlayRun> overlayLineRuns_;
    std::vector<OverlayRun> overlayPointRuns_;

    uint32_t frameSlot_{0};
    bool needsRecreate_{false};
    bool hasRenderedFrame_{false};
};

} // namespace cadgeom::render
