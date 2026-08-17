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
    CgResult Initialize(vk::Context& ctx, VkPipelineLayout layout, VkSampleCountFlagBits samples);
    void Shutdown(vk::Context& ctx);

    /// Draws every item in `snapshot` whose mesh made it onto the GPU. The
    /// caller has already bound the frame's descriptor set and is responsible
    /// for skipping this in RenderMode::Wireframe.
    ///
    /// `view.depthOnlySurfaces` 换成只写深度的那条管线：RenderMode::HiddenLine
    /// 要的正是「表面看不见但挡得住」—— 面把深度铺出来，边才分得清哪一条被遮住。
    void Record(VkCommandBuffer cmd, VkPipelineLayout layout, const GpuScene& gpuScene,
                const SceneSnapshot& snapshot, const RenderView& view) const;

private:
    VkPipeline fill_{VK_NULL_HANDLE};
    /// 同样的几何、同样的 depth bias，只是颜色一个字节都不写。
    VkPipeline depthOnly_{VK_NULL_HANDLE};
};

} // namespace cadgeom::render
