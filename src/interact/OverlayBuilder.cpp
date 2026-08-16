#include "interact/OverlayBuilder.h"

#include "core/Log.h"
#include "geom/Curve.h"

#include <vector>

namespace cadgeom::interact {

OverlayBuilder::OverlayBuilder(render::OverlayData& target) : target_(target) {}

OverlayBuilder::~OverlayBuilder() = default;

void OverlayBuilder::AddPoint(const Vec3d& p, const Color& color, float pixelSize) {
    render::OverlayPoint point{};
    point.position = p;
    point.color = color;
    point.size = pixelSize > 0.0f ? pixelSize : 6.0f;
    target_.points.push_back(point);
}

void OverlayBuilder::AddLine(const Vec3d& a, const Vec3d& b, const Color& color, float pixelWidth,
                             LineStyle style) {
    render::OverlayLine line{};
    line.a = a;
    line.b = b;
    line.arcA = 0.0;
    line.arcB = Distance(a, b);
    line.color = color;
    line.width = pixelWidth > 0.0f ? pixelWidth : 1.5f;
    line.style = style;
    target_.lines.push_back(line);
}

void OverlayBuilder::AddPolyline(CgSpan<const Vec3d> points, bool closed, const Color& color,
                                 float pixelWidth, LineStyle style) {
    if (!points.data || points.count < 2) {
        return;
    }

    const size_t segments = closed ? points.count : points.count - 1;
    const float width = pixelWidth > 0.0f ? pixelWidth : 1.5f;

    // Arc length carries across the whole chain so a dashed pattern runs
    // continuously through the corners instead of restarting at each one.
    double arc = 0.0;
    for (size_t i = 0; i < segments; ++i) {
        const Vec3d& a = points.data[i];
        const Vec3d& b = points.data[(i + 1) % points.count];

        render::OverlayLine line{};
        line.a = a;
        line.b = b;
        line.arcA = arc;
        arc += Distance(a, b);
        line.arcB = arc;
        line.color = color;
        line.width = width;
        line.style = style;
        target_.lines.push_back(line);
    }
}

void OverlayBuilder::AddCircle(const Plane& plane, double radius, const Color& color,
                               float pixelWidth, LineStyle style) {
    if (!(radius > 0.0) || !geom::IsUsablePlane(plane)) {
        return;
    }

    const uint32_t segments = geom::ArcSegmentCount(radius, kTwoPi, tess_);
    std::vector<Vec3d> points;
    geom::SampleArc(plane, radius, 0.0, kTwoPi, segments, /*dropLastPoint=*/true, points);
    AddPolyline(CgSpan<const Vec3d>{points.data(), points.size()}, /*closed=*/true, color,
                pixelWidth, style);
}

void OverlayBuilder::AddText(const Vec3d& worldAnchor, const char* utf8Text, const Color& color) {
    // Text needs a glyph atlas and a text pipeline, which arrive with the ImGui
    // panels in M6. Dropped rather than faked, but said out loud once per call
    // so a tool author is not left wondering where their label went.
    (void)worldAnchor;
    (void)color;
    CG_TRACE("overlay text \"%s\" dropped: text rendering arrives in milestone M6",
             utf8Text ? utf8Text : "");
}

} // namespace cadgeom::interact
