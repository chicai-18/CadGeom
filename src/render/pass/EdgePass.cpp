#include "render/pass/EdgePass.h"

#include "render/vk/Pipeline.h"

#include "shaders/line.frag.spv.h"
#include "shaders/line.vert.spv.h"

namespace cadgeom::render {
namespace {

/// 和 LinePass 同一套实例格式 —— 用的就是同一个实例缓冲。
const VkVertexInputBindingDescription kBinding{0, sizeof(GpuLineInstance),
                                               VK_VERTEX_INPUT_RATE_INSTANCE};

const VkVertexInputAttributeDescription kAttributes[2] = {
    {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuLineInstance, a)},
    {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuLineInstance, b)},
};

} // namespace

CgResult EdgePass::Initialize(vk::Context& ctx, VkPipelineLayout layout) {
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;

    CgResult r = vk::CreateShaderModule(ctx, line_vert_spv, sizeof(line_vert_spv), vertex);
    if (CgFailed(r)) {
        return r;
    }
    r = vk::CreateShaderModule(ctx, line_frag_spv, sizeof(line_frag_spv), fragment);
    if (CgFailed(r)) {
        vkDestroyShaderModule(ctx.Device(), vertex, nullptr);
        return r;
    }

    vk::GraphicsPipelineBuilder builder;
    builder.SetShaders(vertex, fragment)
        .SetVertexInput(&kBinding, 1, kAttributes, 2)
        .SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        // 测深度但不写：边是贴在实体表面上的一层，把它自己的深度留下来只会挡住
        // 后面本该画在同一处的东西。挡不挡得住由它所在的那个面说了算。
        .SetDepth(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)
        .SetAlphaBlend(true)
        .SetFormats(kColorFormat, kDepthFormat);

    r = builder.Build(ctx, layout, pipeline_);

    vkDestroyShaderModule(ctx.Device(), vertex, nullptr);
    vkDestroyShaderModule(ctx.Device(), fragment, nullptr);
    return r;
}

void EdgePass::Shutdown(vk::Context& ctx) {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.Device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
}

void EdgePass::Record(VkCommandBuffer cmd, VkPipelineLayout layout, const GpuScene& gpuScene,
                      const SceneSnapshot& snapshot, const RenderView& view) const {
    if (pipeline_ == VK_NULL_HANDLE || !gpuScene.HasLines() || snapshot.edgeItems.empty()) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    const VkDeviceSize offset = 0;
    VkBuffer instances = gpuScene.LineInstanceBuffer();
    vkCmdBindVertexBuffers(cmd, 0, 1, &instances, &offset);

    for (const CurveItem& item : snapshot.edgeItems) {
        const GpuScene::InstanceRange* range = gpuScene.LineRangeFor(item.curveIndex);
        if (!range) {
            continue;
        }

        // 深度偏移给零：表面已经在 MeshPass 那边被推远了一格深度单位，那个偏移量
        // 随深度缓冲的实际分辨率走，比在这里猜一个固定的 NDC 值靠谱得多 —— 固定值
        // 在小零件上会让背面的边从正面透出来。
        CurvePushConstants push{};
        FillCurvePushConstants(item.worldTransform, view.cameraOrigin, item.color, item.width,
                               item.style, 0.0f, push);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(cmd, 6, range->instanceCount, 0, range->firstInstance);
    }
}

} // namespace cadgeom::render
