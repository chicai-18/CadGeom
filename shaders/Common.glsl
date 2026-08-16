// Shared per-frame state. The layout here must match render::FrameUniforms
// byte for byte — everything is a vec4 or a mat4 precisely so std140 padding
// never gets a chance to disagree with the C++ struct.
//
// All positions in shader space are **camera-relative**: the CPU subtracts the
// camera origin in double precision and only then narrows to float, so a model
// a hundred kilometres from the origin still renders without jitter
// (docs/architecture.md §4.3).
#ifndef CADGEOM_COMMON_GLSL
#define CADGEOM_COMMON_GLSL

layout(set = 0, binding = 0) uniform SceneBlock {
    mat4 viewProj;     // camera-relative world -> clip
    mat4 invViewProj;  // clip -> camera-relative world
    vec4 viewDir;      // xyz: world-space forward. w: 1 for perspective, 0 for ortho
    vec4 lightDir;     // xyz: world-space direction *towards* the key light
    vec4 gridParams;   // x: minor spacing  y: ground plane z  z: fade start  w: fade end
    vec4 gridOffset;   // xy: camera origin folded into one major cell  zw: camera origin XY
    vec4 focusRel;     // xyz: what the camera is looking at, camera-relative
} scene;

#endif // CADGEOM_COMMON_GLSL
