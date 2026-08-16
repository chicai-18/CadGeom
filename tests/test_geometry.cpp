// The geometry kernel through the public builder (docs/architecture.md §3).
//
// Everything here goes through include/cadgeom/, which is what a host sees, and
// none of it touches Vulkan — the layering exists precisely so the kernel can be
// checked without standing up a device.
//
// The recurring theme is that parameters are the source of truth: creation is a
// command, an edit regenerates the geometry rather than patching triangles, and
// undo puts back the same entity id it took away.

#include "TestSupport.h"

#include "core/Math.h"  // Header-only, and the only internal header tests touch.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

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

} // namespace

TEST_CASE("every primitive lands in the scene with its own shape", "[geom]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const EntityId point = builder.MakePoint(Vec3d{1.0, 2.0, 3.0});
    const EntityId line = builder.MakeLine(Vec3d{0, 0, 0}, Vec3d{10, 0, 0});
    const EntityId circle = builder.MakeCircle(kXY, 25.0);
    const EntityId arc = builder.MakeArc(kXY, 8.0, 0.0, kHalfPi);
    const EntityId rect = builder.MakeRectangle(kXY, Vec3d{1, 0, 0}, 40.0, 20.0);

    const Vec3d path[] = {{0, 0, 0}, {5, 0, 0}, {5, 5, 0}};
    const EntityId poly = builder.MakePolyline(CgSpan<const Vec3d>{path, 3}, false);

    REQUIRE(IsValid(point));
    REQUIRE(IsValid(line));
    REQUIRE(IsValid(circle));
    REQUIRE(IsValid(arc));
    REQUIRE(IsValid(rect));
    REQUIRE(IsValid(poly));
    CHECK(scene.GetEntityCount() == 6);

    CHECK(scene.GetEntity(point)->GetShapeType() == ShapeType::Point);
    CHECK(scene.GetEntity(line)->GetShapeType() == ShapeType::Line);
    CHECK(scene.GetEntity(circle)->GetShapeType() == ShapeType::Circle);
    CHECK(scene.GetEntity(arc)->GetShapeType() == ShapeType::Arc);
    CHECK(scene.GetEntity(rect)->GetShapeType() == ShapeType::Rectangle);
    CHECK(scene.GetEntity(poly)->GetShapeType() == ShapeType::Polyline);

    // Each carries its own kernel shape; sharing one would make a parametric
    // edit to any of them silently change the rest.
    CHECK(scene.GetEntity(point)->GetShape() != scene.GetEntity(line)->GetShape());
    CHECK(std::string(scene.GetEntity(circle)->GetName()) == "Circle 1");
}

TEST_CASE("bounds come from the tessellated geometry", "[geom][bounds]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    SECTION("a circle spans its diameter and nothing in the plane normal") {
        const Aabb b = BoundsOf(scene, builder.MakeCircle(kXY, 25.0));
        CHECK(b.min.x == Approx(-25.0).margin(0.05));
        CHECK(b.max.x == Approx(25.0).margin(0.05));
        CHECK(b.min.y == Approx(-25.0).margin(0.05));
        CHECK(b.max.y == Approx(25.0).margin(0.05));
        CHECK(b.min.z == Approx(0.0));
        CHECK(b.max.z == Approx(0.0));
    }

    SECTION("a rectangle is its width by its height, centred on the plane origin") {
        const Aabb b = BoundsOf(scene, builder.MakeRectangle(kXY, Vec3d{1, 0, 0}, 40.0, 20.0));
        CHECK(b.min.x == Approx(-20.0));
        CHECK(b.max.x == Approx(20.0));
        CHECK(b.min.y == Approx(-10.0));
        CHECK(b.max.y == Approx(10.0));
    }

    SECTION("a point bounds itself") {
        const Aabb b = BoundsOf(scene, builder.MakePoint(Vec3d{3.0, -4.0, 5.0}));
        CHECK(b.min.x == Approx(3.0));
        CHECK(b.max.z == Approx(5.0));
    }

    SECTION("a line bounds its endpoints") {
        const Aabb b = BoundsOf(scene, builder.MakeLine(Vec3d{-1, 2, 0}, Vec3d{7, -3, 4}));
        CHECK(b.min.x == Approx(-1.0));
        CHECK(b.max.x == Approx(7.0));
        CHECK(b.min.y == Approx(-3.0));
        CHECK(b.max.z == Approx(4.0));
    }

    SECTION("the entity transform moves the bounds with it") {
        const EntityId circle = builder.MakeCircle(kXY, 10.0);
        Transform xform{};
        xform.translation = Vec3d{1000.0, 0.0, 0.0};
        scene.GetEntity(circle)->SetLocalTransform(xform);

        const Aabb b = BoundsOf(scene, circle);
        CHECK(b.min.x == Approx(990.0).margin(0.05));
        CHECK(b.max.x == Approx(1010.0).margin(0.05));
    }
}

