#version 450
#extension GL_GOOGLE_include_directive : require

#include "Common.glsl"

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    vec4 params;  // x: width px  y: LineStyle  z: depth bias  w: unused
} object;

layout(location = 0) in float vPhase;
layout(location = 1) in float vOffset;

layout(location = 0) out vec4 outColor;

/// One dash period as (length, end of the first mark, start of the second,
/// end of the second) in pixels. Two marks per period is enough for every entry
/// in LineStyle, including centre lines, so the whole table fits in a vec4 and
/// the shader needs no loop.
vec4 DashPattern(int style) {
    if (style == 1) return vec4(12.0,  8.0,  0.0,  0.0);  // Dashed
    if (style == 2) return vec4( 5.0,  1.5,  0.0,  0.0);  // Dotted
    if (style == 3) return vec4(18.0, 10.0, 13.0, 14.5);  // DashDot
    if (style == 4) return vec4(26.0, 16.0, 20.0, 24.0);  // Center: long-short-long
    if (style == 5) return vec4( 8.0,  4.0,  0.0,  0.0);  // Hidden
    return vec4(0.0);                                     // Solid
}

/// Coverage of the run [from, to], with a half-pixel ramp at each end so a dash
/// boundary does not crawl as the view moves.
float RunCoverage(float t, float from, float to) {
    return clamp(t - from + 0.5, 0.0, 1.0) * clamp(to - t + 0.5, 0.0, 1.0);
}

void main() {
    // Across the width: the quad is a pixel wider than the line on each side,
    // and this is what turns that margin into a soft edge.
    float halfWidth = max(object.params.x, 1.0) * 0.5;
    float across = clamp(halfWidth - abs(vOffset) + 0.5, 0.0, 1.0);

    float along = 1.0;
    vec4 pattern = DashPattern(int(object.params.y + 0.5));
    if (pattern.x > 0.0) {
        float t = mod(vPhase, pattern.x);
        along = RunCoverage(t, 0.0, pattern.y);
        if (pattern.w > pattern.z) {
            along = max(along, RunCoverage(t, pattern.z, pattern.w));
        }
    }

    float alpha = object.color.a * across * along;
    if (alpha <= 0.002) {
        // Discarding rather than blending a transparent fragment keeps the gap
        // in a dashed line from writing depth and occluding what is behind it.
        discard;
    }
    outColor = vec4(object.color.rgb, alpha);
}
