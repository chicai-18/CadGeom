#include "interact/tools/ToolBase.h"

#include "interact/ToolContext.h"

#include <cmath>

namespace cadgeom::interact {

ToolBase::ToolBase(const ToolSettings& settings) : settings_(settings) {}

ToolBase::~ToolBase() = default;

void ToolBase::Release() {
    delete this;
}

void ToolBase::OnActivate(IToolContext* ctx) {
    Reset();
    ShowPrompt(ctx);
}

void ToolBase::OnDeactivate(IToolContext* ctx) {
    // Switching away mid-gesture must leave the scene exactly as it was, which
    // for these tools means dropping the preview and nothing else — no
    // half-built geometry ever reached the scene to begin with.
    Reset();
    if (ctx) {
        ctx->SetStatusText("");
    }
}

void ToolBase::OnCancel(IToolContext* ctx) {
    Reset();
    ShowPrompt(ctx);
}

ToolResult ToolBase::OnMouseDown(const MouseEvent&, IToolContext*) {
    return ToolResult::Ignored;
}

ToolResult ToolBase::OnMouseMove(const MouseEvent& e, IToolContext* ctx) {
    hasCursor_ = CursorPoint(e, ctx, cursor_);
    // Not Handled: a tool that only tracks the cursor has no claim on the event,
    // and saying otherwise would starve whatever else wants to see mouse moves.
    return ToolResult::Ignored;
}

ToolResult ToolBase::OnMouseUp(const MouseEvent&, IToolContext*) {
    return ToolResult::Ignored;
}

ToolResult ToolBase::OnKey(const KeyEvent& e, IToolContext* ctx) {
    if (e.action != KeyAction::Up && e.key == Key_Escape && InProgress()) {
        OnCancel(ctx);
        return ToolResult::Handled;
    }
    // An idle tool declines Escape so it reaches the viewport, which switches
    // back to Select. Consuming it unconditionally would trap the user in
    // whatever tool they last picked.
    return ToolResult::Ignored;
}

void ToolBase::BuildPreview(IOverlayBuilder*, IToolContext*) {}

bool ToolBase::CursorPoint(const MouseEvent& e, IToolContext* ctx, Vec3d& out) {
    if (!ctx) {
        return false;
    }
    SnapResult snap{};
    if (ctx->SnapAt(e.x, e.y, snap)) {
        out = snap.point;
        return true;
    }
    // SnapAt already falls back to the raw work-plane hit, so failing here means
    // the cursor genuinely is not pointing at the plane — edge-on, or behind the
    // camera. There is no sensible point to report.
    return false;
}

bool ToolBase::BeyondClickSlop(double x0, double y0, double x1, double y1) {
    return std::fabs(x1 - x0) > kClickSlopPixels || std::fabs(y1 - y0) > kClickSlopPixels;
}

void ToolBase::ShowPrompt(IToolContext* ctx) const {
    if (ctx) {
        ctx->SetStatusText(Prompt());
    }
}

} // namespace cadgeom::interact
