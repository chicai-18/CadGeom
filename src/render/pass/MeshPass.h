// Shaded solids (docs/architecture.md §4.2).
//
// One pipeline, one vertex format, per-object data in push constants. CAD needs
// a small fixed set of pipelines rather than a general material system, and
// this is the first of them.
#pragma once

#include "render/FrameData.h"
#include "render/GpuScene.h"
#include "render/vk/Context.h"

namespace cadgeom::render {

class MeshPass {
public:
    CgResult Initialize(vk::Context& ctx, VkPipelineLayout layout);
    void Shutdown(vk::Context& ctx);

    /// Draws every item in `snapshot` whose mesh made it onto the GPU. The
    /// caller has already bound the frame's descriptor set and is responsible
    /// for skipping this in RenderMode::Wireframe.
    void Record(VkCommandBuffer cmd, VkPipelineLayout layout, const GpuScene& gpuScene,
                const SceneSnapshot& snapshot, const RenderView& view) const;

private:
    VkPipeline fill_{VK_NULL_HANDLE};
};

} // namespace cadgeom::render
