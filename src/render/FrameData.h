// The contract between a viewport's camera and the passes that draw for it.
//
// Everything above this line is double; everything below it is float. The
// narrowing happens once, in FillFrameUniforms, and always relative to the
// camera origin (docs/architecture.md §0.1).
#pragma once

#include "core/Math.h"
#include "render/vk/Common.h"

namespace cadgeom::render {

/// The frame is composed off-screen in half-float linear light and blitted to
/// the swapchain at the end. That buys correct sRGB output today and leaves
/// room for MSAA, tone mapping and post effects without touching any pass.
inline constexpr VkFormat kColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
inline constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

/// CPU runs at most one frame ahead of the GPU.
inline constexpr uint32_t kFramesInFlight = 2;

/// Mirrors SceneBlock in shaders/Common.glsl. Nothing but vec4s and mat4s, so
/// std140 padding has nothing to disagree about.
struct FrameUniforms {
    float viewProj[16];
    float invViewProj[16];
    float viewDir[4];
    float lightDir[4];
    float gridParams[4];
    float gridOffset[4];
    float focusRel[4];
};

/// Per-draw data, small enough to ride in push constants and skip a descriptor
/// update per object.
struct MeshPushConstants {
    float model[16];
    float color[4];
};

static_assert(sizeof(MeshPushConstants) <= 128,
              "push constants must fit the 128-byte minimum every Vulkan device guarantees");

/// One viewport's view of the world for one frame, still in double.
struct RenderView {
    /// Camera-relative world -> clip. Built with the eye at the origin.
    Mat4d viewProj{Mat4Identity()};
    /// World position everything is expressed relative to (the eye).
    Vec3d cameraOrigin{0.0, 0.0, 0.0};
    /// Unit vector the camera looks along, world space.
    Vec3d forward{0.0, 1.0, 0.0};
    /// Unit vector towards the key light, world space.
    Vec3d lightDir{0.3, -0.5, 0.8};
    /// What the camera is looking at, camera-relative. The grid fades around
    /// this rather than around the eye, which under orthographic projection can
    /// be arbitrarily far away.
    Vec3d focusRel{0.0, 0.0, 0.0};
    bool perspective{false};

    double gridSpacing{1.0};
    double gridFadeStart{50.0};
    double gridFadeEnd{100.0};

    Color background{0.16f, 0.17f, 0.19f, 1.0f};
    bool showGrid{true};
    bool wireframe{false};
};

void FillFrameUniforms(const RenderView& view, FrameUniforms& out);

} // namespace cadgeom::render
