#include "render/pass/PointPass.h"

#include "render/vk/Pipeline.h"

#include "shaders/point.frag.spv.h"
#include "shaders/point.vert.spv.h"

namespace cadgeom::render {
namespace {

const VkVertexInputBindingDescription kBinding{0, sizeof(GpuPointInstance),
                                               VK_VERTEX_INPUT_RATE_INSTANCE};

const VkVertexInputAttributeDescription kAttributes[1] = {
    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuPointInstance, position)},
};

/// A little more than the curves get: a vertex marker sits exactly on the curve
/// it belongs to and has to win that tie every time.
constexpr float kSceneDepthBias = 4.0e-5f;

void FillPush(CurvePushConstants& push, const Mat4d& model, const Vec3d& cameraOrigin,
              const Color& color, float size, float depthBias) {
    Mat4d relative = model;
    relative.m[12] -= cameraOrigin.x;
    relative.m[13] -= cameraOrigin.y;
    relative.m[14] -= cameraOrigin.z;
    for (int i = 0; i < 16; ++i) {
        push.model[i] = static_cast<float>(relative.m[i]);
    }

    push.color[0] = color.r;
    push.color[1] = color.g;
    push.color[2] = color.b;
    push.color[3] = color.a;

    push.params[0] = size;
    push.params[1] = 0.0f;
    push.params[2] = depthBias;
    push.params[3] = 0.0f;
}

} // namespace

CgResult PointPass::Initialize(vk::Context& ctx, VkPipelineLayout layout) {
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;

    CgResult r = vk::CreateShaderModule(ctx, point_vert_spv, sizeof(point_vert_spv), vertex);
    if (CgFailed(r)) {
        return r;
    }
    r = vk::CreateShaderModule(ctx, point_frag_spv, sizeof(point_frag_spv), fragment);
    if (CgFailed(r)) {
        vkDestroyShaderModule(ctx.Device(), vertex, nullptr);
        return r;
    }

    vk::GraphicsPipelineBuilder builder;
    builder.SetShaders(vertex, fragment)
        .SetVertexInput(&kBinding, 1, kAttributes, 1)
        .SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .SetDepth(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .SetAlphaBlend(true)
        .SetFormats(kColorFormat, kDepthFormat);

    r = builder.Build(ctx, layout, scene_);
    if (CgSucceeded(r)) {
        builder.SetDepth(false, false, VK_COMPARE_OP_ALWAYS);
        r = builder.Build(ctx, layout, overlay_);
    }

    vkDestroyShaderModule(ctx.Device(), vertex, nullptr);
    vkDestroyShaderModule(ctx.Device(), fragment, nullptr);
    return r;
}

void PointPass::Shutdown(vk::Context& ctx) {
    if (scene_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.Device(), scene_, nullptr);
        scene_ = VK_NULL_HANDLE;
    }
    if (overlay_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.Device(), overlay_, nullptr);
        overlay_ = VK_NULL_HANDLE;
    }
}

void PointPass::Record(VkCommandBuffer cmd, VkPipelineLayout layout, const GpuScene& gpuScene,
                       const SceneSnapshot& snapshot, const RenderView& view) const {
    if (scene_ == VK_NULL_HANDLE || !gpuScene.HasPoints() || snapshot.pointItems.empty()) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_);

    const VkDeviceSize offset = 0;
    VkBuffer instances = gpuScene.PointInstanceBuffer();
    vkCmdBindVertexBuffers(cmd, 0, 1, &instances, &offset);

    for (const PointItem& item : snapshot.pointItems) {
        const GpuScene::InstanceRange* range = gpuScene.PointRangeFor(item.curveIndex);
        if (!range) {
            continue;
        }

        CurvePushConstants push{};
        FillPush(push, item.worldTransform, view.cameraOrigin, item.color, item.size,
                 kSceneDepthBias);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(cmd, 6, range->instanceCount, 0, range->firstInstance);
    }
}

void PointPass::RecordOverlay(VkCommandBuffer cmd, VkPipelineLayout layout, VkBuffer instances,
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
        CurvePushConstants push{};
        FillPush(push, Mat4Identity(), Vec3d{0.0, 0.0, 0.0}, run.color, run.size, 0.0f);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(cmd, 6, run.instanceCount, 0, run.firstInstance);
    }
}

} // namespace cadgeom::render
