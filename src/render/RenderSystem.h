// Everything the renderer owns once per engine, as opposed to once per viewport
// (docs/architecture.md §4.1).
//
// The device and the uploaded geometry are shared: a second viewport costs a
// swapchain and nothing else. Pipelines are shared too, but with one axis of
// variation — under dynamic rendering a pipeline is tied to the attachment
// *formats* and the *sample count*, and M6 lets a viewport ask for MSAA. So the
// passes live in a PassSet built once per sample count actually in use; two
// viewports at the same sample count still share one set, and a host that never
// asks for MSAA never builds a second.
//
// Created lazily on the first CreateViewport(). A host that only ever touches
// the scene graph never initialises a GPU.
#pragma once

#include "render/GpuScene.h"
#include "render/pass/EdgePass.h"
#include "render/pass/GridPass.h"
#include "render/pass/LinePass.h"
#include "render/pass/MeshPass.h"
#include "render/pass/PointPass.h"
#include "render/vk/Context.h"
#include "render/vk/DeleteQueue.h"

#include <memory>
#include <vector>

namespace cadgeom::render {

/// One complete set of passes, all built for the same sample count.
struct PassSet {
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    GridPass grid;
    MeshPass mesh;
    EdgePass edges;
    LinePass lines;
    PointPass points;
};

class RenderSystem {
public:
    RenderSystem() = default;
    ~RenderSystem();

    RenderSystem(const RenderSystem&) = delete;
    RenderSystem& operator=(const RenderSystem&) = delete;

    CgResult Initialize(const char* applicationName, bool enableValidation, bool needPresentation);
    void Shutdown();

    bool IsValid() const { return context_.IsValid(); }

    vk::Context& Context() { return context_; }
    const vk::Context& Context() const { return context_; }
    vk::DeleteQueue& Deletions() { return deletions_; }
    GpuScene& Geometry() { return geometry_; }

    /// @brief 拿到（必要时建出）这个采样数的那一整套 pass。
    /// @return 建不出来时为 null，原因已经写进错误槽。
    /// @note 视口初始化时要一次并把结果记下来 —— 帧循环里不该出现「可能会失败」
    ///       的东西。
    const PassSet* PassesFor(VkSampleCountFlagBits samples);

    VkPipelineLayout PipelineLayout() const { return pipelineLayout_; }
    VkDescriptorSetLayout SceneSetLayout() const { return sceneSetLayout_; }

    /// Called once per engine tick, before any viewport renders: retires
    /// resources whose last possible reader has finished with them.
    void BeginFrame(uint64_t frameIndex);
    uint64_t FrameIndex() const { return frameIndex_; }

private:
    CgResult CreateLayouts();

    vk::Context context_;
    vk::DeleteQueue deletions_;
    GpuScene geometry_;

    /// unique_ptr 而不是直接放进 vector：视口握着 `const PassSet*`，而 vector 扩容
    /// 会把元素搬走，那个指针就悬空了。
    std::vector<std::unique_ptr<PassSet>> passSets_;

    VkDescriptorSetLayout sceneSetLayout_{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};

    uint64_t frameIndex_{0};
};

} // namespace cadgeom::render
