/**
 * @file test_picking.cpp
 * @brief 射线拾取，以及它解出来的子元素优先级（docs/architecture.md §6.3）。
 *
 * 全部走 IScene::Raycast —— 宿主看到的就是它，IViewport::Pick 也建在它上面，两者
 * 的差别只是容差从哪儿来。所以从场景这一层进去，一次就能覆盖空间索引、窄阶段和
 * 优先级规则。这里没有 Vulkan：拾取在 CPU 上对着内核的细分结果做，这正是分层把
 * 它挡在 render/ 之外的理由。
 */

#include "TestSupport.h"

#include "core/Math.h"  // Header-only, and the only internal header tests touch.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace cadgeom;

namespace {

constexpr Plane kXY{Vec3d{0.0, 0.0, 0.0}, Vec3d{0.0, 0.0, 1.0}};

/// @brief 从上方垂直打向 XY 平面上 (x, y) 的一条射线。
Ray DownAt(double x, double y) {
    return Ray{Vec3d{x, y, 100.0}, Vec3d{0.0, 0.0, -1.0}};
}

} // namespace

TEST_CASE("a ray finds the entity under it", "[pick]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const EntityId line = builder.MakeLine(Vec3d{-50, 0, 0}, Vec3d{50, 0, 0});
    REQUIRE(IsValid(line));

    PickResult hit{};
    REQUIRE(scene.Raycast(DownAt(10.0, 0.0), PickFilter_All, hit));
    CHECK(hit.entity == line);
    CHECK(hit.kind == PickKind::Edge);
    CHECK(hit.point.x == Approx(10.0).margin(1e-6));
    CHECK(hit.point.z == Approx(0.0).margin(1e-6));
    // distance 是沿射线量的，而射线是从 100 高处出发的。
    CHECK(hit.distance == Approx(100.0).margin(1e-6));

    SECTION("and reports nothing where there is nothing") {
        PickResult miss{};
        CHECK_FALSE(scene.Raycast(DownAt(10.0, 40.0), PickFilter_All, miss));
    }

    SECTION("an endpoint outranks the edge it belongs to") {
        // 线段末端处两者都在容差内。§6.3 规定顶点优先 —— 用户点端点就是想抓它。
        PickResult end{};
        REQUIRE(scene.Raycast(DownAt(50.0, 0.0), PickFilter_All, end));
        CHECK(end.kind == PickKind::Vertex);
        CHECK(end.point.x == Approx(50.0).margin(1e-6));
    }

    SECTION("a filter that excludes vertices falls back to the edge") {
        PickResult edge{};
        REQUIRE(scene.Raycast(DownAt(50.0, 0.0), PickFilter_Edge, edge));
        CHECK(edge.kind == PickKind::Edge);
    }

    SECTION("an empty filter asks only which object it is") {
        PickResult whole{};
        REQUIRE(scene.Raycast(DownAt(10.0, 0.0), PickFilter_None, whole));
        CHECK(whole.entity == line);
        CHECK(whole.kind == PickKind::Entity);
    }
}

TEST_CASE("a point primitive is pickable at its position", "[pick]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    // 它的包围盒三个方向都是零厚度，slab 测试和索引外扩都得扛得住这一点。
    const EntityId point = scene.GetGeometryBuilder()->MakePoint(Vec3d{7.0, -3.0, 0.0});
    REQUIRE(IsValid(point));

    PickResult hit{};
    REQUIRE(scene.Raycast(DownAt(7.0, -3.0), PickFilter_All, hit));
    CHECK(hit.entity == point);
    CHECK(hit.kind == PickKind::Vertex);
}

TEST_CASE("picking follows the entity transform", "[pick]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    const EntityId circle = scene.GetGeometryBuilder()->MakeCircle(kXY, 20.0);
    Transform xform{};
    xform.translation = Vec3d{500.0, 0.0, 0.0};
    scene.GetEntity(circle)->SetLocalTransform(xform);

    PickResult moved{};
    REQUIRE(scene.Raycast(DownAt(520.0, 0.0), PickFilter_All, moved));
    CHECK(moved.entity == circle);

    // 老位置上就没有了。索引是跟着 revision 走的，所以挪动实体必须让它失效。
    PickResult stale{};
    CHECK_FALSE(scene.Raycast(DownAt(20.0, 0.0), PickFilter_All, stale));
}

TEST_CASE("a planar curve reports its carrier plane as the hit normal", "[pick]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    // 这个法线就是 SetWorkPlaneFromPick 会采纳的那个，所以在还没有实体可以点面
    // 之前，「点一个圆 → 在它的平面上画」这条路已经通了。
    const Plane yz{Vec3d{0, 0, 0}, Vec3d{1, 0, 0}};
    const EntityId circle = scene.GetGeometryBuilder()->MakeCircle(yz, 30.0);
    REQUIRE(IsValid(circle));

    PickResult hit{};
    REQUIRE(scene.Raycast(Ray{Vec3d{100, 0, 30}, Vec3d{-1, 0, 0}}, PickFilter_All, hit));
    CHECK(hit.entity == circle);
    CHECK(std::abs(hit.normal.x) == Approx(1.0).margin(1e-6));
}

