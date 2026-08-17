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

    SnapQuery query{};
    query.mask = settings_.snapMask;
    query.tolerancePixels = settings_.tolerancePixels;
    query.gridSpacing = gridSpacing_;
    query.reference = settings_.snapReference;
    query.hasReference = settings_.hasSnapReference;
    return FindSnap(snapSource_, camera_, plane, x, y, query, out);
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
    // 两个去处：引擎自绘的 HUD（视口每帧从 StatusText() 取），以及宿主自己的面板
    // （ICadEngine2::GetStatusText）。IViewport 上没有它的访问器，因为那个接口已经
    // 冻结 —— 扩展槽就是为这种事留的（§2.2 第 3 条）。
    CG_TRACE("tool status: %s", status_.c_str());
}

} // namespace cadgeom::interact
