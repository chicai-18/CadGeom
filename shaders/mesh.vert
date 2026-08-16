#version 450
#extension GL_GOOGLE_include_directive : require

#include "Common.glsl"

layout(push_constant) uniform PushConstants {
    mat4 model;  // object -> camera-relative world
    vec4 color;
} object;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 vPositionRel;
layout(location = 1) out vec3 vNormal;

void main() {
    vec4 positionRel = object.model * vec4(inPosition, 1.0);
    vPositionRel = positionRel.xyz;
    // mat3 of the model matrix is the right normal transform for the rigid and
    // uniformly scaled cases, which is everything the kernel produces. Non-
    // uniform scale needs the inverse transpose and arrives with it.
    vNormal = mat3(object.model) * inNormal;
    gl_Position = scene.viewProj * positionRel;
}
