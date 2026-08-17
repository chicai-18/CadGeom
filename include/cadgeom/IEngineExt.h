/**
 * @file IEngineExt.h
 * @brief M6 的扩展接口 —— 冻结的 vtable 之外，新能力从哪儿进来
 *        （docs/architecture.md §2.2 第 3 条）。
 *
 * 已发布接口只能追加不能改，而 M6 要补的几件事（单位系统、带参考点的吸附、
 * HUD 与状态栏）分别属于引擎、工具和视口 —— 往三个冻结的接口上各挂一批虚函数
 * 是在给自己埋雷。走扩展槽只有一处新增：
 *
 * @code
 * auto* ext = static_cast<cadgeom::ICadEngine2*>(
 *     engine->GetExtension(cadgeom::ExtensionId_Engine2));
 * if (ext) { ext->SetUnitSettings(...); }   // null 表示这个版本没有它
 * @endcode
 *
 * 这个对象归引擎所有，随引擎一起销毁，**不要** Release 它。
 */
#ifndef CADGEOM_IENGINEEXT_H
#define CADGEOM_IENGINEEXT_H

#include <cadgeom/Export.h>
#include <cadgeom/Types.h>

namespace cadgeom {

class IViewport;

/// @brief ICadEngine 的 M6 扩展。用 ExtensionId_Engine2 取。
class CADGEOM_API ICadEngine2 {
public:
    // -- 单位系统 -----------------------------------------------------------

    /// @brief 换显示单位。只影响读数与 HUD，一个顶点都不会动。
    virtual void SetUnitSettings(const UnitSettings& settings) = 0;
    virtual void GetUnitSettings(UnitSettings& out) const = 0;

    /// @brief 模型单位 → 显示单位的数值换算（不带后缀）。
    virtual double ToDisplayLength(double modelUnits) const = 0;
    /// @brief 显示单位 → 模型单位。宿主的输入框走这条路。
    virtual double ToModelLength(double displayValue) const = 0;

    /// @brief 把一个模型单位的长度格式化成 UTF-8 显示字符串，如 "12.50 mm"。
    /// @param buffer   目标缓冲；即使容量不够也会写入一个 '\\0' 结尾的截断结果。
    /// @param capacity buffer 的字节数，含结尾的 '\\0'。
    /// @return 写入的字节数，不含结尾的 '\\0'；`buffer` 为 null 或 `capacity`
    ///         为 0 时返回 0。
    virtual uint32_t FormatLength(double modelUnits, char* buffer, uint32_t capacity) const = 0;
    /// @brief 同上，但输入是弧度，按 UnitSettings::angleUnit 输出。
    virtual uint32_t FormatAngle(double radians, char* buffer, uint32_t capacity) const = 0;

    // -- 吸附参考点 ---------------------------------------------------------
    //
    // 垂足吸附是相对「上一个点」说的，而 IToolContext::SnapAt 的签名里没有那个
    // 参数、也不能再加。把参考点变成引擎的一份状态就绕开了这件事：内置工具在落
    // 下第一个点时设它，宿主写的工具照样可以，而 SnapAt 的签名一个字都不用改。

    /// @brief 设定垂足吸附的参考点，并让 Snap_Perpendicular 开始生效。
    virtual void SetSnapReference(const Vec3d& point) = 0;
    /// @brief 清掉参考点。之后 Snap_Perpendicular 不再产生候选点。
    virtual void ClearSnapReference() = 0;
    /// @return 当前有参考点时为 true，并写入 `out`。
    virtual bool GetSnapReference(Vec3d& out) const = 0;

    // -- 视口附加 -----------------------------------------------------------

    /// @brief 当前工具写给状态栏的提示，UTF-8，永不为 null。
    /// @param viewport 提示是跟着视口走的（工具的上下文属于某个视口）；传 null
    ///                 取最近一次派发过事件的那个视口。
    /// @note 指针在下一次工具改写状态之前有效。宿主自己的面板（ImGui、Qt）就是
    ///       靠它把「Specify radius:」显示出来的。
    virtual const char* GetStatusText(const IViewport* viewport) const = 0;

    /// @brief 引擎自绘的 HUD 开关（状态行、工具名、显示模式、网格间距）。
    /// @param viewport 传 null 表示所有视口 —— 「把 HUD 关掉」通常说的是整个应用。
    /// @note 它用的是叠加层里的笔画字体，不是 UI 库 —— 核心库不带 UI 依赖
    ///       （docs/architecture.md §0.1）。宿主要做真正的面板，用
    ///       GetStatusText() 自己画，然后把这个关掉。
    virtual void SetHudVisible(IViewport* viewport, bool visible) = 0;
    /// @param viewport 传 null 取第一个视口；一个视口都没有时返回 false。
    virtual bool IsHudVisible(const IViewport* viewport) const = 0;

    /// @brief 视口实际拿到的 MSAA 采样数。
    /// @return 1 表示没开；设备不支持时 ViewportDesc::sampleCount 会被向下取到
    ///         最近的可用值，所以这个数未必等于宿主要的那个。
    virtual uint32_t GetSampleCount(const IViewport* viewport) const = 0;

    // -- 测量（ToolId::Measure）---------------------------------------------

    /// @brief 最近一次完成的测量。
    /// @param from,to  两个端点，世界坐标。
    /// @param distance 两点距离，模型单位。
    /// @return 还没量过任何东西时为 false。
    virtual bool GetMeasurement(Vec3d& from, Vec3d& to, double& distance) const = 0;

protected:
    virtual ~ICadEngine2() = default;
};

} // namespace cadgeom

#endif // CADGEOM_IENGINEEXT_H
