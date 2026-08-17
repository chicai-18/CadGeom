#include "render/RenderSystem.h"

#include "core/Error.h"
#include "core/Log.h"
#include "render/FrameData.h"

namespace cadgeom::render {

RenderSystem::~RenderSystem() {
    Shutdown();
}

CgResult RenderSystem::Initialize(const char* applicationName, bool enableValidation,
                                  bool needPresentation) {
    if (IsValid()) {
        return CgResult::Ok;
    }

    vk::Context::Config config{};
    config.applicationName = applicationName;
    config.enableValidation = enableValidation;
    config.needPresentation = needPresentation;

    CgResult r = context_.Initialize(config);
    if (CgFailed(r)) {
        return r;
    }
    r = CreateLayouts();
    if (CgFailed(r)) {
        Shutdown();
        return r;
    }
    // 单采样那一套现在就建出来：每一个视口都用得上它，而在这里失败比在第一帧
    // 里失败好解释得多。
    if (!PassesFor(VK_SAMPLE_COUNT_1_BIT)) {
        Shutdown();
        return core::LastError();
    }
    return CgResult::Ok;
}

const PassSet* RenderSystem::PassesFor(VkSampleCountFlagBits samples) {
    if (samples == 0) {
        samples = VK_SAMPLE_COUNT_1_BIT;
    }
    for (const std::unique_ptr<PassSet>& set : passSets_) {
        if (set->samples == samples) {
            return set.get();
        }
    }
    if (!IsValid()) {
        core::SetError(CgResult::InvalidState, "PassesFor() before the device exists");
        return nullptr;
    }

    auto set = std::make_unique<PassSet>();
    set->samples = samples;

    CgResult r = set->grid.Initialize(context_, pipelineLayout_, samples);
    if (CgSucceeded(r)) {
        r = set->mesh.Initialize(context_, pipelineLayout_, samples);
    }
    if (CgSucceeded(r)) {
        r = set->edges.Initialize(context_, pipelineLayout_, samples);
    }
    if (CgSucceeded(r)) {
        r = set->lines.Initialize(context_, pipelineLayout_, samples);
    }
    if (CgSucceeded(r)) {
        r = set->points.Initialize(context_, pipelineLayout_, samples);
    }
    if (CgFailed(r)) {
        // 半套 pass 没有任何用处，而 Shutdown 对没建成的管线是安全的。
        set->points.Shutdown(context_);
        set->lines.Shutdown(context_);
        set->edges.Shutdown(context_);
        set->mesh.Shutdown(context_);
        set->grid.Shutdown(context_);
        return nullptr;
    }

    CG_DEBUG("built the pass set for %ux MSAA", static_cast<unsigned>(samples));
    passSets_.push_back(std::move(set));
    return passSets_.back().get();
}

CgResult RenderSystem::CreateLayouts() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.bindingCount = 1;
    setInfo.pBindings = &binding;
    CG_VK_REQUIRE(vkCreateDescriptorSetLayout(context_.Device(), &setInfo, nullptr,
                                              &sceneSetLayout_),
                  "vkCreateDescriptorSetLayout");

    // One layout for every pass. The grid never reads the push constants, but
    // sharing a layout means the descriptor set survives a pipeline change and
    // is bound once per frame instead of once per pass. The range is the full
    // 128 bytes every Vulkan device guarantees, so a new pass with a bigger
    // push block never has to renegotiate it.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = kPushConstantBytes;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &sceneSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    CG_VK_REQUIRE(vkCreatePipelineLayout(context_.Device(), &layoutInfo, nullptr, &pipelineLayout_),
                  "vkCreatePipelineLayout");

    return CgResult::Ok;
}

void RenderSystem::BeginFrame(uint64_t frameIndex) {
    frameIndex_ = frameIndex;
    deletions_.Flush(frameIndex);
}

void RenderSystem::Shutdown() {
    if (!context_.IsValid()) {
        return;
    }

    // Everything below assumes the GPU is finished; viewports have already been
    // torn down by the time the engine gets here, but a stray in-flight frame
    // would make every destroy below undefined.
    context_.WaitIdle();
    deletions_.FlushAll();

    geometry_.Destroy(context_);
    for (const std::unique_ptr<PassSet>& set : passSets_) {
        set->points.Shutdown(context_);
        set->lines.Shutdown(context_);
        set->edges.Shutdown(context_);
        set->mesh.Shutdown(context_);
        set->grid.Shutdown(context_);
    }
    passSets_.clear();

    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context_.Device(), pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (sceneSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_.Device(), sceneSetLayout_, nullptr);
        sceneSetLayout_ = VK_NULL_HANDLE;
    }

    context_.Shutdown();
}

} // namespace cadgeom::render