TEST_CASE("scene bounds skip what is not visible", "[geom][bounds]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    builder.MakeCircle(kXY, 5.0);
    const EntityId hidden = builder.MakeCircle(Plane{Vec3d{500, 0, 0}, Vec3d{0, 0, 1}}, 5.0);

    Aabb all{};
    REQUIRE(scene.GetBounds(all));
    CHECK(all.max.x == Approx(505.0).margin(0.05));

    scene.GetEntity(hidden)->SetVisible(false);
    REQUIRE(scene.GetBounds(all));
    CHECK(all.max.x == Approx(5.0).margin(0.05));
}

TEST_CASE("parameters round-trip", "[geom][params]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    SECTION("a circle reports the plane and radius it was built from") {
        const EntityId circle = builder.MakeCircle(Plane{Vec3d{1, 2, 3}, Vec3d{0, 0, 1}}, 7.5);
        ShapeParams params{};
        REQUIRE(builder.GetParams(circle, params));
        CHECK(params.type == ShapeType::Circle);
        CHECK(params.circle.radius == Approx(7.5));
        CHECK(params.circle.plane.origin.x == Approx(1.0));
        CHECK(params.circle.plane.origin.z == Approx(3.0));
    }

    SECTION("a line reports its endpoints") {
        const EntityId line = builder.MakeLine(Vec3d{1, 1, 1}, Vec3d{2, 4, 8});
        ShapeParams params{};
        REQUIRE(builder.GetParams(line, params));
        CHECK(params.type == ShapeType::Line);
        CHECK(params.line.end.z == Approx(8.0));
    }

    SECTION("a group has no parametric shape to report") {
        ShapeParams params{};
        CHECK_FALSE(builder.GetParams(scene.CreateGroup("G", kInvalidEntity), params));
    }

    SECTION("a polyline reports its type and nothing else") {
        // ShapeParams is a frozen POD union with nowhere to put a variable
        // number of points, so the type is all it can honestly carry.
        const Vec3d path[] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}};
        const EntityId poly = builder.MakePolyline(CgSpan<const Vec3d>{path, 3}, true);
        ShapeParams params{};
        REQUIRE(builder.GetParams(poly, params));
        CHECK(params.type == ShapeType::Polyline);
    }

    SECTION("an unknown entity is a handle error, not a silent false") {
        ShapeParams params{};
        CHECK_FALSE(builder.GetParams(EntityId{9999}, params));
        CHECK(fixture.Engine().GetLastError() == CgResult::InvalidHandle);
    }
}

