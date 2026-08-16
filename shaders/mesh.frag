#version 450
#extension GL_GOOGLE_include_directive : require

#include "Common.glsl"

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
} object;

layout(location = 0) in vec3 vPositionRel;
layout(location = 1) in vec3 vNormal;

layout(location = 0) out vec4 outColor;

const vec3 kSkyAmbient = vec3(0.26, 0.28, 0.32);
const vec3 kGroundAmbient = vec3(0.09, 0.085, 0.08);

void main() {
    // Two-sided shading: a CAD model is routinely open, and a back face lit as
    // if it were black reads as a hole in the part rather than as its inside.
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) {
        n = -n;
    }

    // Under orthographic projection every pixel shares one view direction;
    // under perspective the eye sits at the origin of camera-relative space.
    vec3 view = scene.viewDir.w > 0.5 ? normalize(-vPositionRel) : -normalize(scene.viewDir.xyz);
    vec3 light = normalize(scene.lightDir.xyz);

    float ndl = max(dot(n, light), 0.0);
    float hemisphere = 0.5 * n.z + 0.5;  // Z-up: +Z faces the sky.

    vec3 albedo = object.color.rgb;
    vec3 ambient = albedo * mix(kGroundAmbient, kSkyAmbient, hemisphere);
    vec3 diffuse = albedo * ndl * 0.85;

    vec3 halfway = normalize(light + view);
    float specular = ndl > 0.0 ? pow(max(dot(n, halfway), 0.0), 48.0) * 0.18 : 0.0;

    outColor = vec4(ambient + diffuse + vec3(specular), object.color.a);
}
