#include "render/pass/MeshPass.h"

#include "render/vk/Pipeline.h"

#include "shaders/mesh.frag.spv.h"
#include "shaders/mesh.vert.spv.h"

namespace cadgeom::render {
namespace {

const VkVertexInputBindingDescription kBinding{0, sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX};

const VkVertexInputAttributeDescription kAttributes[2] = {
    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, position)},
    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, normal)},
};

} // namespace

CgResult MeshPass::Initialize(vk::Context& ctx, VkPipelineLayout layout,
                              VkSampleCountFlagBits samples) {
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;

    CgResult r = vk::CreateShaderModule(ctx, mesh_vert_spv, sizeof(mesh_vert_spv), vertex);
    if (CgFailed(r)) {
        return r;
    }
    r = vk::CreateShaderModule(ctx, mesh_frag_spv, sizeof(mesh_frag_spv), fragment);
    if (CgFailed(r)) {
        vkDestroyShaderModule(ctx.Device(), vertex, nullptr);
        return r;
    }

    vk::GraphicsPipelineBuilder builder;
    builder.SetShaders(vertex, fragment)
        .SetVertexInput(&kBinding, 1, kAttributes, 2)
        .SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        // No back-face culling: CAD models are routinely open shells and a
        // sketch profile has no inside at all. The fragment shader flips the
        // normal for back faces so both sides light correctly.
        .SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .SetDepth(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
        // 表面整体往后推一格深度单位，斜面按坡度多推一点。EdgePass 画的特征边就
        // 贴在这些面上，推开表面比给边加一个固定的 NDC 偏移可靠得多：depth bias
        // 的单位是深度缓冲自己的最小可分辨量，一个一米的零件和一个一毫米的零件
        // 因此都合适，而固定偏移量在小零件上会让背面的边从正面透出来。
        .SetDepthBias(1.0f, 1.5f)
        .SetSampleCount(samples)
        .SetFormats(kColorFormat, kDepthFormat);

    r = builder.Build(ctx, layout, fill_);
    if (CgSucceeded(r)) {
        // 隐藏线模式的第一遍：深度照写，颜色不写。深度偏移一并留着 —— 边依旧贴在
        // 这些面上，需要被推开的那一格和着色模式下是同一格。
        builder.SetColorWrite(false);
        r = builder.Build(ctx, layout, depthOnly_);
    }

    vkDestroyShaderModule(ctx.Device(), vertex, nullptr);
    vkDestroyShaderModule(ctx.Device(), fragment, nullptr);
    return r;
}

void MeshPass::Shutdown(vk::Context& ctx) {
    if (fill_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.Device(), fill_, nullptr);
        fill_ = VK_NULL_HANDLE;
    }
    if (depthOnly_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.Device(), depthOnly_, nullptr);
        depthOnly_ = VK_NULL_HANDLE;
    }
}

void MeshPass::Record(VkCommandBuffer cmd, VkPipelineLayout layout, const GpuScene& gpuScene,
                      const SceneSnapshot& snapshot, const RenderView& view) const {
    const VkPipeline pipeline = view.depthOnlySurfaces ? depthOnly_ : fill_;
    if (pipeline == VK_NULL_HANDLE || !gpuScene.HasGeometry() || snapshot.items.empty()) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    const VkDeviceSize offset = 0;
    VkBuffer vertexBuffer = gpuScene.VertexBuffer();
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, gpuScene.IndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    for (const DrawItem& item : snapshot.items) {
        const GpuScene::MeshRange* range = gpuScene.RangeFor(item.meshIndex);
        if (!range) {
            continue;
        }

        // The one place world coordinates become camera-relative. Subtracting
        // in double and narrowing afterwards is what keeps a model far from the
        // origin from shimmering (§4.3).
        Mat4d model = item.worldTransform;
        model.m[12] -= view.cameraOrigin.x;
        model.m[13] -= view.cameraOrigin.y;
        model.m[14] -= view.cameraOrigin.z;

        MeshPushConstants push{};
        for (int i = 0; i < 16; ++i) {
            push.model[i] = static_cast<float>(model.m[i]);
        }
        push.color[0] = item.color.r;
        push.color[1] = item.color.g;
        push.color[2] = item.color.b;
        push.color[3] = item.color.a;

        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, range->indexCount, 1, range->firstIndex, range->vertexOffset, 0);
    }
}

} // namespace cadgeom::render
