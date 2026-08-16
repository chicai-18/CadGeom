#version 450
#extension GL_GOOGLE_include_directive : require

#include "Common.glsl"

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

// Linear-light colours: the frame is composed in a 16-bit float target and the
// blit to the sRGB swapchain applies the encode.
const vec3 kMinorColor = vec3(0.16, 0.165, 0.18);
const vec3 kMajorColor = vec3(0.30, 0.31, 0.33);
const vec3 kAxisXColor = vec3(0.55, 0.07, 0.08);
const vec3 kAxisYColor = vec3(0.09, 0.42, 0.12);

/// Coverage of the nearest lattice line, anti-aliased to roughly one pixel.
/// `fw` is how much world space one pixel spans, which is what turns a world
/// distance into a screen distance without knowing anything about the camera.
float LineCoverage(vec2 coord, float spacing, vec2 fw) {
    vec2 toLine = abs(fract(coord / spacing - 0.5) - 0.5) * spacing;
    vec2 pixels = toLine / max(fw, vec2(1e-8));
    return 1.0 - smoothstep(0.0, 1.0, min(pixels.x, pixels.y));
}

void main() {
    // Unproject the two ends of this pixel's view ray. Doing it this way keeps
    // one code path for both projections: under orthographic the rays are
    // parallel and the near point moves per pixel, which falls out for free.
    vec4 nearH = scene.invViewProj * vec4(vNdc, 0.0, 1.0);
    vec4 farH = scene.invViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 rayStart = nearH.xyz / nearH.w;
    vec3 raySpan = farH.xyz / farH.w - rayStart;

    if (abs(raySpan.z) < 1e-9) {
        discard;  // Looking straight along the ground plane.
    }
    float t = (scene.gridParams.y - rayStart.z) / raySpan.z;
    if (t < 0.0 || t > 1.0) {
        discard;  // The plane is behind the eye or past the far plane.
    }
    vec3 hit = rayStart + raySpan * t;

    // Two different coordinates on purpose. `folded` has the camera offset
    // reduced modulo one major cell, so the lattice keeps full float precision
    // however far from the origin the model sits; `world` is the true position,
    // needed only near the axes where it is small anyway.
    vec2 folded = hit.xy + scene.gridOffset.xy;
    vec2 world = hit.xy + scene.gridOffset.zw;
    vec2 fw = fwidth(folded);

    float minorSpacing = scene.gridParams.x;
    float minor = LineCoverage(folded, minorSpacing, fw);
    float major = LineCoverage(folded, minorSpacing * 10.0, fw);

    vec3 color = mix(kMinorColor, kMajorColor, major);
    float alpha = max(minor * 0.55, major);

    vec2 axisPixels = abs(world) / max(fw, vec2(1e-8));
    float onAxisX = 1.0 - smoothstep(0.0, 1.2, axisPixels.y);
    float onAxisY = 1.0 - smoothstep(0.0, 1.2, axisPixels.x);
    float axis = max(onAxisX, onAxisY);
    if (axis > 0.0) {
        color = mix(color, onAxisX >= onAxisY ? kAxisXColor : kAxisYColor, axis);
        alpha = max(alpha, axis);
    }

    // Fade out around the point of interest rather than around the eye: under
    // orthographic projection the eye can be arbitrarily far away and a
    // distance-from-camera fade would blank the whole grid.
    float fade = 1.0 - smoothstep(scene.gridParams.z, scene.gridParams.w,
                                  length(hit - scene.focusRel.xyz));
    alpha *= fade;
    if (alpha < 0.004) {
        discard;  // Also keeps invisible fragments from writing depth.
    }

    vec4 clip = scene.viewProj * vec4(hit, 1.0);
    gl_FragDepth = clip.z / clip.w;
    outColor = vec4(color, alpha);
}
