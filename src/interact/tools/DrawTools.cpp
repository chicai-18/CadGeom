#include "interact/tools/DrawTools.h"

#include <cadgeom/ICamera.h>
#include <cadgeom/IGeometryBuilder.h>
#include <cadgeom/IScene.h>
#include <cadgeom/ISelection.h>

#include "core/Log.h"
#include "interact/ToolContext.h"
#include "interact/WorkPlane.h"
#include "interact/tools/ToolBase.h"

#include <cmath>
#include <vector>

namespace cadgeom::interact {
namespace {

/// The world-space distance `kClickSlopPixels` covers at `at`. Turning a pixel
/// tolerance into a model-space one is what keeps a tool's idea of "the same
/// place" independent of zoom — and it is the same conversion picking will use.
double WorldSlop(IToolContext* ctx, const Vec3d& at) {
    const ICamera* camera = ctx ? ctx->GetCamera() : nullptr;
    return camera ? camera->GetPixelWorldSize(at) * kClickSlopPixels : 0.0;
}

IGeometryBuilder* BuilderOf(IToolContext* ctx) {
    IScene* scene = ctx ? ctx->GetScene() : nullptr;
    return scene ? scene->GetGeometryBuilder() : nullptr;
}

// ---------------------------------------------------------------------------
// Select
// ---------------------------------------------------------------------------

class SelectTool final : public ToolBase {
public:
    explicit SelectTool(const ToolSettings& settings) : ToolBase(settings) {}

    ToolId GetId() const override { return ToolId::Select; }
    const char* GetName() const override { return "Select"; }

    ToolResult OnMouseDown(const MouseEvent& e, IToolContext* ctx) override {
        if (!IsLeft(e) || !ctx) {
            return ToolResult::Ignored;
        }
        PickAndSelect(e, ctx);
        return ToolResult::Handled;
    }

    ToolResult OnMouseMove(const MouseEvent& e, IToolContext* ctx) override {
        // 预高亮：用户点下去之前就知道会选中什么。不认领这次事件 —— 只是跟踪
        // 光标的工具没有资格把它吞掉。
        UpdateHover(e, ctx);
        return ToolResult::Ignored;
    }

    void OnDeactivate(IToolContext* ctx) override {
        // 高亮跟着光标走，换工具之后光标已经不在这里了。
        if (IScene* scene = ctx ? ctx->GetScene() : nullptr) {
            scene->GetSelection()->SetHovered(kInvalidEntity);
        }
        ToolBase::OnDeactivate(ctx);
    }

    ToolResult OnKey(const KeyEvent& e, IToolContext* ctx) override {
        if (e.action != KeyAction::Up && e.key == Key_Delete && ctx) {
            IScene* scene = ctx->GetScene();
            ISelection* selection = scene ? scene->GetSelection() : nullptr;
            if (selection && selection->GetCount() > 0) {
                std::vector<EntityId> doomed;
                doomed.reserve(selection->GetCount());
                for (uint32_t i = 0; i < selection->GetCount(); ++i) {
                    doomed.push_back(selection->GetAt(i));
                }
                selection->Clear();
                scene->DestroyEntities(CgSpan<const EntityId>{doomed.data(), doomed.size()});
                return ToolResult::Handled;
            }
        }
        return ToolBase::OnKey(e, ctx);
    }

protected:
    const char* Prompt() const override { return "Select an object"; }

private:
    ~SelectTool() override = default;
};

// ---------------------------------------------------------------------------
// Point
// ---------------------------------------------------------------------------

class PointTool final : public ToolBase {
public:
    explicit PointTool(const ToolSettings& settings) : ToolBase(settings) {}

    ToolId GetId() const override { return ToolId::Point; }
    const char* GetName() const override { return "Point"; }

    ToolResult OnMouseDown(const MouseEvent& e, IToolContext* ctx) override {
        if (!IsLeft(e)) {
            return ToolResult::Ignored;
        }
        Vec3d p{};
        if (!CursorPoint(e, ctx, p)) {
            return ToolResult::Ignored;
        }
        if (IGeometryBuilder* builder = BuilderOf(ctx)) {
            builder->MakePoint(p);
        }
        return ToolResult::Completed;
    }

