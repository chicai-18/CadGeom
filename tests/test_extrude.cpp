// 拉伸成体（docs/architecture.md §3.3）。
//
// 和其余测试一样，全部走 include/cadgeom/ 这层公共契约，一行 Vulkan 都不碰 ——
// 分层存在的意义就是让内核能脱离设备验证。
//
// 贯穿始终的一条线是：参数才是真相。拉伸的产物是一个参数化实体，改高度重新扫掠
// 而不是去动三角形，撤销把整件事原样收回。

#include "TestSupport.h"

#include "core/Math.h"  // Header-only, and the only internal header tests touch.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using Catch::Approx;
using namespace cadgeom;

namespace {

constexpr Plane kXY{Vec3d{0.0, 0.0, 0.0}, Vec3d{0.0, 0.0, 1.0}};

Aabb BoundsOf(IScene& scene, EntityId entity) {
    Aabb bounds{};
    REQUIRE(scene.GetEntity(entity) != nullptr);
    REQUIRE(scene.GetEntity(entity)->GetWorldBounds(bounds));
    return bounds;
}

/// 一个 L 形 —— 凹的，扇形剖分对它无能为力，只有耳切法能三角化。
std::vector<Vec3d> LProfile() {
    return {{0.0, 0.0, 0.0},  {20.0, 0.0, 0.0},  {20.0, 6.0, 0.0},
            {6.0, 6.0, 0.0},  {6.0, 20.0, 0.0},  {0.0, 20.0, 0.0}};
}

} // namespace

TEST_CASE("a circle extrudes into a cylinder", "[extrude]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const EntityId circle = builder.MakeCircle(kXY, 10.0);
    REQUIRE(IsValid(circle));

    ExtrudeOptions options{};
    const EntityId solid = builder.Extrude(circle, Vec3d{0, 0, 1}, 25.0, options);
    REQUIRE(IsValid(solid));
    CHECK(scene.GetEntity(solid)->GetShapeType() == ShapeType::Solid);

    // 底面半径 10、高 25：包围盒就是这么大，误差只来自弦高容差。
    const Aabb bounds = BoundsOf(scene, solid);
    CHECK(bounds.min.x == Approx(-10.0).margin(0.02));
    CHECK(bounds.max.x == Approx(10.0).margin(0.02));
    CHECK(bounds.min.z == Approx(0.0));
    CHECK(bounds.max.z == Approx(25.0));

    SECTION("轮廓留在场景里，没有被吃掉") {
        CHECK(scene.Exists(circle));
        CHECK(scene.GetEntityCount() == 2);
    }

    SECTION("撤销菜单里读到的是这个动作本身") {
        CHECK(std::string(scene.GetCommandStack()->PeekUndoName()) == "Extrude");
    }
}

TEST_CASE("a rectangle extrudes into a box", "[extrude]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const EntityId rect = builder.MakeRectangle(kXY, Vec3d{1, 0, 0}, 40.0, 20.0);
    REQUIRE(IsValid(rect));

    ExtrudeOptions options{};
    const EntityId solid = builder.Extrude(rect, Vec3d{0, 0, 1}, 8.0, options);
    REQUIRE(IsValid(solid));

    const Aabb bounds = BoundsOf(scene, solid);
    CHECK(bounds.min.x == Approx(-20.0));
    CHECK(bounds.max.x == Approx(20.0));
    CHECK(bounds.min.y == Approx(-10.0));
    CHECK(bounds.max.y == Approx(10.0));
    CHECK(bounds.min.z == Approx(0.0));
    CHECK(bounds.max.z == Approx(8.0));
}

TEST_CASE("拉伸方向朝下也照样成立", "[extrude]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const EntityId rect = builder.MakeRectangle(kXY, Vec3d{1, 0, 0}, 10.0, 10.0);
    ExtrudeOptions options{};
    // 逆着轮廓法线扫掠：端面的朝向和侧面的绕序都得跟着翻，否则整个实体是内外反的。
    const EntityId solid = builder.Extrude(rect, Vec3d{0, 0, -1}, 6.0, options);
    REQUIRE(IsValid(solid));

    const Aabb bounds = BoundsOf(scene, solid);
    CHECK(bounds.min.z == Approx(-6.0));
    CHECK(bounds.max.z == Approx(0.0));
}

