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
    /// x: pixels per world unit  y,z: viewport size in pixels  w: unused.
    /// The line and point passes expand their geometry in screen space, so they
    /// need to know how big a pixel is.
    float lineParams[4];
};

/// Per-draw data, small enough to ride in push constants and skip a descriptor
/// update per object.
struct MeshPushConstants {
    float model[16];
    float color[4];
};

/// Shared by the line and point passes. `params` is x: pixel width (lines) or
/// pixel diameter (points), y: LineStyle as a float, z: depth bias in NDC units
/// toward the viewer, w: unused.
struct CurvePushConstants {
    float model[16];
    float color[4];
    float params[4];
};

/// Every Vulkan device guarantees at least this much, and one range shared by
/// every pass means the descriptor set survives a pipeline change.
inline constexpr uint32_t kPushConstantBytes = 128;

static_assert(sizeof(MeshPushConstants) <= kPushConstantBytes, "push constants overflow");
static_assert(sizeof(CurvePushConstants) <= kPushConstantBytes, "push constants overflow");

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

    /// World units one pixel covers at the focus point. Exact everywhere under
    /// an orthographic camera; under perspective it is right at the focus and
    /// approximate elsewhere, which is what the dash phase is scaled by.
    double pixelWorldSize{1.0};

    Color background{0.16f, 0.17f, 0.19f, 1.0f};
    bool showGrid{true};
    bool wireframe{false};
};

void FillFrameUniforms(const RenderView& view, uint32_t viewportWidth, uint32_t viewportHeight,
                       FrameUniforms& out);

} // namespace cadgeom::render
