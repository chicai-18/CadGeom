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

CgResult MeshPass::Initialize(vk::Context& ctx, VkPipelineLayout layout) {
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
        .SetFormats(kColorFormat, kDepthFormat);

    r = builder.Build(ctx, layout, fill_);

    if (CgSucceeded(r) && ctx.SupportsFillModeNonSolid()) {
        // A stand-in for RenderMode::Wireframe until the EdgePass in M4 draws
        // real feature edges. Optional because fillModeNonSolid is optional.
        builder.SetPolygonMode(VK_POLYGON_MODE_LINE);
        const CgResult wire = builder.Build(ctx, layout, wireframe_);
        if (CgFailed(wire)) {
            CG_WARN("wireframe pipeline unavailable; RenderMode::Wireframe will render shaded");
            wireframe_ = VK_NULL_HANDLE;
        }
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
    if (wireframe_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.Device(), wireframe_, nullptr);
        wireframe_ = VK_NULL_HANDLE;
    }
}

void MeshPass::Record(VkCommandBuffer cmd, VkPipelineLayout layout, const GpuScene& gpuScene,
                      const SceneSnapshot& snapshot, const RenderView& view) const {
    if (fill_ == VK_NULL_HANDLE || !gpuScene.HasGeometry() || snapshot.items.empty()) {
        return;
    }

    const VkPipeline pipeline =
        (view.wireframe && wireframe_ != VK_NULL_HANDLE) ? wireframe_ : fill_;
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
