#include "interact/OverlayBuilder.h"

#include <cadgeom/ICamera.h>

#include "geom/Curve.h"

#include <algorithm>
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
    // 默认往锚点的右上方让开半个字高，免得字压在它标注的那个点上。
    AddTextAligned(worldAnchor, 0.35 * textPixelHeight_, -0.35 * textPixelHeight_, utf8Text, color,
                   textPixelHeight_, TextAlign::Left);
}

void OverlayBuilder::AddTextAligned(const Vec3d& worldAnchor, double offsetX, double offsetY,
                                    const char* utf8Text, const Color& color, float pixelHeight,
                                    TextAlign align) {
    EmitStrokes(worldAnchor, offsetX, offsetY, utf8Text, color, pixelHeight, align);
}

bool OverlayBuilder::AddScreenText(double pixelX, double pixelY, const char* utf8Text,
                                   const Color& color, float pixelHeight, TextAlign align) {
    if (!camera_) {
        return false;
    }
    // 屏幕像素 → 世界锚点：射线打在过焦点、垂直于视线的那个平面上。落在焦平面上
    // 是有讲究的 —— 相机报的「一个像素有多大」正是在那儿量的，字因此是恒定的像素
    // 高度，透视下也不会随着离视线中心远近变大变小。
    Ray ray{};
    camera_->ScreenToRay(pixelX, pixelY, ray);

    Vec3d target{};
    Vec3d forward{};
    camera_->GetTarget(target);
    camera_->GetForward(forward);

    Vec3d anchor{};
    if (!RayPlaneIntersect(ray, target, -forward, anchor)) {
        return false;
    }
    EmitStrokes(anchor, 0.0, 0.0, utf8Text, color, pixelHeight, align);
    return true;
}

void OverlayBuilder::EmitStrokes(const Vec3d& anchor, double offsetX, double offsetY,
                                 const char* utf8Text, const Color& color, float pixelHeight,
                                 TextAlign align) {
    if (!camera_ || !utf8Text || !*utf8Text || !(pixelHeight > 0.0f)) {
        return;
    }

    // 屏幕的右和上，世界空间。文字因此永远正对观察者 —— 它是界面的一部分，不是
    // 贴在模型上的贴花。
    Vec3d forward{};
    Vec3d up{};
    camera_->GetForward(forward);
    camera_->GetUp(up);
    const Vec3d right = Normalized(Cross(forward, up));
    if (LengthSq(right) < kEpsilon) {
        return;
    }

    // 一个像素在锚点那里有多大。字高因此是恒定的像素数，缩放时不跟着变 ——
    // 和 Gizmo 的尺寸是同一个道理。
    const double perPixel = camera_->GetPixelWorldSize(anchor);
    if (!(perPixel > 0.0)) {
        return;
    }
    const double scale = perPixel * static_cast<double>(pixelHeight);

    strokePoints_.clear();
    strokeRuns_.clear();
    BuildStrokeText(utf8Text, strokePoints_, strokeRuns_);
    if (strokeRuns_.empty()) {
        return;
    }

    double alignShift = 0.0;
    if (align != TextAlign::Left) {
        const double width = StrokeTextWidth(utf8Text);
        alignShift = align == TextAlign::Center ? -0.5 * width : -width;
    }

    // offsetY 的单位是屏幕像素、y 向下，而 em 空间的 y 向上，所以这里要反号。
    const Vec3d origin = anchor + right * (offsetX * perPixel) - up * (offsetY * perPixel);

    for (const StrokeRun& run : strokeRuns_) {
        double arc = 0.0;
        for (uint32_t i = 0; i + 1 < run.count; ++i) {
            const Vec2d& p0 = strokePoints_[run.first + i];
            const Vec2d& p1 = strokePoints_[run.first + i + 1];
            const Vec3d a =
                origin + right * ((p0.x + alignShift) * scale) + up * (p0.y * scale);
            const Vec3d b =
                origin + right * ((p1.x + alignShift) * scale) + up * (p1.y * scale);

            render::OverlayLine line{};
            line.a = a;
            line.b = b;
            line.arcA = arc;
            arc += Distance(a, b);
            line.arcB = arc;
            line.color = color;
            // 笔画宽度跟着字高走，字大了笔也粗一点，比例和图纸上的字一致。
            line.width = std::max(1.0f, pixelHeight * 0.11f);
            line.style = LineStyle::Solid;
            target_.lines.push_back(line);
        }
    }
}

} // namespace cadgeom::interact
