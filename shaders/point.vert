#version 450
#extension GL_GOOGLE_include_directive : require

#include "Common.glsl"

// Instanced billboard quad, one per point primitive (docs/architecture.md §4.2).
// Sized in pixels for the same reason lines are: gl_PointSize has an
// implementation-defined range and no way to say what shape the point is.

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    vec4 params;  // x: diameter px  y: unused  z: depth bias (NDC)  w: unused
} object;

layout(location = 0) in vec3 inPosition;  // per-instance, object space

layout(location = 0) out vec2 vLocal;  // pixels from the centre of the dot

const vec2 kCorner[6] = vec2[6](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0,  1.0), vec2(-1.0, 1.0));

const float kMinW = 1e-4;

void main() {
    vec4 clip = scene.viewProj * (object.model * vec4(inPosition, 1.0));
    if (clip.w <= kMinW) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        vLocal = vec2(0.0);
        return;
    }

    vec2 halfViewport = 0.5 * max(scene.lineParams.yz, vec2(1.0));
    // One pixel of margin all round, for the fragment shader to fade the rim in.
    float halfSize = max(object.params.x, 1.0) * 0.5 + 1.0;

    vLocal = kCorner[gl_VertexIndex] * halfSize;
    clip.xy += (vLocal / halfViewport) * clip.w;
    clip.z -= object.params.z * clip.w;
    gl_Position = clip;
}
