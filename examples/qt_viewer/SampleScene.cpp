/**
 * @file SampleScene.cpp
 * @brief BuildSampleScene 的实现，照着 glfw_viewer 那张安装板画的。
 */
#include "SampleScene.h"

#include "HostCommands.h"

#include <cadgeom/CadGeomRAII.h>

#include <cmath>

using namespace cadgeom;

namespace {

constexpr double kPi = 3.14159265358979323846;

/// @brief 套一份样式 —— 走命令，不直接 SetStyle。
///
/// IEntity::SetStyle 是直接写的，不进撤销栈。图纸建在一个 UndoGroup 里，如果样式
/// 绕过命令，撤销之后再重做，实体会以**默认样式**回来：线还在，颜色和线型没了。
/// 这也正是 StyleCommand 存在的理由（HostCommands.h）。
void PushStyle(IScene& scene, EntityId entity, Color color, float width, LineStyle lineStyle) {
    if (!IsValid(entity)) {
        return;
    }
    scene.GetCommandStack()->Push(new StyleCommand(
        std::vector<EntityId>{entity},
        [color, width, lineStyle](EntityStyle& style) {
            style.color = color;
            style.lineWidth = width;
            style.lineStyle = lineStyle;
        },
        "Style"));
}

/// @brief 起个名字，同样走命令 —— SetName 也是直接写的。
void PushName(IScene& scene, EntityId entity, const char* name) {
    if (IsValid(entity)) {
        scene.GetCommandStack()->Push(new RenameCommand(entity, name));
    }
}

} // namespace

void BuildSampleScene(IScene& scene) {
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    // 整张图纸收成一个撤销步：用户按一次 Ctrl+Z 期待的是「示例没了」，不是「少了
    // 一个螺栓孔」。组里既有引擎的创建命令，也有上面那两个宿主命令，撤销和重做
    // 因此是对称的。
    UndoGroup group(scene.GetCommandStack(), "Sample scene");

    const Vec3d up{0.0, 0.0, 1.0};
    const Plane xy{Vec3d{0, 0, 0}, up};

    // 颜色是**线性**的（CLAUDE.md：渲染器在线性光里合成，sRGB 编码发生在最后那次
    // blit），所以这些数看起来比屏幕上暗。
    const Color kOutline{0.82f, 0.84f, 0.88f, 1.0f};
    const Color kBore{0.30f, 0.68f, 0.95f, 1.0f};
    const Color kCentre{0.95f, 0.55f, 0.15f, 1.0f};
    const Color kHidden{0.45f, 0.48f, 0.55f, 1.0f};
    const Color kConstruction{0.35f, 0.75f, 0.45f, 1.0f};

    const EntityId outline = builder.MakeRectangle(xy, Vec3d{1, 0, 0}, 120.0, 80.0);
    PushStyle(scene, outline, kOutline, 2.5f, LineStyle::Solid);
    PushName(scene, outline, "Plate outline");

    const EntityId bore = builder.MakeCircle(xy, 22.0);
    PushStyle(scene, bore, kBore, 2.0f, LineStyle::Solid);
    PushName(scene, bore, "Bore");

    // 节圆是构造线，按制图惯例画点画线。
    PushStyle(scene, builder.MakeCircle(xy, 45.0), kHidden, 1.25f, LineStyle::DashDot);

    PushStyle(scene, builder.MakeLine(Vec3d{-70, 0, 0}, Vec3d{70, 0, 0}), kCentre, 1.25f,
              LineStyle::Center);
    PushStyle(scene, builder.MakeLine(Vec3d{0, -50, 0}, Vec3d{0, 50, 0}), kCentre, 1.25f,
              LineStyle::Center);

    for (int i = 0; i < 4; ++i) {
        const double angle = (45.0 + 90.0 * i) * kPi / 180.0;
        const Vec3d centre{45.0 * std::cos(angle), 45.0 * std::sin(angle), 0.0};
        PushStyle(scene, builder.MakeCircle(Plane{centre, up}, 6.0), kBore, 1.75f,
                  LineStyle::Solid);
        PushStyle(scene, builder.MakePoint(centre), kCentre, 7.0f, LineStyle::Solid);
    }

    // 倒角，一条开放多段线。
    const Vec3d chamfer[] = {{-60.0, 28.0, 0.0}, {-52.0, 40.0, 0.0}, {-40.0, 40.0, 0.0}};
    PushStyle(scene, builder.MakePolyline(CgSpan<const Vec3d>{chamfer, 3}, false), kConstruction,
              2.0f, LineStyle::Dashed);

    // 一个真实体：轮廓留在场景里（它就是贴在底面上的那张草图），实体是从它扫出来
    // 的另一个实体，带自己的一份轮廓拷贝。
    const EntityId bossProfile = builder.MakeCircle(Plane{Vec3d{0, 0, 0}, up}, 34.0);
    PushStyle(scene, bossProfile, kConstruction, 1.25f, LineStyle::Dotted);

    ExtrudeOptions options{};
    options.capEnds = true;
    const EntityId boss = builder.Extrude(bossProfile, Vec3d{0, 0, 1}, 18.0, options);
    PushStyle(scene, boss, Color{0.62f, 0.64f, 0.70f, 1.0f}, 1.5f, LineStyle::Solid);
    PushName(scene, boss, "Boss");
}
