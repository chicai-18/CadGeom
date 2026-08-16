// Where a tool's rubber-band preview goes (docs/architecture.md §6.2).
//
// Preview geometry never enters the scene and never enters the undo stack: the
// tool re-emits it every frame from its current state, and only a commit
// produces a command. That is the whole reason this interface exists separately
// from IGeometryBuilder — the two look similar and mean opposite things.
#pragma once

#include <cadgeom/ITool.h>

#include "core/ObjectTracker.h"
#include "render/Overlay.h"

namespace cadgeom::interact {

class OverlayBuilder final : public IOverlayBuilder {
public:
    explicit OverlayBuilder(render::OverlayData& target);
    ~OverlayBuilder() override;

    void AddPoint(const Vec3d& p, const Color& color, float pixelSize) override;
    void AddLine(const Vec3d& a, const Vec3d& b, const Color& color, float pixelWidth,
                 LineStyle style) override;
    void AddPolyline(CgSpan<const Vec3d> points, bool closed, const Color& color, float pixelWidth,
                     LineStyle style) override;
    void AddCircle(const Plane& plane, double radius, const Color& color, float pixelWidth,
                   LineStyle style) override;
    void AddText(const Vec3d& worldAnchor, const char* utf8Text, const Color& color) override;

    /// Tessellation quality for AddCircle. Set from the same TessParams the
    /// kernel uses, so a previewed circle and the circle it becomes have the
    /// same silhouette.
    void SetTessParams(const TessParams& tess) { tess_ = tess; }

private:
    core::ObjectTracker tracker_;
    render::OverlayData& target_;
    TessParams tess_{};
};

} // namespace cadgeom::interact
