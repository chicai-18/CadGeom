// Shared plumbing for the built-in tools (docs/architecture.md §6.2).
//
// Every tool is a small state machine over the same handful of moves: take a
// point off the work plane, hold it, show a rubber band, commit or cancel. What
// differs between them is which points they collect and what they build. This
// class holds the parts that are the same in all of them so each tool file reads
// as its own state machine and nothing else.
#pragma once

#include <cadgeom/ITool.h>

#include "core/Math.h"
#include "core/ObjectTracker.h"

namespace cadgeom::interact {

struct ToolSettings;

/// A press and release within this many pixels is a click, not a drag. It is
/// what lets one tool serve both the click-click and the press-drag-release
/// habits without the user having to choose. Also the radius, converted to world
/// units at the point in question, within which a tool treats two picks as
/// naming the same place.
inline constexpr double kClickSlopPixels = 4.0;

class ToolBase : public ITool {
public:
    // The engine allocates its own tools, so this is symmetric with the host
    // case: whoever made it frees it.
    void Release() override;

    void OnActivate(IToolContext* ctx) override;
    void OnDeactivate(IToolContext* ctx) override;

    ToolResult OnMouseDown(const MouseEvent& e, IToolContext* ctx) override;
    ToolResult OnMouseMove(const MouseEvent& e, IToolContext* ctx) override;
    ToolResult OnMouseUp(const MouseEvent& e, IToolContext* ctx) override;
    ToolResult OnKey(const KeyEvent& e, IToolContext* ctx) override;

    void BuildPreview(IOverlayBuilder* overlay, IToolContext* ctx) override;

    void OnCancel(IToolContext* ctx) override;

protected:
    explicit ToolBase(const ToolSettings& settings);
    ~ToolBase() override;

    /// Colours are linear (the renderer composes in linear light), so these look
    /// considerably brighter on screen than the numbers suggest.
    static constexpr Color kPreviewColor{1.00f, 0.35f, 0.05f, 1.0f};   ///< Warm amber.
    static constexpr Color kAnchorColor{1.00f, 1.00f, 1.00f, 1.0f};
    static constexpr Color kGuideColor{0.35f, 0.45f, 0.60f, 1.0f};     ///< Cool, recedes.
    static constexpr float kPreviewWidth = 1.75f;
    static constexpr float kAnchorPixels = 7.0f;

    /// True while the tool is holding half-built state. Escape consumes the
    /// gesture when there is one and falls through to the viewport otherwise,
    /// so pressing it twice cancels the drawing *and then* leaves the tool —
    /// which is what a user pressing Escape repeatedly is asking for.
    virtual bool InProgress() const { return false; }

    /// Drop any half-built state. Called on cancel, deactivate and after a
    /// commit that ends the gesture.
    virtual void Reset() {}

    /// Prompt for the state the tool is in right now.
    virtual const char* Prompt() const { return ""; }

    /// Where the tool should treat the cursor as being: snapped when a snap
    /// applies, the raw work-plane hit otherwise. False when the cursor is not
    /// pointing at the work plane at all.
    static bool CursorPoint(const MouseEvent& e, IToolContext* ctx, Vec3d& out);

    static bool IsLeft(const MouseEvent& e) { return e.button == MouseButton::Left; }

    static bool BeyondClickSlop(double x0, double y0, double x1, double y1);

    /// Pushes the tool's current prompt to the status bar.
    void ShowPrompt(IToolContext* ctx) const;

    const ToolSettings& Settings() const { return settings_; }

    /// Where the cursor last was on the work plane, kept by OnMouseMove so a
    /// preview can be rebuilt on a frame with no input.
    Vec3d cursor_{0.0, 0.0, 0.0};
    bool hasCursor_{false};

private:
    core::ObjectTracker tracker_;
    const ToolSettings& settings_;
};

} // namespace cadgeom::interact
