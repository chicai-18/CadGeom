#include "render/pass/GridPass.h"

#include "render/FrameData.h"
#include "render/vk/Pipeline.h"

#include "shaders/grid.frag.spv.h"
#include "shaders/grid.vert.spv.h"

namespace cadgeom::render {

CgResult GridPass::Initialize(vk::Context& ctx, VkPipelineLayout layout,
                              VkSampleCountFlagBits samples) {
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;

    CgResult r = vk::CreateShaderModule(ctx, grid_vert_spv, sizeof(grid_vert_spv), vertex);
    if (CgFailed(r)) {
        return r;
    }
    r = vk::CreateShaderModule(ctx, grid_frag_spv, sizeof(grid_frag_spv), fragment);
    if (CgFailed(r)) {
        vkDestroyShaderModule(ctx.Device(), vertex, nullptr);
        return r;
    }

    vk::GraphicsPipelineBuilder builder;
    builder.SetShaders(vertex, fragment)
        .SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        // Depth-tested and depth-writing: the grid is a real surface in the
        // world, so geometry below the ground plane has to be hidden by it and
        // geometry above it has to occlude it. The shader discards fully faded
        // fragments so they cannot write depth for a line nobody can see.
        .SetDepth(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .SetAlphaBlend(true)
        .SetSampleCount(samples)
        .SetFormats(kColorFormat, kDepthFormat);

    r = builder.Build(ctx, layout, pipeline_);

    vkDestroyShaderModule(ctx.Device(), vertex, nullptr);
    vkDestroyShaderModule(ctx.Device(), fragment, nullptr);
    return r;
}

void GridPass::Shutdown(vk::Context& ctx) {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.Device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
}

void GridPass::Record(VkCommandBuffer cmd) const {
    if (pipeline_ == VK_NULL_HANDLE) {
        return;
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace cadgeom::render