TEST_CASE("editing a parameter regenerates the geometry", "[geom][params]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const EntityId circle = builder.MakeCircle(kXY, 10.0);
    REQUIRE(BoundsOf(scene, circle).max.x == Approx(10.0).margin(0.02));

    ShapeParams params{};
    REQUIRE(builder.GetParams(circle, params));
    params.circle.radius = 40.0;
    REQUIRE(CgSucceeded(builder.SetParams(circle, params)));

    // The mesh is a cache: changing the radius has to move the bounds, not
    // leave the old tessellation behind.
    CHECK(BoundsOf(scene, circle).max.x == Approx(40.0).margin(0.05));

    SECTION("and the edit is undoable") {
        REQUIRE(CgSucceeded(scene.GetCommandStack()->Undo()));
        CHECK(BoundsOf(scene, circle).max.x == Approx(10.0).margin(0.02));
        REQUIRE(CgSucceeded(scene.GetCommandStack()->Redo()));
        CHECK(BoundsOf(scene, circle).max.x == Approx(40.0).margin(0.05));
    }

    SECTION("changing what a shape *is* is refused") {
        ShapeParams other{};
        other.type = ShapeType::Line;
        other.line.start = Vec3d{0, 0, 0};
        other.line.end = Vec3d{1, 0, 0};
        CHECK(builder.SetParams(circle, other) == CgResult::InvalidArgument);
    }

    SECTION("a polyline cannot be edited through ShapeParams") {
        const Vec3d path[] = {{0, 0, 0}, {1, 0, 0}};
        const EntityId poly = builder.MakePolyline(CgSpan<const Vec3d>{path, 2}, false);
        ShapeParams polyParams{};
        REQUIRE(builder.GetParams(poly, polyParams));
        CHECK(builder.SetParams(poly, polyParams) == CgResult::NotSupported);
    }
}

TEST_CASE("degenerate geometry is refused with a reason", "[geom][validation]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const auto refused = [&](EntityId id) {
        CHECK_FALSE(IsValid(id));
        CHECK(fixture.Engine().GetLastError() == CgResult::InvalidArgument);
        CHECK(std::string(fixture.Engine().GetLastErrorMessage()).size() > 0);
    };

    refused(builder.MakeCircle(kXY, 0.0));
    refused(builder.MakeCircle(kXY, -1.0));
    refused(builder.MakeCircle(Plane{Vec3d{0, 0, 0}, Vec3d{0, 0, 0}}, 5.0));
    refused(builder.MakeLine(Vec3d{1, 1, 1}, Vec3d{1, 1, 1}));
    refused(builder.MakeArc(kXY, 5.0, 0.0, 0.0));
    refused(builder.MakeRectangle(kXY, Vec3d{1, 0, 0}, 0.0, 10.0));
    // uAxis parallel to the normal leaves nothing to orient the rectangle with.
    refused(builder.MakeRectangle(kXY, Vec3d{0, 0, 1}, 10.0, 10.0));

    const Vec3d single[] = {{0, 0, 0}};
    refused(builder.MakePolyline(CgSpan<const Vec3d>{single, 1}, false));
    const Vec3d pair[] = {{0, 0, 0}, {1, 0, 0}};
    refused(builder.MakePolyline(CgSpan<const Vec3d>{pair, 2}, /*closed=*/true));

    // Nothing that failed left an entity or a half-built shape behind.
    CHECK(scene.GetEntityCount() == 0);
}

TEST_CASE("creation is undoable and keeps the entity id", "[geom][commands]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    ICommandStack& stack = *scene.GetCommandStack();

    const EntityId circle = scene.GetGeometryBuilder()->MakeCircle(kXY, 12.0);
    REQUIRE(IsValid(circle));
    CHECK(stack.CanUndo());
    CHECK(std::string(stack.PeekUndoName()) == "Create Circle");

    REQUIRE(CgSucceeded(stack.Undo()));
    CHECK(scene.GetEntityCount() == 0);
    CHECK_FALSE(scene.Exists(circle));

    REQUIRE(CgSucceeded(stack.Redo()));
    // The *same* id: anything else holding it — another command, a host's own
    // bookkeeping — would otherwise be left pointing at nothing.
    CHECK(scene.Exists(circle));
    CHECK(scene.GetEntity(circle)->GetShapeType() == ShapeType::Circle);
    CHECK(BoundsOf(scene, circle).max.x == Approx(12.0).margin(0.02));
}

