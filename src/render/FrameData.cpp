#include "render/FrameData.h"

#include <cmath>

namespace cadgeom::render {
namespace {

void Narrow(const Mat4d& in, float out[16]) {
    for (int i = 0; i < 16; ++i) {
        out[i] = static_cast<float>(in.m[i]);
    }
}

void Narrow(const Vec3d& in, double w, float out[4]) {
    out[0] = static_cast<float>(in.x);
    out[1] = static_cast<float>(in.y);
    out[2] = static_cast<float>(in.z);
    out[3] = static_cast<float>(w);
}

/// Reduces `value` into [0, period) without losing the low bits of a large
/// coordinate — std::fmod on a double keeps full precision, and only the small
/// remainder is ever narrowed to float. This is what lets the grid shader draw
/// a stable lattice with the camera a hundred kilometres from the origin.
double FoldIntoPeriod(double value, double period) {
    if (!(period > 0.0)) {
        return 0.0;
    }
    const double folded = std::fmod(value, period);
    return folded < 0.0 ? folded + period : folded;
}

} // namespace

void FillFrameUniforms(const RenderView& view, uint32_t viewportWidth, uint32_t viewportHeight,
                       FrameUniforms& out) {
    Narrow(view.viewProj, out.viewProj);
    Narrow(Inverse(view.viewProj), out.invViewProj);
    Narrow(view.forward, view.perspective ? 1.0 : 0.0, out.viewDir);
    Narrow(Normalized(view.lightDir), 0.0, out.lightDir);
    Narrow(view.focusRel, 0.0, out.focusRel);

    const double majorSpacing = view.gridSpacing * 10.0;
    out.gridParams[0] = static_cast<float>(view.gridSpacing);
    // The ground plane is z = 0 in world space, which sits here once the camera
    // origin has been subtracted.
    out.gridParams[1] = static_cast<float>(-view.cameraOrigin.z);
    out.gridParams[2] = static_cast<float>(view.gridFadeStart);
    out.gridParams[3] = static_cast<float>(view.gridFadeEnd);

    out.gridOffset[0] = static_cast<float>(FoldIntoPeriod(view.cameraOrigin.x, majorSpacing));
    out.gridOffset[1] = static_cast<float>(FoldIntoPeriod(view.cameraOrigin.y, majorSpacing));
    // The unfolded origin, for the two axis lines. Only accurate near the axes,
    // which is the only place it is read.
    out.gridOffset[2] = static_cast<float>(view.cameraOrigin.x);
    out.gridOffset[3] = static_cast<float>(view.cameraOrigin.y);

    // Screen-space expansion needs to know what a pixel is worth. A degenerate
    // camera would otherwise hand the shader an infinity and every line in the
    // frame would vanish.
    const double worldPerPixel = view.pixelWorldSize > 0.0 ? view.pixelWorldSize : 1.0;
    out.lineParams[0] = static_cast<float>(1.0 / worldPerPixel);
    out.lineParams[1] = static_cast<float>(viewportWidth);
    out.lineParams[2] = static_cast<float>(viewportHeight);
    out.lineParams[3] = 0.0f;
}

} // namespace cadgeom::render
