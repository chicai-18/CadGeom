// Independent lines, circles and rectangles (docs/architecture.md §4.2).
//
// Screen-space quad expansion, one instance per segment. The alternative —
// VK_POLYGON_MODE_LINE with vkCmdSetLineWidth — cannot be relied on: `wideLines`
// is an optional feature, most drivers clamp the width to 1.0, and neither
// approach can produce a dash pattern or a proper end cap. A handful of lines of
// vertex-shader arithmetic buys all three.
//
// Two pipelines: the scene one is depth tested, so a curve behind a solid is
// hidden by it; the overlay one is not, so a rubber-band preview and, later, a
// gizmo stay visible through whatever they are being dragged across.
#pragma once

#include "render/FrameData.h"
#include "render/GpuScene.h"
#include "render/Overlay.h"
#include "render/vk/Context.h"

namespace cadgeom::render {

class LinePass {
public:
    CgResult Initialize(vk::Context& ctx, VkPipelineLayout layout, VkSampleCountFlagBits samples);
    void Shutdown(vk::Context& ctx);

    /// Every curve in `snapshot`, depth tested. The caller has already bound the
    /// frame's descriptor set.
    void Record(VkCommandBuffer cmd, VkPipelineLayout layout, const GpuScene& gpuScene,
                const SceneSnapshot& snapshot, const RenderView& view) const;

    /// @brief 被实体挡住的那一段曲线，画成虚线（RenderMode::HiddenLine）。
    /// @note 和 EdgePass::RecordOccluded 是同一手：深度比较反过来，剩下的正好是
    ///       没通过深度测试的片元。
    void RecordOccluded(VkCommandBuffer cmd, VkPipelineLayout layout, const GpuScene& gpuScene,
                        const SceneSnapshot& snapshot, const RenderView& view) const;

    /// Preview geometry from a per-frame instance buffer, drawn on top of the
    /// scene. `runs` index into `instances`.
    void RecordOverlay(VkCommandBuffer cmd, VkPipelineLayout layout, VkBuffer instances,
                       const std::vector<OverlayRun>& runs) const;

private:
    VkPipeline scene_{VK_NULL_HANDLE};
    VkPipeline occluded_{VK_NULL_HANDLE};
    VkPipeline overlay_{VK_NULL_HANDLE};
};

} // namespace cadgeom::render
