// Where a tool's rubber-band preview goes (docs/architecture.md §6.2).
//
// Preview geometry never enters the scene and never enters the undo stack: the
// tool re-emits it every frame from its current state, and only a commit
// produces a command. That is the whole reason this interface exists separately
// from IGeometryBuilder — the two look similar and mean opposite things.
#pragma once

#include <cadgeom/ITool.h>

#include "core/ObjectTracker.h"
#include "interact/TextStroke.h"
#include "render/Overlay.h"

#include <vector>

namespace cadgeom {
class ICamera;
}

namespace cadgeom::interact {

/// @brief 一段文字相对锚点怎么摆。
enum class TextAlign : int32_t {
    Left = 0,   ///< 锚点在基线左端
    Center,     ///< 锚点在基线中点
    Right,      ///< 锚点在基线右端
};

class OverlayBuilder final : public IOverlayBuilder {
public:
    explicit OverlayBuilder(render::OverlayData& target);
    ~OverlayBuilder() override;

    void AddPoint(const Vec3d& p, const Color& color, float pixelSize) override;
    void AddLine(const Vec3d& a, const Vec3d& b, const Color& color, float pixelWidth,
                 LineStyle style) override;
    void AddPolyline(CgSpan<const Vec3d> points, bool closed, const Color& color, float pixelWidth,
                     LineStyle style) override;
    void AddCircle(const Plane& plane, double radius, const Color& color, float pixelWidth,
                   LineStyle style) override;
    void AddText(const Vec3d& worldAnchor, const char* utf8Text, const Color& color) override;

    /// Tessellation quality for AddCircle. Set from the same TessParams the
    /// kernel uses, so a previewed circle and the circle it becomes have the
    /// same silhouette.
    void SetTessParams(const TessParams& tess) { tess_ = tess; }

    /// @brief 装上相机。文字要它才画得出来：字形是按屏幕像素排的，得知道一个像素
    ///        在锚点那儿有多大、屏幕的右和上在世界里指哪儿。
    /// @note 没装相机时 AddText 静静地什么也不画 —— 一个还没接到视口上的叠加层
    ///       本来就没有「屏幕」可言。
    void SetCamera(const ICamera* camera) { camera_ = camera; }

    /// @brief 文字的大写高度，屏幕像素。
    void SetTextPixelHeight(float pixels) { textPixelHeight_ = pixels > 0.0f ? pixels : 13.0f; }
    float TextPixelHeight() const { return textPixelHeight_; }

    /// @brief 带对齐和像素偏移的文字，引擎自己用（HUD、测量读数）。
    /// @param worldAnchor 锚点，世界坐标。
    /// @param offsetX,offsetY 从锚点算起的屏幕像素偏移，y 向下 —— 和视口的像素
    ///        坐标系一致，标注因此可以「挪到这个点右上方 10 像素」。
    void AddTextAligned(const Vec3d& worldAnchor, double offsetX, double offsetY,
                        const char* utf8Text, const Color& color, float pixelHeight,
                        TextAlign align);

    /// @brief 钉在视口某个像素位置上的文字。HUD 走这条路。
    /// @param pixelX,pixelY 视口像素，左上原点、y 向下。
    /// @return 写不出来（没有相机）时为 false。
    bool AddScreenText(double pixelX, double pixelY, const char* utf8Text, const Color& color,
                       float pixelHeight, TextAlign align = TextAlign::Left);

private:
    /// @brief 把笔画摆到世界空间里去。字形永远正对观察者：文字是界面，不是模型。
    void EmitStrokes(const Vec3d& anchor, double offsetX, double offsetY, const char* utf8Text,
                     const Color& color, float pixelHeight, TextAlign align);

    core::ObjectTracker tracker_;
    render::OverlayData& target_;
    TessParams tess_{};
    const ICamera* camera_{nullptr};
    float textPixelHeight_{13.0f};

    /// BuildStrokeText 的落点，成员而不是局部变量：HUD 每帧要摆好几行字，而这是
    /// 每帧都会重来一遍的路径。
    std::vector<Vec2d> strokePoints_;
    std::vector<StrokeRun> strokeRuns_;
};

} // namespace cadgeom::interact
