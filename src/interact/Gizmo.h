/**
 * @file Gizmo.h
 * @brief 平移箭头与旋转圆环（docs/architecture.md §6.5）。
 *
 * Gizmo 只算增量，不碰场景：命中测试、拖拽求解、以及往 OverlayPass 里画自己。
 * 谁被拖、什么时候进撤销栈，是工具的事 —— 拖拽过程只改 transform，松开鼠标才
 * push 一个 Command。
 *
 * 尺寸恒定为屏幕像素：Gizmo 是个界面控件，不是模型的一部分，缩放时它跟着一起
 * 变大变小就没法用了。
 */
#pragma once

#include <cadgeom/ITool.h>
#include <cadgeom/Types.h>

#include "core/Math.h"

namespace cadgeom {
class ICamera;
}

namespace cadgeom::interact {

enum class GizmoMode : int32_t {
    Translate = 0,  ///< 三根轴向箭头 + 三个平面方块
    Rotate,         ///< 三个圆环
    Scale,          ///< 三根轴向标尺 + 原点上的等比手柄
};

/// @brief Gizmo 上可以抓的部位。平面手柄以它的法线轴命名。
enum class GizmoHandle : int32_t {
    None = 0,
    AxisX,
    AxisY,
    AxisZ,
    PlaneYZ,  ///< 法线 = X
    PlaneZX,  ///< 法线 = Y
    PlaneXY,  ///< 法线 = Z
    Uniform,  ///< 只有缩放模式有：抓原点，三轴同时缩放
};

class Gizmo {
public:
    void SetMode(GizmoMode mode) { mode_ = mode; }
    GizmoMode Mode() const { return mode_; }

    void SetOrigin(const Vec3d& origin) { origin_ = origin; }
    const Vec3d& Origin() const { return origin_; }

    /// @brief 光标下的手柄；没抓到任何东西时是 GizmoHandle::None。
    /// @note 平面方块先于轴线判定 —— 它们贴着原点，和三根轴的根部重叠，反过来
    ///       测的话方块永远抓不到。
    GizmoHandle HitTest(const ICamera& camera, double x, double y) const;

    /// @brief 开始一次拖拽。
    /// @return false 表示这个手柄在当前视角下解不稳（拖拽平面几乎与视线平行），
    ///         此时不进入拖拽状态。
    bool BeginDrag(const ICamera& camera, GizmoHandle handle, double x, double y);

    /// @brief 自 BeginDrag 以来的累计增量。
    /// @param translation 平移模式下的世界位移；其余模式下为零。
    /// @param rotation    旋转模式下绕手柄轴、过 Origin() 的旋转；其余模式下为
    ///                    单位四元数。
    /// @param scale       缩放模式下过 Origin() 的各轴倍率；其余模式下为 (1,1,1)。
    /// @return false 表示这一次鼠标位置解不出来（射线与拖拽平面平行），增量保持
    ///         上一次的值，拖拽不中断。
    bool UpdateDrag(const ICamera& camera, double x, double y, Vec3d& translation, Quatd& rotation,
                    Vec3d& scale);

    void EndDrag() { dragging_ = false; }
    bool IsDragging() const { return dragging_; }
    GizmoHandle DragHandle() const { return dragging_ ? handle_ : GizmoHandle::None; }

    /// @brief 把自己画进叠加层。`highlight` 会被画成高亮色。
    void BuildOverlay(IOverlayBuilder& overlay, const ICamera& camera, GizmoHandle highlight) const;

    /// @brief 手柄对应的世界轴。平面手柄给的是它的法线。
    static Vec3d AxisOf(GizmoHandle handle);
    static bool IsPlaneHandle(GizmoHandle handle);

private:
    /// Gizmo 在世界空间里有多大 —— 由恒定的像素尺寸换算而来。
    double WorldSize(const ICamera& camera) const;

    GizmoMode mode_{GizmoMode::Translate};
    Vec3d origin_{0.0, 0.0, 0.0};

    bool dragging_{false};
    GizmoHandle handle_{GizmoHandle::None};

    /// 拖拽起点。轴向拖用参数，平面拖用世界点。
    double startParam_{0.0};
    Vec3d startPoint_{0.0, 0.0, 0.0};

    /// 旋转拖拽：在环平面里量角度用的正交基，加上跨过 ±pi 之后仍然连续的累计角。
    Vec3d refU_{1.0, 0.0, 0.0};
    Vec3d refV_{0.0, 1.0, 0.0};
    double lastAngle_{0.0};
    double totalAngle_{0.0};

    /// 等比缩放：按下时光标离原点有多少像素。倍率就是「现在多远 / 当时多远」——
    /// 屏幕距离之比在正交和透视下都是同一件事，用不着知道自己在哪种投影里。
    double startPixelDistance_{0.0};
};

} // namespace cadgeom::interact
