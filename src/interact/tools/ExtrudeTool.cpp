#include "interact/tools/ExtrudeTool.h"

#include <cadgeom/ICamera.h>
#include <cadgeom/IEntity.h>
#include <cadgeom/IGeometryBuilder.h>
#include <cadgeom/IScene.h>
#include <cadgeom/ISelection.h>

#include "core/Log.h"
#include "geom/Curve.h"
#include "geom/Intersect.h"
#include "interact/ToolContext.h"
#include "interact/tools/ToolBase.h"

namespace cadgeom::interact {
namespace {

IGeometryBuilder* BuilderOf(IToolContext* ctx) {
    IScene* scene = ctx ? ctx->GetScene() : nullptr;
    return scene ? scene->GetGeometryBuilder() : nullptr;
}

/// @brief 一个被选中的轮廓，连同它的扫掠轴。
///
/// 轴取自轮廓自己的承载平面 —— 「在一个面上画个圆再拉出来」这条工作流里，用户
/// 期待的方向就是那个面的法线，不是世界的 Z。
struct ProfileAxis {
    EntityId entity{kInvalidEntity};
    /// 世界空间。
    Vec3d origin{0.0, 0.0, 0.0};
    Vec3d axis{0.0, 0.0, 1.0};
    bool valid{false};
};

/// @brief 判断一个实体能不能当轮廓，顺便把扫掠轴算出来。
///
/// @note 闭合与否只有多段线看不出来 —— ShapeParams 是冻结的 POD union，装不下变长
///       点表（CLAUDE.md 里记过这条），所以这里对多段线一律先接受，真正的判定留给
///       提交时的 Extrude()，它会带着理由失败。
ProfileAxis ResolveProfile(IToolContext* ctx, EntityId entity) {
    ProfileAxis result{};
    IScene* scene = ctx ? ctx->GetScene() : nullptr;
    IEntity* target = scene ? scene->GetEntity(entity) : nullptr;
    IGeometryBuilder* builder = BuilderOf(ctx);
    if (!target || !builder) {
        return result;
    }

    ShapeParams params{};
    if (!builder->GetParams(entity, params)) {
        return result;
    }

    Mat4d world{};
    target->GetWorldTransform(world);

    Vec3d localOrigin{0.0, 0.0, 0.0};
    Vec3d localNormal{0.0, 0.0, 1.0};
    switch (params.type) {
        case ShapeType::Circle:
            localOrigin = params.circle.plane.origin;
            localNormal = params.circle.plane.normal;
            break;
        case ShapeType::Arc:
            if (std::fabs(params.arc.sweepAngle) < kTwoPi - 1e-6) {
                return result;  // 开口的弧围不出一个面。
            }
            localOrigin = params.arc.plane.origin;
            localNormal = params.arc.plane.normal;
            break;
        case ShapeType::Rectangle:
            localOrigin = params.rectangle.plane.origin;
            localNormal = params.rectangle.plane.normal;
            break;
        case ShapeType::Polyline: {
            // 拿不到点表，就退回包围盒中心和当前工作平面的法线。多段线画在工作平面
            // 上是常态，所以这个退路多数时候正好是对的。
            Aabb bounds{};
            if (!target->GetWorldBounds(bounds) || IsEmpty(bounds)) {
                return result;
            }
            WorkPlane plane{};
            ctx->GetWorkPlane(plane);
            result.entity = entity;
            result.origin = Center(bounds);
            result.axis = Normalized(plane.normal);
            result.valid = LengthSq(result.axis) > kEpsilon;
            return result;
        }
        default:
            return result;  // 点、直线、实体、导入网格都围不出一个面。
    }

    result.entity = entity;
    result.origin = TransformPoint(world, localOrigin);
    result.axis = Normalized(TransformDirection(world, localNormal));
    result.valid = LengthSq(result.axis) > kEpsilon;
    return result;
}

/// @brief 选择集里唯一的那个实体；不是恰好一个时返回 kInvalidEntity。
EntityId LoneSelection(IToolContext* ctx) {
    IScene* scene = ctx ? ctx->GetScene() : nullptr;
    const ISelection* selection = scene ? scene->GetSelection() : nullptr;
    return (selection && selection->GetCount() == 1) ? selection->GetAt(0) : kInvalidEntity;
}

class ExtrudeTool final : public ToolBase {
public:
    explicit ExtrudeTool(ToolSettings& settings) : ToolBase(settings) {}

    ToolId GetId() const override { return ToolId::Extrude; }
    const char* GetName() const override { return "Extrude"; }

