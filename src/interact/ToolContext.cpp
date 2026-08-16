#include "interact/ToolContext.h"

#include <cadgeom/ICamera.h>
#include <cadgeom/ICommandStack.h>
#include <cadgeom/IScene.h>
#include <cadgeom/IViewport.h>

#include "core/Error.h"
#include "core/Log.h"

namespace cadgeom::interact {

ToolContext::ToolContext(IScene& scene, IViewport& viewport, ICamera& camera,
                         const ToolSettings& settings, const ISnapSource* snapSource)
    : scene_(scene),
      viewport_(viewport),
      camera_(camera),
      settings_(settings),
      snapSource_(snapSource) {}

ToolContext::~ToolContext() = default;

IScene* ToolContext::GetScene() {
    return &scene_;
}

IViewport* ToolContext::GetViewport() {
    return &viewport_;
}

ICamera* ToolContext::GetCamera() {
    return &camera_;
}

void ToolContext::GetWorkPlane(WorkPlane& out) const {
    viewport_.GetWorkPlane(out);
}

void ToolContext::SetWorkPlane(const WorkPlane& plane) {
    viewport_.SetWorkPlane(plane);
}

bool ToolContext::ScreenToWorkPlane(double x, double y, Vec3d& out) const {
    WorkPlane plane{};
    viewport_.GetWorkPlane(plane);

    Ray ray{};
    camera_.ScreenToRay(x, y, ray);
    return RayHitsWorkPlane(plane, ray, out);
}

bool ToolContext::SnapAt(double x, double y, SnapResult& out) const {
    WorkPlane plane{};
    viewport_.GetWorkPlane(plane);
    return FindSnap(snapSource_, camera_, plane, x, y, settings_.snapMask,
                    settings_.tolerancePixels, gridSpacing_, out);
}

bool ToolContext::PickAt(double x, double y, uint32_t pickFilter, PickResult& out) const {
    return viewport_.Pick(x, y, pickFilter, out);
}

CgResult ToolContext::PushCommand(ICommand* command) {
    if (!command) {
        return core::SetError(CgResult::InvalidArgument, "PushCommand: command is null");
    }
    return scene_.GetCommandStack()->Push(command);
}

void ToolContext::SetStatusText(const char* utf8Text) {
    const char* text = utf8Text ? utf8Text : "";
    if (status_ == text) {
        return;
    }
    status_ = text;
    // Nothing displays this yet: the status bar arrives with the ImGui panels in
    // M6, and there is deliberately no accessor on IViewport for it until there
    // is something on the other end. Tracing it keeps a tool's state machine
    // visible in the meantime.
    CG_TRACE("tool status: %s", status_.c_str());
}

} // namespace cadgeom::interact
