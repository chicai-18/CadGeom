// Feature edges of solids (docs/architecture.md §4.2).
//
// CAD 的「黑边」不是装饰，是可读性的核心：一个没有轮廓线的着色实体，圆角和平面
// 长得一模一样。线段直接来自 Topology.edges —— 底环、顶环、硬角的竖直棱 —— 所以
// 画出来的是零件上真有的边，而不是三角形的每一条边。
//
// 复用 LinePass 的那对着色器和同一个实例缓冲：一条特征边和一条曲线在 GPU 上是同
// 一种东西（屏幕空间撑开的四边形）。不同的是深度策略和取哪个颜色，而那正好是一条
// 独立管线加一个 push constant 的事：
//
//   * 不写深度 —— 边是画在实体表面上的一层，让它挡住后面本该可见的东西没有道理；
//   * 表面在 MeshPass 那边被 depth bias 推远了一格，所以这里不需要再自己偏移，
//     贴在面上的边照样赢得过深度测试，而且不会在小零件上从背面透出来。
#pragma once

#include "render/FrameData.h"
#include "render/GpuScene.h"
#include "render/vk/Context.h"

namespace cadgeom::render {

class EdgePass {
public:
    CgResult Initialize(vk::Context& ctx, VkPipelineLayout layout, VkSampleCountFlagBits samples);
    void Shutdown(vk::Context& ctx);

    /// Draws `snapshot.edgeItems`. The caller has already bound the frame's
    /// descriptor set and is responsible for skipping this in RenderMode::Shaded.
    void Record(VkCommandBuffer cmd, VkPipelineLayout layout, const GpuScene& gpuScene,
                const SceneSnapshot& snapshot, const RenderView& view) const;

    /// @brief 被挡住的那些边，画成虚线 —— RenderMode::HiddenLine 的第二遍。
    ///
    /// 和 Record 同一批线段、同一个实例缓冲，区别只有深度比较反过来（GREATER）：
    /// 深度测试**没通过**的那些片元才是被遮挡的，把判据翻过来就正好只剩它们。
    /// 制图上这叫虚线，表示「零件背面确实有这条棱」。
    void RecordOccluded(VkCommandBuffer cmd, VkPipelineLayout layout, const GpuScene& gpuScene,
                        const SceneSnapshot& snapshot, const RenderView& view) const;

private:
    VkPipeline pipeline_{VK_NULL_HANDLE};
    VkPipeline occluded_{VK_NULL_HANDLE};
};

} // namespace cadgeom::render