TEST_CASE("凹轮廓也能拉伸 —— 耳切法不是扇形剖分", "[extrude][profile]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const std::vector<Vec3d> path = LProfile();
    const EntityId poly =
        builder.MakePolyline(CgSpan<const Vec3d>{path.data(), path.size()}, /*closed=*/true);
    REQUIRE(IsValid(poly));

    ExtrudeOptions options{};
    const EntityId solid = builder.Extrude(poly, Vec3d{0, 0, 1}, 5.0, options);
    REQUIRE(IsValid(solid));

    const Aabb bounds = BoundsOf(scene, solid);
    CHECK(bounds.min.x == Approx(0.0));
    CHECK(bounds.max.x == Approx(20.0));
    CHECK(bounds.max.y == Approx(20.0));
    CHECK(bounds.max.z == Approx(5.0));
}

TEST_CASE("拉伸选项各自起作用", "[extrude][options]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    SECTION("双向拉伸关于轮廓平面对称") {
        const EntityId rect = builder.MakeRectangle(kXY, Vec3d{1, 0, 0}, 10.0, 10.0);
        ExtrudeOptions options{};
        options.bothDirections = true;
        const EntityId solid = builder.Extrude(rect, Vec3d{0, 0, 1}, 20.0, options);
        REQUIRE(IsValid(solid));

        const Aabb bounds = BoundsOf(scene, solid);
        CHECK(bounds.min.z == Approx(-10.0));
        CHECK(bounds.max.z == Approx(10.0));
    }

    SECTION("拔模让顶面张开") {
        const EntityId rect = builder.MakeRectangle(kXY, Vec3d{1, 0, 0}, 10.0, 10.0);
        ExtrudeOptions options{};
        options.draftAngle = 45.0 * kDegToRad;  // 走一格高就往外张一格。
        const EntityId solid = builder.Extrude(rect, Vec3d{0, 0, 1}, 4.0, options);
        REQUIRE(IsValid(solid));

        // 底面还是 10 见方，顶面每边各外扩 4：包围盒因此是 18 见方。
        const Aabb bounds = BoundsOf(scene, solid);
        CHECK(bounds.min.x == Approx(-9.0));
        CHECK(bounds.max.x == Approx(9.0));
        CHECK(bounds.max.z == Approx(4.0));
    }

    SECTION("拔模把轮廓收没了就是错误，不是一个奇形怪状的实体") {
        const EntityId rect = builder.MakeRectangle(kXY, Vec3d{1, 0, 0}, 10.0, 10.0);
        ExtrudeOptions options{};
        options.draftAngle = -60.0 * kDegToRad;
        CHECK_FALSE(IsValid(builder.Extrude(rect, Vec3d{0, 0, 1}, 40.0, options)));
        CHECK(CgFailed(fixture.Engine().GetLastError()));
    }

    SECTION("不封端仍然是一圈侧壁") {
        const EntityId circle = builder.MakeCircle(kXY, 6.0);
        ExtrudeOptions options{};
        options.capEnds = false;
        const EntityId shell = builder.Extrude(circle, Vec3d{0, 0, 1}, 3.0, options);
        REQUIRE(IsValid(shell));

        const Aabb bounds = BoundsOf(scene, shell);
        CHECK(bounds.max.x == Approx(6.0).margin(0.02));
        CHECK(bounds.max.z == Approx(3.0));
    }
}

TEST_CASE("拉不出来的东西带着理由被拒绝", "[extrude][validation]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();
    ExtrudeOptions options{};

    const auto refused = [&](EntityId id) {
        CHECK_FALSE(IsValid(id));
        CHECK(CgFailed(fixture.Engine().GetLastError()));
        CHECK(std::string(fixture.Engine().GetLastErrorMessage()).size() > 0);
    };

    const EntityId circle = builder.MakeCircle(kXY, 5.0);

    refused(builder.Extrude(builder.MakePoint(Vec3d{0, 0, 0}), Vec3d{0, 0, 1}, 5.0, options));
    refused(builder.Extrude(builder.MakeLine(Vec3d{0, 0, 0}, Vec3d{5, 0, 0}), Vec3d{0, 0, 1}, 5.0,
                            options));
    refused(builder.Extrude(builder.MakeArc(kXY, 5.0, 0.0, kHalfPi), Vec3d{0, 0, 1}, 5.0, options));

    const Vec3d open[] = {{0, 0, 0}, {5, 0, 0}, {5, 5, 0}};
    refused(builder.Extrude(builder.MakePolyline(CgSpan<const Vec3d>{open, 3}, /*closed=*/false),
                            Vec3d{0, 0, 1}, 5.0, options));

    // 方向躺在轮廓平面里 —— 扫出来的不是实体，是一张自交的纸。
    refused(builder.Extrude(circle, Vec3d{1, 0, 0}, 5.0, options));
    refused(builder.Extrude(circle, Vec3d{0, 0, 0}, 5.0, options));
    refused(builder.Extrude(circle, Vec3d{0, 0, 1}, 0.0, options));
    refused(builder.Extrude(EntityId{9999}, Vec3d{0, 0, 1}, 5.0, options));

    // 不共面的环没有唯一的扫掠平面。
    const Vec3d skew[] = {{0, 0, 0}, {10, 0, 0}, {10, 10, 5}, {0, 10, 0}};
    refused(builder.Extrude(builder.MakePolyline(CgSpan<const Vec3d>{skew, 4}, /*closed=*/true),
                            Vec3d{0, 0, 1}, 5.0, options));
}

