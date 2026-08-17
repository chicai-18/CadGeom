#include "render/vk/Pipeline.h"

namespace cadgeom::render::vk {

CgResult CreateShaderModule(Context& ctx, const uint32_t* code, size_t sizeBytes,
                            VkShaderModule& out) {
    out = VK_NULL_HANDLE;

    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = sizeBytes;
    info.pCode = code;

    CG_VK_REQUIRE(vkCreateShaderModule(ctx.Device(), &info, nullptr, &out), "vkCreateShaderModule");
    return CgResult::Ok;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetShaders(VkShaderModule vertex,
                                                             VkShaderModule fragment) {
    vertex_ = vertex;
    fragment_ = fragment;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetVertexInput(
    const VkVertexInputBindingDescription* bindings, uint32_t bindingCount,
    const VkVertexInputAttributeDescription* attributes, uint32_t attributeCount) {
    bindings_.assign(bindings, bindings + bindingCount);
    attributes_.assign(attributes, attributes + attributeCount);
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetTopology(VkPrimitiveTopology topology) {
    topology_ = topology;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetCullMode(VkCullModeFlags mode,
                                                              VkFrontFace frontFace) {
    cullMode_ = mode;
    frontFace_ = frontFace;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetDepth(bool test, bool write,
                                                           VkCompareOp compare) {
    depthTest_ = test;
    depthWrite_ = write;
    depthCompare_ = compare;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetDepthBias(float constantFactor,
                                                               float slopeFactor) {
    depthBiasConstant_ = constantFactor;
    depthBiasSlope_ = slopeFactor;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetAlphaBlend(bool enabled) {
    alphaBlend_ = enabled;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetColorWrite(bool enabled) {
    colorWrite_ = enabled;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetSampleCount(VkSampleCountFlagBits samples) {
    samples_ = samples != 0 ? samples : VK_SAMPLE_COUNT_1_BIT;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetFormats(VkFormat colorFormat,
                                                             VkFormat depthFormat) {
    colorFormat_ = colorFormat;
    depthFormat_ = depthFormat;
    return *this;
}

CgResult GraphicsPipelineBuilder::Build(Context& ctx, VkPipelineLayout layout,
                                        VkPipeline& out) const {
    out = VK_NULL_HANDLE;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex_;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment_;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings_.size());
    vertexInput.pVertexBindingDescriptions = bindings_.empty() ? nullptr : bindings_.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes_.size());
    vertexInput.pVertexAttributeDescriptions = attributes_.empty() ? nullptr : attributes_.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = topology_;

    // Viewport and scissor are dynamic: a pipeline outlives every resize.
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    // 永远填充。线条和特征边都是在顶点着色器里撑开的四边形，谁也不需要
    // VK_POLYGON_MODE_LINE —— 而那是个可选特性（§4.2）。
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = cullMode_;
    raster.frontFace = frontFace_;
    raster.lineWidth = 1.0f;
    raster.depthBiasEnable =
        (depthBiasConstant_ != 0.0f || depthBiasSlope_ != 0.0f) ? VK_TRUE : VK_FALSE;
    raster.depthBiasConstantFactor = depthBiasConstant_;
    raster.depthBiasSlopeFactor = depthBiasSlope_;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = samples_;
    multisample.minSampleShading = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = depthTest_ ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = depthWrite_ ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = depthCompare_;
    depthStencil.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        colorWrite_ ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT)
                    : 0;
    blendAttachment.blendEnable = alphaBlend_ ? VK_TRUE : VK_FALSE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat_;
    rendering.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depthStencil;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;

    CG_VK_REQUIRE(
        vkCreateGraphicsPipelines(ctx.Device(), VK_NULL_HANDLE, 1, &info, nullptr, &out),
        "vkCreateGraphicsPipelines");
    return CgResult::Ok;
}

} // namespace cadgeom::render::vk
