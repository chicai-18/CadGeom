#include "render/pass/LinePass.h"

#include "render/vk/Pipeline.h"

#include "shaders/line.frag.spv.h"
#include "shaders/line.vert.spv.h"

namespace cadgeom::render {
namespace {

/// Instance rate, not vertex rate: the six vertices of the quad come from
/// gl_VertexIndex, and the buffer holds one record per segment.
const VkVertexInputBindingDescription kBinding{0, sizeof(GpuLineInstance),
                                               VK_VERTEX_INPUT_RATE_INSTANCE};

const VkVertexInputAttributeDescription kAttributes[2] = {
    {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuLineInstance, a)},
    {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuLineInstance, b)},
};

/// Pushes lines a sliver toward the viewer in NDC, so a curve lying exactly on
/// the surface it outlines is not left fighting the depth buffer for it. Small
/// enough to be invisible at any zoom the camera allows.
constexpr float kSceneDepthBias = 2.0e-5f;

/// 被遮挡的曲线淡多少 —— 和 EdgePass 用同一个数，两者在图上是同一种线。
constexpr float kOccludedAlpha = 0.45f;

} // namespace

CgResult LinePass::Initialize(vk::Context& ctx, VkPipelineLayout layout,
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
        // The expanded quad's winding depends on which way the segment happens
        // to point on screen, so culling would drop half of every drawing.
        .SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .SetDepth(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
        // Needed for the anti-aliased edge and for dash gaps, not for
        // transparency.
        .SetAlphaBlend(true)
        .SetSampleCount(samples)
        .SetFormats(kColorFormat, kDepthFormat);

    r = builder.Build(ctx, layout, scene_);
    if (CgSucceeded(r)) {
        // 判据翻过来：只留被挡住的片元，隐藏线模式把它们画成虚线。
        builder.SetDepth(true, false, VK_COMPARE_OP_GREATER);
        r = builder.Build(ctx, layout, occluded_);
    }
    if (CgSucceeded(r)) {
        // Depth off entirely rather than "test but do not write": a preview the
        // user is dragging has to stay visible through the model it is being
        // drawn against.
        builder.SetDepth(false, false, VK_COMPARE_OP_ALWAYS);
        r = builder.Build(ctx, layout, overlay_);
    }

    vkDestroyShaderModule(ctx.Device(), vertex, nullptr);
    vkDestroyShaderModule(ctx.Device(), fragment, nullptr);
    return r;
}

void LinePass::Shutdown(vk::Context& ctx) {
    if (scene_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.Device(), scene_, nullptr);
        scene_ = VK_NULL_HANDLE;
    }
    if (occluded_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.Device(), occluded_, nullptr);
        occluded_ = VK_NULL_HANDLE;
    }
    if (overlay_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.Device(), overlay_, nullptr);
        overlay_ = VK_NULL_HANDLE;
    }
}

void LinePass::Record(VkCommandBuffer cmd, VkPipelineLayout layout, const GpuScene& gpuScene,
                      const SceneSnapshot& snapshot, const RenderView& view) const {
    if (scene_ == VK_NULL_HANDLE || !gpuScene.HasLines() || snapshot.curveItems.empty()) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_);

    const VkDeviceSize offset = 0;
    VkBuffer instances = gpuScene.LineInstanceBuffer();
    vkCmdBindVertexBuffers(cmd, 0, 1, &instances, &offset);

    for (const CurveItem& item : snapshot.curveItems) {
        const GpuScene::InstanceRange* range = gpuScene.LineRangeFor(item.curveIndex);
        if (!range) {
            continue;
        }

        CurvePushConstants push{};
        FillCurvePushConstants(item.worldTransform, view.cameraOrigin, item.color, item.width,
                               item.style, kSceneDepthBias, push);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(cmd, 6, range->instanceCount, 0, range->firstInstance);
    }
}

void LinePass::RecordOccluded(VkCommandBuffer cmd, VkPipelineLayout layout,
                              const GpuScene& gpuScene, const SceneSnapshot& snapshot,
                              const RenderView& view) const {
    if (occluded_ == VK_NULL_HANDLE || !gpuScene.HasLines() || snapshot.curveItems.empty()) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, occluded_);

    const VkDeviceSize offset = 0;
    VkBuffer instances = gpuScene.LineInstanceBuffer();
    vkCmdBindVertexBuffers(cmd, 0, 1, &instances, &offset);

    for (const CurveItem& item : snapshot.curveItems) {
        const GpuScene::InstanceRange* range = gpuScene.LineRangeFor(item.curveIndex);
        if (!range) {
            continue;
        }

        Color faded = item.color;
        faded.a *= kOccludedAlpha;
        CurvePushConstants push{};
        // 深度偏移给零：这一遍找的就是输在深度上的片元，往前推等于把它们推成
        // 「没被挡住」，那正是这一遍要区分的东西。
        FillCurvePushConstants(item.worldTransform, view.cameraOrigin, faded, item.width,
                               LineStyle::Hidden, 0.0f, push);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(cmd, 6, range->instanceCount, 0, range->firstInstance);
    }
}

void LinePass::RecordOverlay(VkCommandBuffer cmd, VkPipelineLayout layout, VkBuffer instances,
                             const std::vector<OverlayRun>& runs) const {
    if (overlay_ == VK_NULL_HANDLE || instances == VK_NULL_HANDLE || runs.empty()) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_);

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &instances, &offset);

    for (const OverlayRun& run : runs) {
        if (run.instanceCount == 0) {
            continue;
        }
        // Already camera-relative: the overlay is rebuilt every frame, so the
        // subtraction happened on the CPU when the buffer was filled.
        CurvePushConstants push{};
        FillCurvePushConstants(Mat4Identity(), Vec3d{0.0, 0.0, 0.0}, run.color, run.size, run.style,
                               0.0f, push);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(cmd, 6, run.instanceCount, 0, run.firstInstance);
    }
}

} // namespace cadgeom::render
