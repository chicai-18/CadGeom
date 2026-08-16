// Everything the renderer owns once per engine, as opposed to once per viewport
// (docs/architecture.md §4.1).
//
// The device, the pipelines and the uploaded geometry are shared: pipelines
// under dynamic rendering depend only on attachment *formats*, and every
// viewport composes into the same off-screen format, so a second viewport costs
// a swapchain and nothing else.
//
// Created lazily on the first CreateViewport(). A host that only ever touches
// the scene graph never initialises a GPU.
#pragma once

#include "render/GpuScene.h"
#include "render/pass/GridPass.h"
#include "render/pass/MeshPass.h"
#include "render/vk/Context.h"
#include "render/vk/DeleteQueue.h"

namespace cadgeom::render {

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
    GridPass& Grid() { return grid_; }
    MeshPass& Mesh() { return mesh_; }

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
    GridPass grid_;
    MeshPass mesh_;

    VkDescriptorSetLayout sceneSetLayout_{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};

    uint64_t frameIndex_{0};
};

} // namespace cadgeom::render