    void BuildPreview(IOverlayBuilder* overlay, IToolContext*) override {
        if (overlay && hasCursor_) {
            overlay->AddPoint(cursor_, kPreviewColor, kAnchorPixels);
        }
    }

protected:
    const char* Prompt() const override { return "Specify point"; }

private:
    ~PointTool() override = default;
};

// ---------------------------------------------------------------------------
// Two-point gestures: line, circle, rectangle
// ---------------------------------------------------------------------------

class TwoPointTool : public ToolBase {
public:
    ToolResult OnMouseDown(const MouseEvent& e, IToolContext* ctx) override {
        if (!IsLeft(e)) {
            return ToolResult::Ignored;
        }
        Vec3d p{};
        if (!CursorPoint(e, ctx, p)) {
            return ToolResult::Ignored;
        }
        cursor_ = p;
        hasCursor_ = true;

        if (!anchored_) {
            anchor_ = p;
            anchored_ = true;
            dragCandidate_ = true;
            pressX_ = e.x;
            pressY_ = e.y;
            ShowPrompt(ctx);
            return ToolResult::Handled;
        }
        return Finish(ctx, p);
    }

    ToolResult OnMouseMove(const MouseEvent& e, IToolContext* ctx) override {
        ToolBase::OnMouseMove(e, ctx);
        // Only claimed mid-gesture: an idle tool tracking the cursor has no
        // business swallowing the event.
        return anchored_ ? ToolResult::Handled : ToolResult::Ignored;
    }

    ToolResult OnMouseUp(const MouseEvent& e, IToolContext* ctx) override {
        if (!IsLeft(e) || !anchored_ || !dragCandidate_) {
            return ToolResult::Ignored;
        }
        if (!BeyondClickSlop(pressX_, pressY_, e.x, e.y)) {
            // A click, not a drag. Keep the anchor and wait for the second
            // click; this is the same gesture, just the other habit.
            dragCandidate_ = false;
            return ToolResult::Handled;
        }
        Vec3d p{};
        if (CursorPoint(e, ctx, p)) {
            cursor_ = p;
        }
        return Finish(ctx, cursor_);
    }

protected:
    explicit TwoPointTool(const ToolSettings& settings) : ToolBase(settings) {}
    ~TwoPointTool() override = default;

    /// Builds the shape. Returning kInvalidEntity means "too small to be real
    /// yet", and the gesture stays open rather than being thrown away.
    virtual EntityId Commit(IToolContext* ctx, const Vec3d& a, const Vec3d& b) = 0;

    /// Whether a committed gesture rolls straight into the next one from the
    /// point it ended at.
    virtual bool Continues() const { return false; }

    bool InProgress() const override { return anchored_; }

    void Reset() override {
        anchored_ = false;
        dragCandidate_ = false;
    }

    bool anchored_{false};
    Vec3d anchor_{0.0, 0.0, 0.0};

private:
    ToolResult Finish(IToolContext* ctx, const Vec3d& p) {
        const EntityId created = Commit(ctx, anchor_, p);
        if (!IsValid(created)) {
            return ToolResult::Handled;  // Degenerate so far; let the user keep going.
        }
        if (Continues()) {
            anchor_ = p;
            dragCandidate_ = false;
        } else {
            Reset();
        }
        ShowPrompt(ctx);
        return ToolResult::Completed;
    }

    bool dragCandidate_{false};
    double pressX_{0.0};
    double pressY_{0.0};
};

class LineTool final : public TwoPointTool {
public:
    explicit LineTool(const ToolSettings& settings) : TwoPointTool(settings) {}

    ToolId GetId() const override { return ToolId::Line; }
    const char* GetName() const override { return "Line"; }

    void BuildPreview(IOverlayBuilder* overlay, IToolContext*) override {
        if (!overlay || !anchored_) {
            return;
        }
        overlay->AddPoint(anchor_, kAnchorColor, kAnchorPixels);
        if (hasCursor_) {
            overlay->AddLine(anchor_, cursor_, kPreviewColor, kPreviewWidth, LineStyle::Solid);
        }
    }

protected:
    const char* Prompt() const override {
        return anchored_ ? "Specify end point" : "Specify start point";
    }

    EntityId Commit(IToolContext* ctx, const Vec3d& a, const Vec3d& b) override {
        if (Distance(a, b) <= WorldSlop(ctx, a)) {
            return kInvalidEntity;
        }
        IGeometryBuilder* builder = BuilderOf(ctx);
        return builder ? builder->MakeLine(a, b) : kInvalidEntity;
    }