TEST_CASE("deleting is undoable and the geometry survives it", "[geom][commands]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    ICommandStack& stack = *scene.GetCommandStack();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const EntityId parent = builder.MakeCircle(kXY, 5.0);
    const EntityId child = builder.MakeLine(Vec3d{0, 0, 0}, Vec3d{9, 0, 0});
    REQUIRE(CgSucceeded(scene.SetParent(child, parent)));

    const ShapeId childShape = scene.GetEntity(child)->GetShape();

    REQUIRE(CgSucceeded(scene.DestroyEntity(parent)));
    CHECK(scene.GetEntityCount() == 0);

    REQUIRE(CgSucceeded(stack.Undo()));
    CHECK(scene.GetEntityCount() == 2);
    REQUIRE(scene.Exists(child));
    // The same shape, not a rebuilt one: the delete detached it rather than
    // destroying it, which is what makes the round trip lossless.
    CHECK(scene.GetEntity(child)->GetShape() == childShape);
    CHECK(scene.GetEntity(child)->GetParent() == parent);
    CHECK(BoundsOf(scene, child).max.x == Approx(9.0));
}

TEST_CASE("a batch delete reduces to the outermost entities", "[geom][commands]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    const EntityId parent = scene.CreateGroup("Parent", kInvalidEntity);
    const EntityId child = scene.CreateGroup("Child", parent);

    // A selection routinely holds both; capturing the subtree twice would
    // resurrect the child twice on undo.
    const EntityId batch[] = {child, parent};
    REQUIRE(CgSucceeded(scene.DestroyEntities(CgSpan<const EntityId>{batch, 2})));
    CHECK(scene.GetEntityCount() == 0);

    REQUIRE(CgSucceeded(scene.GetCommandStack()->Undo()));
    CHECK(scene.GetEntityCount() == 2);
    CHECK(scene.GetEntity(parent)->GetChildCount() == 1);
}

TEST_CASE("tessellation tolerance controls how closely a curve is followed",
          "[geom][tessellation]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    // An arc sweeping past 180 degrees. Its extreme in one axis falls in the
    // middle of the sweep rather than at an endpoint, so a coarse chord misses
    // it and the bounding box comes out visibly smaller. The measure is the box
    // diagonal, which does not depend on how the plane's reference frame
    // happens to be oriented.
    //
    // Exactly: 100 units either side along one axis, and 100 to -100*cos(20°)
    // along the other, so the true diagonal is hypot(200, 134.20) = 240.85.
    const EntityId arc = builder.MakeArc(kXY, 100.0, 0.0, 200.0 * kDegToRad);
    REQUIRE(IsValid(arc));

    TessParams coarse{};
    coarse.chordTolerance = 50.0;
    coarse.angularTolerance = 45.0 * kDegToRad;
    builder.SetTessParams(coarse);
    const double coarseDiagonal = DiagonalLength(BoundsOf(scene, arc));

    TessParams fine{};
    fine.chordTolerance = 0.001;
    fine.angularTolerance = 1.0 * kDegToRad;
    builder.SetTessParams(fine);
    const double fineDiagonal = DiagonalLength(BoundsOf(scene, arc));

    CHECK(coarseDiagonal < fineDiagonal - 1.0);  // Tightening it followed the curve closer.
    CHECK(fineDiagonal == Approx(240.85).margin(0.2));
}

TEST_CASE("extrude reports the milestone that brings it", "[geom][pending]") {
    cgtest::EngineFixture fixture;
    IGeometryBuilder& builder = *fixture.Scene().GetGeometryBuilder();

    const EntityId profile = builder.MakeCircle(kXY, 10.0);
    REQUIRE(IsValid(profile));

    ExtrudeOptions options{};
    CHECK_FALSE(IsValid(builder.Extrude(profile, Vec3d{0, 0, 1}, 25.0, options)));
    CHECK(fixture.Engine().GetLastError() == CgResult::NotImplemented);
    CHECK(std::string(fixture.Engine().GetLastErrorMessage()).find("M4") != std::string::npos);
}