    void OnActivate(IToolContext* ctx) override {
        ToolBase::OnActivate(ctx);
        // 「选中一个圆，按 E，往上拖」—— 已经选好的东西直接接着用，不必再点一次。
        Adopt(ctx, LoneSelection(ctx));
        ShowPrompt(ctx);
    }

    ToolResult OnMouseDown(const MouseEvent& e, IToolContext* ctx) override {
        if (!IsLeft(e) || !ctx) {
            return ToolResult::Ignored;
        }
        if (anchored_) {
            return Commit(ctx);
        }

        PickAndSelect(e, ctx);
        if (!Adopt(ctx, LoneSelection(ctx))) {
            ShowPrompt(ctx);
            return ToolResult::Handled;
        }
        pressX_ = e.x;
        pressY_ = e.y;
        dragCandidate_ = true;
        MarkHeightOrigin(e, ctx);
        ShowPrompt(ctx);
        return ToolResult::Handled;
    }

    ToolResult OnMouseMove(const MouseEvent& e, IToolContext* ctx) override {
        if (!anchored_) {
            UpdateHover(e, ctx);
            return ToolResult::Ignored;
        }
        if (!hasHeightOrigin_) {
            // 从选择集接过来的轮廓（按 E 之前先选好了东西）还没有起点。第一次移动
            // 就是起点，高度从这一刻起算。
            MarkHeightOrigin(e, ctx);
            return ToolResult::Handled;
        }
        double param = 0.0;
        if (AxisParamAt(e, ctx, param)) {
            height_ = param - heightOrigin_;
        }
        // 解不出来就保持上一次的高度：视线正对着轴时（比如在顶视图里拉一个躺在
        // XY 平面上的圆）射线和轴平行，这时候没有答案，但拖拽不该因此中断。
        return ToolResult::Handled;
    }

    ToolResult OnMouseUp(const MouseEvent& e, IToolContext* ctx) override {
        if (!IsLeft(e) || !anchored_ || !dragCandidate_) {
            return ToolResult::Ignored;
        }
        if (!BeyondClickSlop(pressX_, pressY_, e.x, e.y)) {
            // 是一次点击，不是一次拖拽。留着锚点等第二次点击 —— 同一个手势的另一
            // 种习惯。
            dragCandidate_ = false;
            return ToolResult::Handled;
        }
        return Commit(ctx);
    }

    void BuildPreview(IOverlayBuilder* overlay, IToolContext* ctx) override {
        if (!overlay || !ctx || !anchored_) {
            return;
        }
        const Vec3d offset = profile_.axis * height_;
        const Vec3d top = profile_.origin + offset;

        overlay->AddPoint(profile_.origin, kAnchorColor, kAnchorPixels);
        overlay->AddLine(profile_.origin, top, kGuideColor, 1.0f, LineStyle::Dashed);
        if (std::fabs(height_) <= kEpsilon) {
            return;
        }
        overlay->AddPoint(top, kPreviewColor, kAnchorPixels);
        BuildTopOutline(*overlay, ctx, offset);
    }

protected:
    bool InProgress() const override { return anchored_; }

    void Reset() override {
        profile_ = ProfileAxis{};
        anchored_ = false;
        dragCandidate_ = false;
        hasHeightOrigin_ = false;
        height_ = 0.0;
    }

    const char* Prompt() const override {
        return anchored_ ? "Specify extrusion height"
                         : "Select a closed profile (circle, rectangle or closed polyline)";
    }

private:
    ~ExtrudeTool() override = default;

    /// @brief 认下一个轮廓。@return 它当得了轮廓时为 true。
    bool Adopt(IToolContext* ctx, EntityId entity) {
        if (!IsValid(entity)) {
            return false;
        }
        const ProfileAxis resolved = ResolveProfile(ctx, entity);
        if (!resolved.valid) {
            return false;
        }
        profile_ = resolved;
        anchored_ = true;
        hasHeightOrigin_ = false;
        height_ = 0.0;
        return true;
    }

    /// @brief 把当前光标在轴上的落点定为高度的零点。
    ///
    /// 高度量的是「相对开始拉的那一刻移动了多少」，不是光标在轴上的绝对位置：斜着
    /// 看过去时，点在轮廓边上的那一下本身就投在轴上离零点老远的地方，按绝对值算的
    /// 话，手还没动实体就先窜出去一截了。
    void MarkHeightOrigin(const MouseEvent& e, IToolContext* ctx) {
        heightOrigin_ = 0.0;
        hasHeightOrigin_ = AxisParamAt(e, ctx, heightOrigin_);
        height_ = 0.0;
    }