    bool Continues() const override { return Settings().continuous; }

private:
    ~LineTool() override = default;
};

class CircleTool final : public TwoPointTool {
public:
    explicit CircleTool(const ToolSettings& settings) : TwoPointTool(settings) {}

    ToolId GetId() const override { return ToolId::Circle; }
    const char* GetName() const override { return "Circle"; }

    void BuildPreview(IOverlayBuilder* overlay, IToolContext* ctx) override {
        if (!overlay || !anchored_ || !ctx) {
            return;
        }
        overlay->AddPoint(anchor_, kAnchorColor, kAnchorPixels);
        if (!hasCursor_) {
            return;
        }
        // The radius line doubles as the dimension the user is dragging, so it
        // is drawn in the "construction" style rather than as part of the shape.
        overlay->AddLine(anchor_, cursor_, kGuideColor, 1.0f, LineStyle::Dashed);

        WorkPlane plane{};
        ctx->GetWorkPlane(plane);
        overlay->AddCircle(Plane{anchor_, plane.normal}, Distance(anchor_, cursor_), kPreviewColor,
                           kPreviewWidth, LineStyle::Solid);
    }

protected:
    const char* Prompt() const override {
        return anchored_ ? "Specify radius" : "Specify centre point";
    }

    EntityId Commit(IToolContext* ctx, const Vec3d& a, const Vec3d& b) override {
        const double radius = Distance(a, b);
        if (radius <= WorldSlop(ctx, a)) {
            return kInvalidEntity;
        }
        WorkPlane plane{};
        ctx->GetWorkPlane(plane);
        IGeometryBuilder* builder = BuilderOf(ctx);
        return builder ? builder->MakeCircle(Plane{a, plane.normal}, radius) : kInvalidEntity;
    }

private:
    ~CircleTool() override = default;
};

class RectangleTool final : public TwoPointTool {
public:
    explicit RectangleTool(const ToolSettings& settings) : TwoPointTool(settings) {}

    ToolId GetId() const override { return ToolId::Rectangle; }
    const char* GetName() const override { return "Rectangle"; }

    void BuildPreview(IOverlayBuilder* overlay, IToolContext* ctx) override {
        if (!overlay || !anchored_ || !ctx) {
            return;
        }
        overlay->AddPoint(anchor_, kAnchorColor, kAnchorPixels);
        if (!hasCursor_) {
            return;
        }
        WorkPlane plane{};
        ctx->GetWorkPlane(plane);

        const Vec2d a = ToPlaneUV(plane, anchor_);
        const Vec2d b = ToPlaneUV(plane, cursor_);
        const Vec3d corners[4] = {
            FromPlaneUV(plane, Vec2d{a.x, a.y}), FromPlaneUV(plane, Vec2d{b.x, a.y}),
            FromPlaneUV(plane, Vec2d{b.x, b.y}), FromPlaneUV(plane, Vec2d{a.x, b.y})};
        overlay->AddPolyline(CgSpan<const Vec3d>{corners, 4}, /*closed=*/true, kPreviewColor,
                             kPreviewWidth, LineStyle::Solid);
    }

protected:
    const char* Prompt() const override {
        return anchored_ ? "Specify opposite corner" : "Specify first corner";
    }

    EntityId Commit(IToolContext* ctx, const Vec3d& a, const Vec3d& b) override {
        WorkPlane plane{};
        ctx->GetWorkPlane(plane);

        const Vec2d uvA = ToPlaneUV(plane, a);
        const Vec2d uvB = ToPlaneUV(plane, b);
        const double width = std::fabs(uvB.x - uvA.x);
        const double height = std::fabs(uvB.y - uvA.y);
        const double slop = WorldSlop(ctx, a);
        if (width <= slop || height <= slop) {
            return kInvalidEntity;  // Still a sliver; wait for a real rectangle.
        }

        const Vec3d centre =
            FromPlaneUV(plane, Vec2d{0.5 * (uvA.x + uvB.x), 0.5 * (uvA.y + uvB.y)});
        IGeometryBuilder* builder = BuilderOf(ctx);
        return builder ? builder->MakeRectangle(Plane{centre, plane.normal}, plane.uAxis, width,
                                                height)
                       : kInvalidEntity;
    }

private:
    ~RectangleTool() override = default;
};

// ---------------------------------------------------------------------------
// Polyline
// ---------------------------------------------------------------------------

class PolylineTool final : public ToolBase {
public:
    explicit PolylineTool(const ToolSettings& settings) : ToolBase(settings) {}