TEST_CASE("改拉伸参数会重新扫掠", "[extrude][params]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const EntityId circle = builder.MakeCircle(kXY, 10.0);
    ExtrudeOptions options{};
    const EntityId solid = builder.Extrude(circle, Vec3d{0, 0, 1}, 25.0, options);
    REQUIRE(IsValid(solid));

    ShapeParams params{};
    REQUIRE(builder.GetParams(solid, params));
    CHECK(params.type == ShapeType::Solid);
    CHECK(params.extrude.distance == Approx(25.0));
    CHECK(params.extrude.direction.z == Approx(1.0));
    CHECK(params.extrude.profile == scene.GetEntity(circle)->GetShape());

    params.extrude.distance = 60.0;
    REQUIRE(CgSucceeded(builder.SetParams(solid, params)));
    // 网格是派生缓存：改高度必须把包围盒带走，而不是把旧的三角形留在那儿。
    CHECK(BoundsOf(scene, solid).max.z == Approx(60.0));

    SECTION("而且这一步可以撤销") {
        REQUIRE(CgSucceeded(scene.GetCommandStack()->Undo()));
        CHECK(BoundsOf(scene, solid).max.z == Approx(25.0));
        REQUIRE(CgSucceeded(scene.GetCommandStack()->Redo()));
        CHECK(BoundsOf(scene, solid).max.z == Approx(60.0));
    }
}

TEST_CASE("实体自带一份轮廓，删掉轮廓也活得下去", "[extrude][params]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const EntityId circle = builder.MakeCircle(kXY, 10.0);
    ExtrudeOptions options{};
    const EntityId solid = builder.Extrude(circle, Vec3d{0, 0, 1}, 25.0, options);
    REQUIRE(IsValid(solid));

    REQUIRE(CgSucceeded(scene.DestroyEntity(circle)));
    CHECK_FALSE(scene.Exists(circle));

    // 轮廓的实体没了，实体的参数化定义照样完整：改高度还能重新扫掠。
    ShapeParams params{};
    REQUIRE(builder.GetParams(solid, params));
    params.extrude.distance = 40.0;
    REQUIRE(CgSucceeded(builder.SetParams(solid, params)));
    CHECK(BoundsOf(scene, solid).max.z == Approx(40.0));
}

TEST_CASE("拉伸整个是一步，撤销把它原样收回", "[extrude][commands]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    ICommandStack& stack = *scene.GetCommandStack();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const EntityId rect = builder.MakeRectangle(kXY, Vec3d{1, 0, 0}, 10.0, 10.0);
    ExtrudeOptions options{};
    const EntityId solid = builder.Extrude(rect, Vec3d{0, 0, 1}, 5.0, options);
    REQUIRE(IsValid(solid));
    CHECK(scene.GetEntityCount() == 2);

    REQUIRE(CgSucceeded(stack.Undo()));
    CHECK_FALSE(scene.Exists(solid));
    CHECK(scene.GetEntityCount() == 1);

    REQUIRE(CgSucceeded(stack.Redo()));
    // 同一个 id：别处存着它的东西不能在一次撤销之后指向空气。
    CHECK(scene.Exists(solid));
    CHECK(BoundsOf(scene, solid).max.z == Approx(5.0));
}

