// CadGeom — interactive tools (docs/architecture.md §6.2).
//
// A tool is a small state machine fed mouse and key events by a viewport. It
// draws its in-progress geometry into the overlay and produces a command only
// when the user commits, so rubber-band previews never touch the scene and
// never pollute the undo stack.
#ifndef CADGEOM_ITOOL_H
#define CADGEOM_ITOOL_H

#include <cadgeom/Export.h>
#include <cadgeom/Types.h>

namespace cadgeom {

class ICamera;
class ICommand;
class IScene;
class IViewport;

enum class ToolId : int32_t {
    None = 0,
    Select,
    Point,
    Line,
    Circle,
    Rectangle,
    Polyline,
    Extrude,
    Move,
    Rotate,
    Scale,
    Measure,
    Custom = 0x1000,  ///< First id available to host-registered tools.
};

/// What a tool did with an event.
enum class ToolResult : int32_t {
    /// Not interested — the viewport falls through to camera navigation.
    Ignored = 0,
    /// Consumed; the tool is mid-gesture and wants a redraw.
    Handled,
    /// Consumed and the gesture finished. The tool stays active for the next one.
    Completed,
    /// Consumed and the tool wants to be deactivated (back to Select).
    Finished,
};

/// Where a tool draws its preview. Coordinates are world space; the overlay is
/// rebuilt from scratch every frame, so a tool just re-emits its current state.
class CADGEOM_API IOverlayBuilder {
public:
    virtual void AddPoint(const Vec3d& p, const Color& color, float pixelSize) = 0;
    virtual void AddLine(const Vec3d& a, const Vec3d& b, const Color& color, float pixelWidth,
                         LineStyle style) = 0;
    virtual void AddPolyline(CgSpan<const Vec3d> points, bool closed, const Color& color,
                             float pixelWidth, LineStyle style) = 0;
    virtual void AddCircle(const Plane& plane, double radius, const Color& color,
                           float pixelWidth, LineStyle style) = 0;
    /// Screen-anchored text, in viewport pixels. UTF-8.
    virtual void AddText(const Vec3d& worldAnchor, const char* utf8Text, const Color& color) = 0;

protected:
    virtual ~IOverlayBuilder() = default;
};

/// Everything a tool is allowed to reach. Passed by the engine on every
/// callback; never store it past the call.
class CADGEOM_API IToolContext {
public:
    virtual IScene* GetScene() = 0;
    virtual IViewport* GetViewport() = 0;
    virtual ICamera* GetCamera() = 0;

    /// The plane 2D creation drops points onto (§6.1).
    virtual void GetWorkPlane(WorkPlane& out) const = 0;
    virtual void SetWorkPlane(const WorkPlane& plane) = 0;

    /// Mouse pixel -> point on the current work plane. False when the ray is
    /// parallel to the plane.
    virtual bool ScreenToWorkPlane(double x, double y, Vec3d& out) const = 0;

    /// Mouse pixel -> snapped point, honouring the active snap mask. Falls back
    /// to the raw work-plane hit when nothing snaps.
    virtual bool SnapAt(double x, double y, SnapResult& out) const = 0;

    /// Pick under the cursor, respecting the viewport's pick filter.
    virtual bool PickAt(double x, double y, uint32_t pickFilter, PickResult& out) const = 0;

    /// Commit an edit. The context takes ownership of `command`.
    virtual CgResult PushCommand(ICommand* command) = 0;

    /// UTF-8 prompt for the host's status bar ("Specify radius:").
    virtual void SetStatusText(const char* utf8Text) = 0;

protected:
    virtual ~IToolContext() = default;
};

/// Implement this to add a tool of your own, then register it with
/// IToolManager::RegisterTool.
class CADGEOM_API ITool {
public:
    /// Called when the manager drops the tool. A host-implemented tool frees
    /// itself here, keeping allocation and release on the same side.
    virtual void Release() = 0;

    virtual ToolId GetId() const = 0;
    /// UTF-8 display name. Must outlive the tool.
    virtual const char* GetName() const = 0;

    virtual void OnActivate(IToolContext* ctx) = 0;
    virtual void OnDeactivate(IToolContext* ctx) = 0;

    virtual ToolResult OnMouseDown(const MouseEvent& e, IToolContext* ctx) = 0;
    virtual ToolResult OnMouseMove(const MouseEvent& e, IToolContext* ctx) = 0;
    virtual ToolResult OnMouseUp(const MouseEvent& e, IToolContext* ctx) = 0;
    virtual ToolResult OnKey(const KeyEvent& e, IToolContext* ctx) = 0;

    /// Emit the rubber-band preview for the current state. Called every frame
    /// while the tool is active.
    virtual void BuildPreview(IOverlayBuilder* overlay, IToolContext* ctx) = 0;

    /// Escape, or the tool being switched away mid-gesture. Must leave the
    /// scene exactly as it was.
    virtual void OnCancel(IToolContext* ctx) = 0;

protected:
    virtual ~ITool() = default;
};

class CADGEOM_API IToolManager {
public:
    virtual CgResult Activate(ToolId id) = 0;
    virtual ToolId GetActiveTool() const = 0;
    virtual ITool* GetActiveToolInterface() = 0;

    /// Cancels the in-progress gesture and returns to ToolId::Select.
    virtual void Cancel() = 0;

    /// Registers a host-implemented tool under its own GetId(). Ids at or above
    /// ToolId::Custom are reserved for hosts; built-in ids may be overridden to
    /// replace default behaviour. The manager takes ownership and calls
    /// ITool::Release() at shutdown.
    virtual CgResult RegisterTool(ITool* tool) = 0;
    virtual CgResult UnregisterTool(ToolId id) = 0;

    /// Which snap types are live. See SnapType.
    virtual void SetSnapMask(uint32_t snapMask) = 0;
    virtual uint32_t GetSnapMask() const = 0;
    /// Snap search radius in pixels.
    virtual void SetSnapTolerance(double pixels) = 0;

    /// Line/polyline tools keep going after each segment when this is set.
    virtual void SetContinuousMode(bool enabled) = 0;
    virtual bool IsContinuousMode() const = 0;

protected:
    virtual ~IToolManager() = default;
};

} // namespace cadgeom

#endif // CADGEOM_ITOOL_H
