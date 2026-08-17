// Everything a tool is allowed to reach (docs/architecture.md §6.2).
//
// One of these per viewport, because half of what a tool asks for — the camera,
// the work plane, what is under the cursor — is a property of the view it is
// being driven from. The other half is engine-global and lives in ToolSettings,
// which the tool manager owns.
#pragma once

#include <cadgeom/ITool.h>

#include "core/ObjectTracker.h"
#include "interact/Snap.h"
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
    uint32_t snapMask{Snap_Endpoint | Snap_Midpoint | Snap_Center | Snap_Intersection |
                      Snap_Perpendicular};
    /// Snap search radius, in pixels.
    double tolerancePixels{8.0};

    /// @brief 垂足吸附的参考点 —— 「上一个点」。
    ///
    /// 住在这里而不是在工具自己身上，是因为 `IToolContext::SnapAt` 冻结的签名里
    /// 传不进它，而这份设置正是工具与引擎共有的那半边状态。内置工具落下第一个点
    /// 时设它，宿主则通过 `ICadEngine2::SetSnapReference` 设
    /// （docs/architecture.md §6.3）。
    Vec3d snapReference{0.0, 0.0, 0.0};
    bool hasSnapReference{false};

    /// 显示单位。Measure 工具的读数和 HUD 都从这里取；它不影响任何几何。
    UnitSettings units{};

    /// @brief 最近一次量完的距离，模型单位。宿主从 ICadEngine2::GetMeasurement
    ///        读它 —— Measure 是唯一一个不改场景的工具，结果没有别的地方可放。
    Vec3d measureFrom{0.0, 0.0, 0.0};
    Vec3d measureTo{0.0, 0.0, 0.0};
    double measureDistance{0.0};
    bool hasMeasurement{false};
};

class ToolContext final : public IToolContext {
public:
    /// @param snapSource 吸附候选点的来源，由 api/ 实现并注入；为 null 时只剩
    ///                   网格吸附。
    ToolContext(IScene& scene, IViewport& viewport, ICamera& camera,
                const ToolSettings& settings, const ISnapSource* snapSource);
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
    const ISnapSource* snapSource_{nullptr};

    double gridSpacing_{1.0};
    std::string status_;
};

} // namespace cadgeom::interact