TEST_CASE("实体跟着轮廓的变换走", "[extrude][transform]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const EntityId rect = builder.MakeRectangle(kXY, Vec3d{1, 0, 0}, 10.0, 10.0);
    Transform xform{};
    xform.translation = Vec3d{1000.0, 0.0, 0.0};
    scene.GetEntity(rect)->SetLocalTransform(xform);

    ExtrudeOptions options{};
    const EntityId solid = builder.Extrude(rect, Vec3d{0, 0, 1}, 5.0, options);
    REQUIRE(IsValid(solid));

    // 扫掠发生在轮廓的对象空间里，所以实体必须接过轮廓那一份局部变换，否则它会
    // 落在一千个单位以外。
    const Aabb bounds = BoundsOf(scene, solid);
    CHECK(bounds.min.x == Approx(995.0));
    CHECK(bounds.max.x == Approx(1005.0));
    CHECK(bounds.max.z == Approx(5.0));
}

TEST_CASE("面拾取报的是面，不是三角形", "[extrude][picking]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const EntityId rect = builder.MakeRectangle(kXY, Vec3d{1, 0, 0}, 40.0, 40.0);
    ExtrudeOptions options{};
    const EntityId solid = builder.Extrude(rect, Vec3d{0, 0, 1}, 10.0, options);
    REQUIRE(IsValid(solid));

    // 从正上方打一条射线，落在顶面中间 —— 离任何一条边和任何一个角都很远，所以
    // 顶点和边的优先级都轮不上。
    PickResult hit{};
    REQUIRE(scene.Raycast(Ray{Vec3d{0, 0, 100}, Vec3d{0, 0, -1}}, PickFilter_Face, hit));
    CHECK(hit.entity == solid);
    CHECK(hit.kind == PickKind::Face);
    CHECK(hit.point.z == Approx(10.0));
    // 平面面报的是自己那个精确的法线，不是三角形算出来的近似值。
    CHECK(hit.normal.z == Approx(1.0));

    // 一个盒子有六个面：底、顶，加四个侧面。顶面在盒子上是第二个面（底、顶、
    // 侧……），拾取报的应当是这个下标，而不是被打中的那个三角形。
    CHECK(hit.subIndex == 1);

    SECTION("侧面各自是一个面") {
        PickResult side{};
        REQUIRE(scene.Raycast(Ray{Vec3d{100, 0, 5}, Vec3d{-1, 0, 0}}, PickFilter_Face, side));
        CHECK(side.entity == solid);
        CHECK(side.subIndex >= 2);
        CHECK(side.normal.x == Approx(1.0));
    }

    SECTION("圆柱的整个侧面是一个面") {
        const EntityId circle = builder.MakeCircle(Plane{Vec3d{500, 0, 0}, Vec3d{0, 0, 1}}, 10.0);
        const EntityId cylinder = builder.Extrude(circle, Vec3d{0, 0, 1}, 20.0, options);
        REQUIRE(IsValid(cylinder));

        PickResult first{};
        PickResult second{};
        REQUIRE(scene.Raycast(Ray{Vec3d{600, 0, 5}, Vec3d{-1, 0, 0}}, PickFilter_Face, first));
        REQUIRE(scene.Raycast(Ray{Vec3d{600, 0, 15}, Vec3d{-1, 0, 0}}, PickFilter_Face, second));
        CHECK(first.entity == cylinder);
        // 两条射线打中的是不同的三角形，却是同一个面 —— 圆柱侧面是一个光滑面，
        // 不是六十四个各自为政的四边形。
        CHECK(first.subIndex == second.subIndex);
        CHECK(first.subIndex == 2);
    }
}

TEST_CASE("实体的角点可以吸附", "[extrude][snap]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const EntityId rect = builder.MakeRectangle(kXY, Vec3d{1, 0, 0}, 20.0, 20.0);
    ExtrudeOptions options{};
    const EntityId solid = builder.Extrude(rect, Vec3d{0, 0, 1}, 10.0, options);
    REQUIRE(IsValid(solid));
    scene.GetEntity(rect)->SetVisible(false);  // 只留实体，免得吸到轮廓上去。

    // 顶面的一个角。竖直棱是真的棱，所以它的两端都是拓扑顶点。
    PickResult hit{};
    REQUIRE(scene.Raycast(Ray{Vec3d{10, 10, 100}, Vec3d{0, 0, -1}}, PickFilter_All, hit));
    CHECK(hit.entity == solid);
    CHECK(hit.kind == PickKind::Vertex);
    CHECK(hit.point.z == Approx(10.0));
}