    /// @brief 光标射线在扫掠轴上的参数。@return 射线与轴接近平行时为 false。
    bool AxisParamAt(const MouseEvent& e, IToolContext* ctx, double& out) const {
        const ICamera* camera = ctx ? ctx->GetCamera() : nullptr;
        if (!camera) {
            return false;
        }
        Ray ray{};
        camera->ScreenToRay(e.x, e.y, ray);
        double onRay = 0.0;
        return geom::ClosestLineLine(profile_.origin, profile_.axis, ray.origin, ray.dir, out,
                                     onRay);
    }

    /// @brief 顶面轮廓 + 竖直棱的预览。
    ///
    /// 圆和矩形能照着参数原样画出来；多段线画不了 —— 它的点表过不了 ShapeParams。
    /// 那种情况就只留高度导引线，等提交之后由实体本身说话。
    void BuildTopOutline(IOverlayBuilder& overlay, IToolContext* ctx, const Vec3d& offset) const {
        IScene* scene = ctx->GetScene();
        IEntity* entity = scene ? scene->GetEntity(profile_.entity) : nullptr;
        IGeometryBuilder* builder = BuilderOf(ctx);
        ShapeParams params{};
        if (!entity || !builder || !builder->GetParams(profile_.entity, params)) {
            return;
        }
        Mat4d world{};
        entity->GetWorldTransform(world);

        switch (params.type) {
            case ShapeType::Circle:
            case ShapeType::Arc: {
                const bool isCircle = params.type == ShapeType::Circle;
                const Plane& plane = isCircle ? params.circle.plane : params.arc.plane;
                const double radius = isCircle ? params.circle.radius : params.arc.radius;
                overlay.AddCircle(Plane{TransformPoint(world, plane.origin) + offset,
                                        TransformDirection(world, plane.normal)},
                                  radius, kPreviewColor, kPreviewWidth, LineStyle::Solid);
                break;
            }

            case ShapeType::Rectangle: {
                Vec3d corners[4];
                geom::RectangleCorners(params.rectangle.plane, params.rectangle.uAxis,
                                       params.rectangle.width, params.rectangle.height, corners);
                Vec3d base[4];
                Vec3d top[4];
                for (int i = 0; i < 4; ++i) {
                    base[i] = TransformPoint(world, corners[i]);
                    top[i] = base[i] + offset;
                }
                overlay.AddPolyline(CgSpan<const Vec3d>{top, 4}, /*closed=*/true, kPreviewColor,
                                    kPreviewWidth, LineStyle::Solid);
                for (int i = 0; i < 4; ++i) {
                    overlay.AddLine(base[i], top[i], kPreviewColor, kPreviewWidth,
                                    LineStyle::Solid);
                }
                break;
            }

            default:
                break;
        }
    }

    ToolResult Commit(IToolContext* ctx) {
        IGeometryBuilder* builder = BuilderOf(ctx);
        if (!ctx || !builder || std::fabs(height_) <= HeightSlop(ctx)) {
            // 还没拉出高度来。手势留着，让用户接着拖。
            return ToolResult::Handled;
        }

        ExtrudeOptions options{};
        const EntityId solid =
            builder->Extrude(profile_.entity, profile_.axis, height_, options);
        if (!IsValid(solid)) {
            // 多段线的闭合性只有到这一步才验得出来。把理由摆到状态栏上，手势保持
            // 原样 —— 用户可以换一个轮廓再来。
            ctx->SetStatusText("That profile cannot be extruded; pick a closed one");
            Reset();
            return ToolResult::Handled;
        }

        // 选中刚拉出来的实体：接着按 M 就能挪它，这是最常见的下一步。
        if (IScene* scene = ctx->GetScene()) {
            scene->GetSelection()->Set(CgSpan<const EntityId>{&solid, 1});
        }
        Reset();
        ShowPrompt(ctx);
        return ToolResult::Completed;
    }

    /// 小于这个高度就当用户还没开始拉。和别的工具一样，按像素折算成世界单位。
    double HeightSlop(IToolContext* ctx) const {
        const ICamera* camera = ctx ? ctx->GetCamera() : nullptr;
        return camera ? camera->GetPixelWorldSize(profile_.origin) * kClickSlopPixels : 0.0;
    }

    ProfileAxis profile_{};
    bool anchored_{false};
    bool dragCandidate_{false};
    double height_{0.0};
    /// 高度的零点，在扫掠轴上的参数。
    double heightOrigin_{0.0};
    bool hasHeightOrigin_{false};
    double pressX_{0.0};
    double pressY_{0.0};
};

} // namespace

void RegisterExtrudeTool(IToolManager& manager, ToolSettings& settings) {
    manager.RegisterTool(new ExtrudeTool(settings));
    CG_DEBUG("registered the extrude tool");
}

} // namespace cadgeom::interact
