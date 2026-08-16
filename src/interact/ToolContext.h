// Everything a tool is allowed to reach (docs/architecture.md §6.2).
//
// One of these per viewport, because half of what a tool asks for — the camera,
// the work plane, what is under the cursor — is a property of the view it is
// being driven from. The other half is engine-global and lives in ToolSettings,
// which the tool manager owns.
#pragma once

#include <cadgeom/ITool.h>

#include "core/ObjectTracker.h"
#include "interact/WorkPlane.h"

#include <string>

namespace cadgeom {
class ICamera;
class IScene;
class IViewport;
}

namespace cadgeom::interact {

/// Engine-global tool state. IToolContext is frozen and does not carry these,
/// and they are set through IToolManager rather than per viewport, so the
/// manager owns one of these and the built-in tools read it.
struct ToolSettings {
    /// Line and polyline tools keep going after each segment.
    bool continuous{false};
    /// Which SnapType bits are live.
    uint32_t snapMask{Snap_Endpoint | Snap_Midpoint | Snap_Center | Snap_Intersection};
    /// Snap search radius, in pixels.
    double tolerancePixels{8.0};
};

class ToolContext final : public IToolContext {
public:
    ToolContext(IScene& scene, IViewport& viewport, ICamera& camera,
                const ToolSettings& settings);
    ~ToolContext() override;

    // -- IToolContext -------------------------------------------------------

    IScene* GetScene() override;
    IViewport* GetViewport() override;
    ICamera* GetCamera() override;

    void GetWorkPlane(WorkPlane& out) const override;
    void SetWorkPlane(const WorkPlane& plane) override;

    bool ScreenToWorkPlane(double x, double y, Vec3d& out) const override;
    bool SnapAt(double x, double y, SnapResult& out) const override;
    bool PickAt(double x, double y, uint32_t pickFilter, PickResult& out) const override;

    CgResult PushCommand(ICommand* command) override;

    void SetStatusText(const char* utf8Text) override;

    // -- Engine-side --------------------------------------------------------

    /// Lattice the grid snap rounds to, in model units. The viewport sets this
    /// every frame from the spacing the grid shader is actually drawing, so
    /// snapping lands on lines the user can see.
    void SetGridSpacing(double spacing) { gridSpacing_ = spacing; }

    /// UTF-8, never null. What the active tool last asked to be shown.
    const char* StatusText() const { return status_.c_str(); }

    const ToolSettings& Settings() const { return settings_; }

private:
    core::ObjectTracker tracker_;

    IScene& scene_;
    IViewport& viewport_;
    ICamera& camera_;
    const ToolSettings& settings_;

    double gridSpacing_{1.0};
    std::string status_;
};

} // namespace cadgeom::interact
