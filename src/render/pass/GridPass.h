// Infinite ground grid (docs/architecture.md §4.2).
//
// Zero geometry: one full-screen triangle, and the fragment shader intersects
// each pixel's view ray with the z = 0 plane and works out analytically which
// lattice line it is near. That gives a grid that is exactly one pixel wide at
// any zoom, needs no level-of-detail scheme, and never runs out of extent.
#pragma once

#include "render/vk/Context.h"

namespace cadgeom::render {

class GridPass {
public:
    CgResult Initialize(vk::Context& ctx, VkPipelineLayout layout, VkSampleCountFlagBits samples);
    void Shutdown(vk::Context& ctx);

    /// The caller has already bound the frame's descriptor set.
    void Record(VkCommandBuffer cmd) const;

private:
    VkPipeline pipeline_{VK_NULL_HANDLE};
};

} // namespace cadgeom::render
