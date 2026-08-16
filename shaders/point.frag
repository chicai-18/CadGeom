#version 450
#extension GL_GOOGLE_include_directive : require

#include "Common.glsl"

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    vec4 params;  // x: diameter px
} object;

layout(location = 0) in vec2 vLocal;

layout(location = 0) out vec4 outColor;

void main() {
    float radius = max(object.params.x, 1.0) * 0.5;
    float d = length(vLocal);

    float alpha = clamp(radius - d + 0.5, 0.0, 1.0);
    if (alpha <= 0.002) {
        discard;  // Outside the disc: a square dot would read as a pixel artefact.
    }

    // A darkened rim, so a pale point still reads against a pale background and
    // a dense cloud of them does not merge into one blob.
    float rim = clamp(d - (radius - 1.5), 0.0, 1.0);
    vec3 rgb = mix(object.color.rgb, object.color.rgb * 0.25, rim);

    outColor = vec4(rgb, object.color.a * alpha);
}
