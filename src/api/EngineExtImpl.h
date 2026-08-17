#pragma once

#include <cadgeom/IEngineExt.h>

#include "core/ObjectTracker.h"

namespace cadgeom::api {

class EngineImpl;

/// @brief `ICadEngine2` 的实现 —— M6 从扩展槽进来的那一批能力。
///
/// 它是引擎的一个成员，不是一个可以 Release 的对象：`ICadEngine::GetExtension`
/// 返回的是一个借来的指针，随引擎一起活、一起死。这也是扩展槽和普通接口的区别 ——
/// 后者有 Release，前者没有。
///
/// 里面的状态一件都不在这儿：单位、吸附参考点和测量结果都住在
/// `interact::ToolSettings`（工具管理器的成员），因为读它们的是工具。这个类只是
/// 把那份状态接到公开接口上。
class EngineExtImpl final : public ICadEngine2 {
public:
    explicit EngineExtImpl(EngineImpl& engine);
    ~EngineExtImpl() override;

    void SetUnitSettings(const UnitSettings& settings) override;
    void GetUnitSettings(UnitSettings& out) const override;

    double ToDisplayLength(double modelUnits) const override;
    double ToModelLength(double displayValue) const override;

    uint32_t FormatLength(double modelUnits, char* buffer, uint32_t capacity) const override;
    uint32_t FormatAngle(double radians, char* buffer, uint32_t capacity) const override;

    void SetSnapReference(const Vec3d& point) override;
    void ClearSnapReference() override;
    bool GetSnapReference(Vec3d& out) const override;

    const char* GetStatusText(const IViewport* viewport) const override;

    void SetHudVisible(IViewport* viewport, bool visible) override;
    bool IsHudVisible(const IViewport* viewport) const override;

    uint32_t GetSampleCount(const IViewport* viewport) const override;

    bool GetMeasurement(Vec3d& from, Vec3d& to, double& distance) const override;

private:
    core::ObjectTracker tracker_;
    EngineImpl& engine_;
};

} // namespace cadgeom::api
