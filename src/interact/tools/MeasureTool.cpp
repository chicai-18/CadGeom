#include "interact/tools/MeasureTool.h"

#include <cadgeom/ITool.h>

#include "core/Log.h"
#include "core/Units.h"
#include "interact/ToolContext.h"
#include "interact/tools/ToolBase.h"

#include <stdio.h>

namespace cadgeom::interact {
namespace {

/// 标注线的颜色。线性光，屏幕上比这个数看着亮。冷色，和工具预览的琥珀分得开 ——
/// 测量不是在建东西。
constexpr Color kMeasureColor{0.25f, 0.85f, 0.95f, 1.0f};
constexpr Color kReadoutColor{1.00f, 1.00f, 1.00f, 1.0f};

class MeasureTool final : public ToolBase {
public:
    explicit MeasureTool(ToolSettings& settings) : ToolBase(settings) {}

    ToolId GetId() const override { return ToolId::Measure; }
    const char* GetName() const override { return "Measure"; }

    ToolResult OnMouseDown(const MouseEvent& e, IToolContext* ctx) override {
        if (!IsLeft(e)) {
            return ToolResult::Ignored;
        }
        Vec3d p{};
        if (!CursorPoint(e, ctx, p)) {
            return ToolResult::Ignored;
        }
        cursor_ = p;
        hasCursor_ = true;

        if (!anchored_) {
            // 已经量完的那一次到此为止：再点一下就是要量新的了。
            anchor_ = p;
            anchored_ = true;
            complete_ = false;
            // 第一个点同时是垂足吸附的参考点。「这个角到那条边的垂直距离」因此是
            // 点两下的事（docs/architecture.md §6.3）。
            SetSnapReference(anchor_);
            ShowPrompt(ctx);
            return ToolResult::Handled;
        }

        end_ = p;
        complete_ = true;
        anchored_ = false;
        ClearSnapReference();
        Record();
        ShowReadout(ctx);
        return ToolResult::Completed;
    }

    ToolResult OnMouseMove(const MouseEvent& e, IToolContext* ctx) override {
        ToolBase::OnMouseMove(e, ctx);
        if (!anchored_) {
            return ToolResult::Ignored;
        }
        // 拖的过程中读数是活的，松手之前就知道量到了多少。
        end_ = cursor_;
        ShowReadout(ctx);
        return ToolResult::Handled;
    }

    void BuildPreview(IOverlayBuilder* overlay, IToolContext*) override {
        if (!overlay) {
            return;
        }
        const bool live = anchored_ && hasCursor_;
        if (!live && !complete_) {
            return;
        }
        const Vec3d to = live ? cursor_ : end_;

        overlay->AddPoint(anchor_, kMeasureColor, kAnchorPixels);
        overlay->AddPoint(to, kMeasureColor, kAnchorPixels);
        // 虚线：这条线是标注，不是画出来的几何，图上得一眼分得开。
        overlay->AddLine(anchor_, to, kMeasureColor, kPreviewWidth, LineStyle::Dashed);

        char text[128];
        FormatDistance(Distance(anchor_, to), text, sizeof(text));
        // 读数摆在中点旁边，标注线因此不会被数字压住。三个分量不画在图上 ——
        // 一条标注线上挤四个数就没人读得下去了，它们走状态栏（ShowReadout）。
        overlay->AddText((anchor_ + to) * 0.5, text, kReadoutColor);
    }

protected:
    bool InProgress() const override { return anchored_; }

    void Reset() override {
        anchored_ = false;
        complete_ = false;
        ClearSnapReference();
    }

    const char* Prompt() const override {
        return anchored_ ? "Specify second point" : "Specify first point";
    }

private:
    ~MeasureTool() override = default;

    void FormatDistance(double distance, char* buffer, size_t capacity) const {
        core::FormatLength(Settings().units, distance, buffer, static_cast<uint32_t>(capacity));
    }

    void FormatDeltas(const Vec3d& delta, char* buffer, size_t capacity) const {
        char dx[48];
        char dy[48];
        char dz[48];
        const UnitSettings& units = Settings().units;
        core::FormatLength(units, delta.x, dx, sizeof(dx));
        core::FormatLength(units, delta.y, dy, sizeof(dy));
        core::FormatLength(units, delta.z, dz, sizeof(dz));
        snprintf(buffer, capacity, "dX %s  dY %s  dZ %s", dx, dy, dz);
    }

    /// 把结果留在设置里，宿主从 ICadEngine2::GetMeasurement 读得到。
    void Record() {
        ToolSettings& settings = Settings();
        settings.measureFrom = anchor_;
        settings.measureTo = end_;
        settings.measureDistance = Distance(anchor_, end_);
        settings.hasMeasurement = true;
        CG_DEBUG("measured %.6f model units", settings.measureDistance);
    }

    /// 状态栏上的那一行：斜距加三个分量。
    void ShowReadout(IToolContext* ctx) {
        if (!ctx) {
            return;
        }
        char distance[64];
        char deltas[192];
        FormatDistance(Distance(anchor_, end_), distance, sizeof(distance));
        FormatDeltas(end_ - anchor_, deltas, sizeof(deltas));

        char line[288];
        snprintf(line, sizeof(line), "Distance %s   %s", distance, deltas);
        ctx->SetStatusText(line);
    }

    bool anchored_{false};
    bool complete_{false};
    Vec3d anchor_{0.0, 0.0, 0.0};
    Vec3d end_{0.0, 0.0, 0.0};
};

} // namespace

void RegisterMeasureTool(IToolManager& manager, ToolSettings& settings) {
    manager.RegisterTool(new MeasureTool(settings));
    CG_DEBUG("registered the measure tool");
}

} // namespace cadgeom::interact