TEST_CASE("picking skips what cannot be selected", "[pick]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const EntityId circle = builder.MakeCircle(kXY, 25.0);
    REQUIRE(IsValid(circle));

    PickResult hit{};
    REQUIRE(scene.Raycast(DownAt(25.0, 0.0), PickFilter_All, hit));

    SECTION("hidden entities") {
        scene.GetEntity(circle)->SetVisible(false);
        CHECK_FALSE(scene.Raycast(DownAt(25.0, 0.0), PickFilter_All, hit));
    }

    SECTION("entities a host marked unselectable") {
        EntityStyle style{};
        scene.GetEntity(circle)->GetStyle(style);
        style.selectable = false;
        scene.GetEntity(circle)->SetStyle(style);
        CHECK_FALSE(scene.Raycast(DownAt(25.0, 0.0), PickFilter_All, hit));
    }

    SECTION("a child hidden by its parent") {
        const EntityId group = scene.CreateGroup("Assembly", kInvalidEntity);
        REQUIRE(CgSucceeded(scene.SetParent(circle, group)));
        scene.GetEntity(group)->SetVisible(false);
        CHECK_FALSE(scene.Raycast(DownAt(25.0, 0.0), PickFilter_All, hit));
    }

    SECTION("and a destroyed entity leaves nothing behind in the index") {
        REQUIRE(CgSucceeded(scene.DestroyEntity(circle)));
        CHECK_FALSE(scene.Raycast(DownAt(25.0, 0.0), PickFilter_All, hit));
        // 撤销会把它按原来的 id 放回来，索引也得跟上。
        REQUIRE(CgSucceeded(scene.GetCommandStack()->Undo()));
        REQUIRE(scene.Raycast(DownAt(25.0, 0.0), PickFilter_All, hit));
        CHECK(hit.entity == circle);
    }
}

TEST_CASE("the nearest of several entities wins", "[pick]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    // 三个沿 Z 叠起来的圆，同一条射线全部穿过。
    builder.MakeCircle(Plane{Vec3d{0, 0, -10}, Vec3d{0, 0, 1}}, 40.0);
    const EntityId top = builder.MakeCircle(Plane{Vec3d{0, 0, 30}, Vec3d{0, 0, 1}}, 40.0);
    builder.MakeCircle(Plane{Vec3d{0, 0, 10}, Vec3d{0, 0, 1}}, 40.0);

    PickResult hit{};
    REQUIRE(scene.Raycast(DownAt(40.0, 0.0), PickFilter_All, hit));
    CHECK(hit.entity == top);
    CHECK(hit.point.z == Approx(30.0).margin(0.5));
}

TEST_CASE("many entities still resolve to the right one", "[pick][bvh]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    // 数量足够走进树里，而不是停在唯一的一个叶子上。
    EntityId target = kInvalidEntity;
    for (int i = 0; i < 200; ++i) {
        const Vec3d centre{static_cast<double>(i) * 10.0, 0.0, 0.0};
        const EntityId id = builder.MakeCircle(Plane{centre, Vec3d{0, 0, 1}}, 3.0);
        if (i == 137) {
            target = id;
        }
    }
    REQUIRE(IsValid(target));
    CHECK(scene.GetEntityCount() == 200);

    PickResult hit{};
    REQUIRE(scene.Raycast(DownAt(1370.0 + 3.0, 0.0), PickFilter_All, hit));
    CHECK(hit.entity == target);
}

TEST_CASE("a pick drives the selection the way a click would", "[pick][selection]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();
    ISelection& selection = *scene.GetSelection();

    const EntityId a = builder.MakeCircle(kXY, 20.0);
    const EntityId b = builder.MakeCircle(Plane{Vec3d{200, 0, 0}, Vec3d{0, 0, 1}}, 20.0);

    PickResult hit{};
    REQUIRE(scene.Raycast(DownAt(20.0, 0.0), PickFilter_All, hit));
    selection.Set(CgSpan<const EntityId>{&hit.entity, 1});
    selection.SetSubElement(hit.kind, hit.subIndex);
    CHECK(selection.GetCount() == 1);
    CHECK(selection.Contains(a));
    CHECK(selection.GetSubElementKind() == hit.kind);

    REQUIRE(scene.Raycast(DownAt(220.0, 0.0), PickFilter_All, hit));
    selection.Toggle(hit.entity);
    CHECK(selection.GetCount() == 2);
    CHECK(selection.Contains(b));

    Aabb bounds{};
    REQUIRE(selection.GetBounds(bounds));
    CHECK(Center(bounds).x == Approx(100.0).margin(0.1));
}
