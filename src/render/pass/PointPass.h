// Point primitives (docs/architecture.md §4.2).
//
// An instanced billboard quad per point, sized in pixels and cut to a disc in
// the fragment shader. gl_PointSize would be shorter but its supported range is
// implementation-defined, it is always a square, and it cannot be anti-aliased.
#pragma once

#include "render/FrameData.h"
#include "render/GpuScene.h"
#include "render/Overlay.h"
#include "render/vk/Context.h"

namespace cadgeom::render {

class PointPass {
public:
    CgResult Initialize(vk::Context& ctx, VkPipelineLayout layout, VkSampleCountFlagBits samples);
    void Shutdown(vk::Context& ctx);

    /// Every point item in `snapshot`, depth tested.
    void Record(VkCommandBuffer cmd, VkPipelineLayout layout, const GpuScene& gpuScene,
                const SceneSnapshot& snapshot, const RenderView& view) const;

    /// Preview markers from a per-frame instance buffer, drawn on top.
    void RecordOverlay(VkCommandBuffer cmd, VkPipelineLayout layout, VkBuffer instances,
                       const std::vector<OverlayRun>& runs) const;

private:
    VkPipeline scene_{VK_NULL_HANDLE};
    VkPipeline overlay_{VK_NULL_HANDLE};
};

} // namespace cadgeom::render
