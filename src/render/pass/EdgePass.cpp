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

/// 被遮挡的边淡多少。图纸上的虚线也是比实线轻的 —— 它说的是「这条棱在背面」，
/// 不该和正面的轮廓抢注意力。
constexpr float kOccludedAlpha = 0.45f;

} // namespace

CgResult EdgePass::Initialize(vk::Context& ctx, VkPipelineLayout layout,
                              VkSampleCountFlagBits samples) {
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
        .SetSampleCount(samples)
        .SetFormats(kColorFormat, kDepthFormat);

    r = builder.Build(ctx, layout, pipeline_);
    if (CgSucceeded(r)) {
        // 把深度判据翻过来，剩下的就只有被遮挡的片元。
        builder.SetDepth(true, false, VK_COMPARE_OP_GREATER);
        r = builder.Build(ctx, layout, occluded_);
    }

    vkDestroyShaderModule(ctx.Device(), vertex, nullptr);
    vkDestroyShaderModule(ctx.Device(), fragment, nullptr);
    return r;
}

void EdgePass::Shutdown(vk::Context& ctx) {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.Device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (occluded_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.Device(), occluded_, nullptr);
        occluded_ = VK_NULL_HANDLE;
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
        FillCurvePushConstants(item.worldTransform, view.cameraOrigin,
                               view.depthOnlySurfaces ? item.soloColor : item.color, item.width,
                               item.style, 0.0f, push);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(cmd, 6, range->instanceCount, 0, range->firstInstance);
    }
}

void EdgePass::RecordOccluded(VkCommandBuffer cmd, VkPipelineLayout layout,
                              const GpuScene& gpuScene, const SceneSnapshot& snapshot,
                              const RenderView& view) const {
    if (occluded_ == VK_NULL_HANDLE || !gpuScene.HasLines() || snapshot.edgeItems.empty()) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, occluded_);

    const VkDeviceSize offset = 0;
    VkBuffer instances = gpuScene.LineInstanceBuffer();
    vkCmdBindVertexBuffers(cmd, 0, 1, &instances, &offset);

    for (const CurveItem& item : snapshot.edgeItems) {
        const GpuScene::InstanceRange* range = gpuScene.LineRangeFor(item.curveIndex);
        if (!range) {
            continue;
        }

        // 和可见的那一批同一个颜色，只是淡下去：一条虚线已经把「在背面」说清楚了，
        // 再换个颜色反而会让人以为那是别的东西。
        Color faded = view.depthOnlySurfaces ? item.soloColor : item.color;
        faded.a *= kOccludedAlpha;
        CurvePushConstants push{};
        FillCurvePushConstants(item.worldTransform, view.cameraOrigin, faded, item.width,
                               LineStyle::Hidden, 0.0f, push);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(cmd, 6, range->instanceCount, 0, range->firstInstance);
    }
}

} // namespace cadgeom::render
