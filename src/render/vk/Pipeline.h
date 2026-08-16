// Graphics pipeline construction for dynamic rendering — no VkRenderPass, no
// VkFramebuffer, just the colour and depth formats the pipeline will be used
// with (docs/architecture.md §4.1).
#pragma once

#include "render/vk/Common.h"
#include "render/vk/Context.h"

#include <vector>

namespace cadgeom::render::vk {

/// SPIR-V comes from the compiled-in arrays in shaders/*.spv.h, so `code` is
/// always uint32-aligned and `sizeBytes` is always a multiple of four.
CgResult CreateShaderModule(Context& ctx, const uint32_t* code, size_t sizeBytes,
                            VkShaderModule& out);

class GraphicsPipelineBuilder {
public:
    GraphicsPipelineBuilder& SetShaders(VkShaderModule vertex, VkShaderModule fragment);
    GraphicsPipelineBuilder& SetVertexInput(const VkVertexInputBindingDescription* bindings,
                                            uint32_t bindingCount,
                                            const VkVertexInputAttributeDescription* attributes,
                                            uint32_t attributeCount);
    GraphicsPipelineBuilder& SetTopology(VkPrimitiveTopology topology);
    GraphicsPipelineBuilder& SetPolygonMode(VkPolygonMode mode);
    GraphicsPipelineBuilder& SetCullMode(VkCullModeFlags mode, VkFrontFace frontFace);
    GraphicsPipelineBuilder& SetDepth(bool test, bool write, VkCompareOp compare);
    GraphicsPipelineBuilder& SetDepthBias(float constantFactor, float slopeFactor);
    /// Straight source-over alpha blending; off means the fragment replaces.
    GraphicsPipelineBuilder& SetAlphaBlend(bool enabled);
    GraphicsPipelineBuilder& SetFormats(VkFormat colorFormat, VkFormat depthFormat);

    CgResult Build(Context& ctx, VkPipelineLayout layout, VkPipeline& out) const;

private:
    VkShaderModule vertex_{VK_NULL_HANDLE};
    VkShaderModule fragment_{VK_NULL_HANDLE};

    std::vector<VkVertexInputBindingDescription> bindings_;
    std::vector<VkVertexInputAttributeDescription> attributes_;

    VkPrimitiveTopology topology_{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPolygonMode polygonMode_{VK_POLYGON_MODE_FILL};
    VkCullModeFlags cullMode_{VK_CULL_MODE_NONE};
    VkFrontFace frontFace_{VK_FRONT_FACE_COUNTER_CLOCKWISE};

    bool depthTest_{true};
    bool depthWrite_{true};
    VkCompareOp depthCompare_{VK_COMPARE_OP_LESS_OR_EQUAL};
    float depthBiasConstant_{0.0f};
    float depthBiasSlope_{0.0f};

    bool alphaBlend_{false};

    VkFormat colorFormat_{VK_FORMAT_UNDEFINED};
    VkFormat depthFormat_{VK_FORMAT_UNDEFINED};
};

} // namespace cadgeom::render::vk
