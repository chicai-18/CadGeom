// Transient draw data for one frame: rubber-band previews, and later gizmos and
// highlights (docs/architecture.md §4.2, OverlayPass).
//
// This is the counterpart to SceneSnapshot. The snapshot is revision-cached and
// lives on the GPU until the scene changes; the overlay is rebuilt from nothing
// every frame, because that is what a preview is — the tool re-emits its current
// state and never has to remember what it drew last time.
//
// It lives in render/ for the same reason SceneSnapshot does: render/ is the
// consumer and sits below the interact/ and api/ layers that fill it, so the
// dependency arrow keeps pointing down. Nothing here is Vulkan, and nothing here
// is narrowed — the camera-relative conversion happens at upload, in double,
// like everywhere else.
#pragma once

#include <cadgeom/Types.h>

#include "core/Math.h"

#include <vector>

namespace cadgeom::render {

struct OverlayLine {
    Vec3d a;
    Vec3d b;
    /// Arc length at each endpoint, so a dashed preview of a circle phases its
    /// pattern across the whole curve rather than restarting per chord.
    double arcA{0.0};
    double arcB{0.0};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    float width{1.5f};
    LineStyle style{LineStyle::Solid};
};

struct OverlayPoint {
    Vec3d position;
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    float size{6.0f};
};

/// A stretch of consecutive overlay instances that share everything the push
/// constants carry, so they go out as one instanced draw. A previewed circle is
/// sixty-odd segments with identical style, which is exactly the case this
/// exists to collapse.
struct OverlayRun {
    uint32_t firstInstance{0};
    uint32_t instanceCount{0};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    /// Pixel width for a line run, pixel diameter for a point run.
    float size{1.5f};
    LineStyle style{LineStyle::Solid};
};

struct OverlayData {
    std::vector<OverlayLine> lines;
    std::vector<OverlayPoint> points;

    bool IsEmpty() const { return lines.empty() && points.empty(); }

    void Clear() {
        // Capacity is deliberately kept: this is refilled every single frame,
        // and giving the memory back only to ask for it again next frame is the
        // one allocation pattern a frame loop cannot afford.
        lines.clear();
        points.clear();
    }
};

} // namespace cadgeom::render
