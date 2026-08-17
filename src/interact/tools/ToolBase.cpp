#include "interact/tools/ToolBase.h"

#include <cadgeom/IScene.h>
#include <cadgeom/ISelection.h>
#include <cadgeom/IViewport.h>

#include "interact/ToolContext.h"

#include <cmath>

namespace cadgeom::interact {

ToolBase::ToolBase(ToolSettings& settings) : settings_(settings) {}

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

bool ToolBase::PickAndSelect(const MouseEvent& e, IToolContext* ctx) {
    IScene* scene = ctx ? ctx->GetScene() : nullptr;
    ISelection* selection = scene ? scene->GetSelection() : nullptr;
    if (!selection) {
        return false;
    }

    IViewport* viewport = ctx->GetViewport();
    const uint32_t filter = viewport ? viewport->GetPickFilter() : PickFilter_All;
    const bool additive = (e.mods & (KeyMod_Ctrl | KeyMod_Shift)) != 0;

    PickResult hit{};
    if (!ctx->PickAt(e.x, e.y, filter, hit)) {
        // 点在空白处就是清空选择 —— 每个 CAD 都是这样，而按住修饰键时用户显然
        // 是在攒一个选择集，一次落空不该把它毁掉。
        if (!additive) {
            selection->Clear();
        }
        return false;
    }

    if (additive) {
        selection->Toggle(hit.entity);
    } else {
        selection->Set(CgSpan<const EntityId>{&hit.entity, 1});
    }
    // 子元素只在恰好选中一个实体时有意义，SetSubElement 自己会把别的情况挡掉。
    selection->SetSubElement(hit.kind, hit.subIndex);
    return true;
}

bool ToolBase::UpdateHover(const MouseEvent& e, IToolContext* ctx) {
    IScene* scene = ctx ? ctx->GetScene() : nullptr;
    ISelection* selection = scene ? scene->GetSelection() : nullptr;
    if (!selection) {
        return false;
    }

    IViewport* viewport = ctx->GetViewport();
    const uint32_t filter = viewport ? viewport->GetPickFilter() : PickFilter_All;

    PickResult hit{};
    const bool found = ctx->PickAt(e.x, e.y, filter, hit);
    selection->SetHovered(found ? hit.entity : kInvalidEntity);
    return found;
}

bool ToolBase::BeyondClickSlop(double x0, double y0, double x1, double y1) {
    return std::fabs(x1 - x0) > kClickSlopPixels || std::fabs(y1 - y0) > kClickSlopPixels;
}

void ToolBase::SetSnapReference(const Vec3d& point) const {
    settings_.snapReference = point;
    settings_.hasSnapReference = true;
}

void ToolBase::ClearSnapReference() const {
    settings_.hasSnapReference = false;
}

void ToolBase::ShowPrompt(IToolContext* ctx) const {
    if (ctx) {
        ctx->SetStatusText(Prompt());
    }
}

} // namespace cadgeom::interact