    ToolId GetId() const override { return ToolId::Polyline; }
    const char* GetName() const override { return "Polyline"; }

    ToolResult OnMouseDown(const MouseEvent& e, IToolContext* ctx) override {
        if (!IsLeft(e)) {
            return ToolResult::Ignored;
        }
        Vec3d p{};
        if (!CursorPoint(e, ctx, p)) {
            return ToolResult::Ignored;
        }
        cursor_ = p;
        hasCursor_ = true;

        // A double-click on the last vertex is how every CAD package ends a
        // polyline, so the second click of it must not also add a point.
        if (e.action == MouseAction::DoubleClick && points_.size() >= 2) {
            return Commit(ctx, /*closed=*/false);
        }

        const double slop = WorldSlop(ctx, p);
        if (!points_.empty() && Distance(points_.back(), p) <= slop) {
            return ToolResult::Handled;  // Same vertex twice; ignore the repeat.
        }
        // Landing back on the first vertex closes the loop, which is how a
        // closed profile gets drawn without a separate tool for it.
        if (points_.size() >= 2 && Distance(points_.front(), p) <= slop) {
            return Commit(ctx, /*closed=*/true);
        }

        points_.push_back(p);
        ShowPrompt(ctx);
        return ToolResult::Handled;
    }

    ToolResult OnMouseMove(const MouseEvent& e, IToolContext* ctx) override {
        ToolBase::OnMouseMove(e, ctx);
        return points_.empty() ? ToolResult::Ignored : ToolResult::Handled;
    }

    ToolResult OnKey(const KeyEvent& e, IToolContext* ctx) override {
        if (e.action != KeyAction::Up && e.key == Key_Enter && points_.size() >= 2) {
            return Commit(ctx, /*closed=*/false);
        }
        return ToolBase::OnKey(e, ctx);
    }

    void BuildPreview(IOverlayBuilder* overlay, IToolContext*) override {
        if (!overlay || points_.empty()) {
            return;
        }
        for (const Vec3d& p : points_) {
            overlay->AddPoint(p, kAnchorColor, kAnchorPixels);
        }

        // The committed part plus the segment chasing the cursor, as one
        // polyline so a dashed style would phase continuously through it.
        std::vector<Vec3d> chain = points_;
        if (hasCursor_) {
            chain.push_back(cursor_);
        }
        if (chain.size() >= 2) {
            overlay->AddPolyline(CgSpan<const Vec3d>{chain.data(), chain.size()},
                                 /*closed=*/false, kPreviewColor, kPreviewWidth,
                                 LineStyle::Solid);
        }
    }

protected:
    bool InProgress() const override { return !points_.empty(); }

    void Reset() override { points_.clear(); }

    const char* Prompt() const override {
        return points_.empty() ? "Specify start point"
                               : "Specify next point (Enter or double-click to finish)";
    }

private:
    ~PolylineTool() override = default;

    ToolResult Commit(IToolContext* ctx, bool closed) {
        if (points_.size() >= (closed ? 3u : 2u)) {
            if (IGeometryBuilder* builder = BuilderOf(ctx)) {
                builder->MakePolyline(CgSpan<const Vec3d>{points_.data(), points_.size()}, closed);
            }
        }
        Reset();
        ShowPrompt(ctx);
        return ToolResult::Completed;
    }

    std::vector<Vec3d> points_;
};

} // namespace

void RegisterBuiltinTools(IToolManager& manager, const ToolSettings& settings) {
    manager.RegisterTool(new SelectTool(settings));
    manager.RegisterTool(new PointTool(settings));
    manager.RegisterTool(new LineTool(settings));
    manager.RegisterTool(new CircleTool(settings));
    manager.RegisterTool(new RectangleTool(settings));
    manager.RegisterTool(new PolylineTool(settings));
    CG_DEBUG("registered 6 built-in tools");
}

} // namespace cadgeom::interact
