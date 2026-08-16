#version 450
#extension GL_GOOGLE_include_directive : require

#include "Common.glsl"

// Screen-space quad expansion (docs/architecture.md §4.2).
//
// One instance per line segment, six vertices per instance, no vertex buffer for
// the quad itself. The segment is projected to clip space, divided down to
// pixels, and the quad is built around it there — which is why the line comes
// out the requested number of pixels wide at any zoom, in either projection, on
// every driver. VK_POLYGON_MODE_LINE and vkCmdSetLineWidth can promise none of
// that: `wideLines` is an optional feature, most drivers only honour 1.0, and
// neither can produce a dash pattern.

layout(push_constant) uniform PushConstants {
    mat4 model;   // object -> camera-relative world
    vec4 color;
    vec4 params;  // x: width px  y: LineStyle  z: depth bias (NDC)  w: unused
} object;

// Per-instance: the two ends of one segment, plus the arc length at each, which
// is what phases the dash pattern across a whole curve.
layout(location = 0) in vec4 inA;
layout(location = 1) in vec4 inB;

layout(location = 0) out float vPhase;   // position along the dash pattern, pixels
layout(location = 1) out float vOffset;  // signed distance from the centreline, pixels

const vec2 kCorner[6] = vec2[6](
    vec2(0.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(0.0, -1.0), vec2(1.0,  1.0), vec2(0.0, 1.0));

/// Anything at or behind this cannot be divided through. Under an orthographic
/// projection w is always 1 and this never fires.
const float kMinW = 1e-4;

void main() {
    vec4 clipA = scene.viewProj * (object.model * vec4(inA.xyz, 1.0));
    vec4 clipB = scene.viewProj * (object.model * vec4(inB.xyz, 1.0));
    float arcA = inA.w;
    float arcB = inB.w;

    if (clipA.w <= kMinW && clipB.w <= kMinW) {
        // Entirely behind the eye. z outside [0,1] clips the whole triangle
        // away, which is cheaper than letting it fold through the origin.
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        vPhase = 0.0;
        vOffset = 0.0;
        return;
    }

    // One end behind the eye: slide it forward onto the near plane so the part
    // that is visible still expands to the right width instead of shooting off
    // to infinity through the divide below.
    if (clipA.w <= kMinW) {
        float t = (kMinW - clipA.w) / (clipB.w - clipA.w);
        clipA = mix(clipA, clipB, t);
        arcA = mix(arcA, arcB, t);
    } else if (clipB.w <= kMinW) {
        float t = (kMinW - clipB.w) / (clipA.w - clipB.w);
        clipB = mix(clipB, clipA, t);
        arcB = mix(arcB, arcA, t);
    }

    vec2 viewport = max(scene.lineParams.yz, vec2(1.0));
    vec2 halfViewport = 0.5 * viewport;
    vec2 screenA = (clipA.xy / clipA.w) * halfViewport;
    vec2 screenB = (clipB.xy / clipB.w) * halfViewport;

    vec2 delta = screenB - screenA;
    float len = length(delta);
    vec2 dir = len > 1e-5 ? delta / len : vec2(1.0, 0.0);
    vec2 normal = vec2(-dir.y, dir.x);

    vec2 corner = kCorner[gl_VertexIndex];
    // A pixel of fringe on each side gives the fragment shader room to fade the
    // edge; a quad exactly as wide as the line has nothing to anti-alias with.
    float halfWidth = max(object.params.x, 1.0) * 0.5 + 1.0;

    // Square caps close the notch where two segments of a polyline meet — but
    // only for solid lines. Extending a dashed segment would lengthen every dash
    // and, on a finely tessellated circle, close the gaps altogether.
    float capExtend = object.params.y < 0.5 ? halfWidth : 0.0;

    vec4 clip = mix(clipA, clipB, corner.x);
    vOffset = corner.y * halfWidth;
    vec2 offsetPixels = normal * vOffset + dir * (corner.x * 2.0 - 1.0) * capExtend;
    clip.xy += (offsetPixels / halfViewport) * clip.w;

    // Toward the viewer: Vulkan's depth runs from 0 at the near plane.
    clip.z -= object.params.z * clip.w;

    vPhase = mix(arcA, arcB, corner.x) * scene.lineParams.x;
    gl_Position = clip;
}
