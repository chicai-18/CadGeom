// Scene graph, entity lifetimes and selection.

#include "TestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using Catch::Approx;
using namespace cadgeom;

TEST_CASE("a fresh scene is empty", "[scene]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    CHECK(scene.GetEntityCount() == 0);
    CHECK(scene.GetRootCount() == 0);
    CHECK(scene.GetEntityAt(0) == kInvalidEntity);
    CHECK_FALSE(scene.Exists(kInvalidEntity));
    CHECK(scene.GetEntity(kInvalidEntity) == nullptr);
    CHECK(scene.GetSelection()->GetCount() == 0);
    CHECK(scene.GetGeometryBuilder() != nullptr);
    CHECK(scene.GetCommandStack() != nullptr);
}

TEST_CASE("entities form a hierarchy", "[scene]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    const EntityId root = scene.CreateGroup("Root", kInvalidEntity);
    const EntityId childA = scene.CreateGroup("A", root);
    const EntityId childB = scene.CreateGroup("B", root);

    REQUIRE(IsValid(root));
    CHECK(scene.GetEntityCount() == 3);
    CHECK(scene.GetRootCount() == 1);
    CHECK(scene.GetRootAt(0) == root);

    IEntity* rootEntity = scene.GetEntity(root);
    REQUIRE(rootEntity != nullptr);
    CHECK(rootEntity->GetChildCount() == 2);
    CHECK(rootEntity->GetChild(0) == childA);
    CHECK(rootEntity->GetChild(1) == childB);
    CHECK(rootEntity->GetChild(99) == kInvalidEntity);
    CHECK(scene.GetEntity(childA)->GetParent() == root);
    CHECK(std::string(rootEntity->GetName()) == "Root");
}

TEST_CASE("ids are never recycled", "[scene][lifetime]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    const EntityId first = scene.CreateGroup("first", kInvalidEntity);
    REQUIRE(CgSucceeded(scene.DestroyEntity(first)));
    const EntityId second = scene.CreateGroup("second", kInvalidEntity);

    // Reusing the id would let a stale handle address someone else's entity —
    // exactly the bug class the monotonic counter exists to rule out.
    CHECK(second != first);
    CHECK_FALSE(scene.Exists(first));
    CHECK(scene.GetEntity(first) == nullptr);
}

TEST_CASE("destroying a parent takes its descendants", "[scene][lifetime]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    const EntityId root = scene.CreateGroup("Root", kInvalidEntity);
    const EntityId mid = scene.CreateGroup("Mid", root);
    const EntityId leaf = scene.CreateGroup("Leaf", mid);
    REQUIRE(scene.GetEntityCount() == 3);

    REQUIRE(CgSucceeded(scene.DestroyEntity(mid)));
    CHECK(scene.GetEntityCount() == 1);
    CHECK(scene.Exists(root));
    CHECK_FALSE(scene.Exists(mid));
    CHECK_FALSE(scene.Exists(leaf));
    CHECK(scene.GetEntity(root)->GetChildCount() == 0);
}

TEST_CASE("destroying an unknown entity reports instead of crashing", "[scene]") {
    cgtest::EngineFixture fixture;
    CHECK(fixture.Scene().DestroyEntity(EntityId{9999}) == CgResult::InvalidHandle);
}

TEST_CASE("a batch delete tolerates ids that vanished mid-way", "[scene]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    const EntityId parent = scene.CreateGroup("Parent", kInvalidEntity);
    const EntityId child = scene.CreateGroup("Child", parent);

    // Deleting a selection that contains both a parent and its child is
    // routine; the child is already gone by the time its turn comes.
    const EntityId batch[] = {parent, child};
    CHECK(CgSucceeded(scene.DestroyEntities(CgSpan<const EntityId>{batch, 2})));
    CHECK(scene.GetEntityCount() == 0);
}

TEST_CASE("creating under a bogus parent fails cleanly", "[scene]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    CHECK_FALSE(IsValid(scene.CreateGroup("orphan", EntityId{4242})));
    CHECK(scene.GetEntityCount() == 0);
    CHECK(fixture.Engine().GetLastError() == CgResult::InvalidHandle);
}

TEST_CASE("reparenting", "[scene]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    const EntityId a = scene.CreateGroup("A", kInvalidEntity);
    const EntityId b = scene.CreateGroup("B", kInvalidEntity);
    const EntityId child = scene.CreateGroup("Child", a);
    REQUIRE(scene.GetRootCount() == 2);

    SECTION("moves the child between parents") {
        REQUIRE(CgSucceeded(scene.SetParent(child, b)));
        CHECK(scene.GetEntity(a)->GetChildCount() == 0);
        CHECK(scene.GetEntity(b)->GetChildCount() == 1);
        CHECK(scene.GetEntity(child)->GetParent() == b);
    }

    SECTION("promotes to a root") {
        REQUIRE(CgSucceeded(scene.SetParent(child, kInvalidEntity)));
        CHECK(scene.GetRootCount() == 3);
        CHECK_FALSE(IsValid(scene.GetEntity(child)->GetParent()));
    }

    SECTION("refuses to make an entity its own ancestor") {
        // Left unchecked this is an infinite loop the next time anything walks
        // the hierarchy.
        CHECK(scene.SetParent(a, child) == CgResult::InvalidArgument);
        CHECK(scene.GetEntity(a)->GetParent() == kInvalidEntity);
        CHECK(scene.SetParent(a, a) == CgResult::InvalidArgument);
    }

    SECTION("refuses an unknown parent") {
        CHECK(scene.SetParent(child, EntityId{777}) == CgResult::InvalidHandle);
        CHECK(scene.GetEntity(child)->GetParent() == a);
    }
}

TEST_CASE("world transforms compose down the hierarchy", "[scene]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    const EntityId parent = scene.CreateGroup("Parent", kInvalidEntity);
    const EntityId child = scene.CreateGroup("Child", parent);

    Transform pt{};
    pt.translation = Vec3d{1000.0, 0.0, 0.0};
    scene.GetEntity(parent)->SetLocalTransform(pt);

    Transform ct{};
    ct.translation = Vec3d{0.0, 50.0, 0.0};
    scene.GetEntity(child)->SetLocalTransform(ct);

    Mat4d world{};
    scene.GetEntity(child)->GetWorldTransform(world);
    CHECK(world.m[12] == Approx(1000.0));
    CHECK(world.m[13] == Approx(50.0));

    // The local transform is what was set, not what was composed.
    Transform readback{};
    scene.GetEntity(child)->GetLocalTransform(readback);
    CHECK(readback.translation.x == Approx(0.0));
    CHECK(readback.translation.y == Approx(50.0));
}

TEST_CASE("style and visibility round-trip", "[scene]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IEntity* e = scene.GetEntity(scene.CreateGroup("Styled", kInvalidEntity));
    REQUIRE(e != nullptr);

    EntityStyle style{};
    style.color = Color{1.0f, 0.0f, 0.0f, 1.0f};
    style.lineWidth = 3.0f;
    style.lineStyle = LineStyle::Dashed;
    e->SetStyle(style);

    EntityStyle readback{};
    e->GetStyle(readback);
    CHECK(readback.color.r == Approx(1.0f));
    CHECK(readback.lineWidth == Approx(3.0f));
    CHECK(readback.lineStyle == LineStyle::Dashed);

    CHECK(e->IsVisible());
    e->SetVisible(false);
    CHECK_FALSE(e->IsVisible());
}

TEST_CASE("the revision counter tracks observable change", "[scene]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    const uint64_t start = scene.GetRevision();
    const EntityId e = scene.CreateGroup("E", kInvalidEntity);
    const uint64_t afterCreate = scene.GetRevision();
    CHECK(afterCreate > start);

    scene.GetEntity(e)->SetName("renamed");
    CHECK(scene.GetRevision() > afterCreate);

    // A query changes nothing, so it must not invalidate anyone's cached frame.
    const uint64_t beforeQuery = scene.GetRevision();
    (void)scene.GetEntityCount();
    (void)scene.GetEntity(e)->GetName();
    CHECK(scene.GetRevision() == beforeQuery);
}

TEST_CASE("Clear empties the scene", "[scene]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    const EntityId a = scene.CreateGroup("A", kInvalidEntity);
    scene.CreateGroup("B", a);
    scene.GetSelection()->Add(a);

    scene.Clear();
    CHECK(scene.GetEntityCount() == 0);
    CHECK(scene.GetRootCount() == 0);
    CHECK(scene.GetSelection()->GetCount() == 0);
    CHECK_FALSE(scene.GetCommandStack()->CanUndo());
}

TEST_CASE("selection", "[scene][selection]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    ISelection& sel = *scene.GetSelection();

    const EntityId a = scene.CreateGroup("A", kInvalidEntity);
    const EntityId b = scene.CreateGroup("B", kInvalidEntity);
    const EntityId c = scene.CreateGroup("C", kInvalidEntity);

    SECTION("add, contains, enumerate in insertion order") {
        sel.Add(a);
        sel.Add(b);
        CHECK(sel.GetCount() == 2);
        CHECK(sel.GetAt(0) == a);
        CHECK(sel.GetAt(1) == b);
        CHECK(sel.GetAt(2) == kInvalidEntity);
        CHECK(sel.Contains(a));
        CHECK_FALSE(sel.Contains(c));
    }

    SECTION("adding twice does not duplicate") {
        sel.Add(a);
        sel.Add(a);
        CHECK(sel.GetCount() == 1);
    }

    SECTION("unknown entities are not selectable") {
        sel.Add(EntityId{31337});
        CHECK(sel.GetCount() == 0);
    }

    SECTION("Set replaces wholesale") {
        sel.Add(a);
        const EntityId batch[] = {b, c};
        sel.Set(CgSpan<const EntityId>{batch, 2});
        CHECK(sel.GetCount() == 2);
        CHECK_FALSE(sel.Contains(a));
        CHECK(sel.Contains(c));
    }

    SECTION("toggle and remove") {
        sel.Toggle(a);
        CHECK(sel.Contains(a));
        sel.Toggle(a);
        CHECK_FALSE(sel.Contains(a));
        sel.Add(b);
        sel.Remove(b);
        CHECK(sel.GetCount() == 0);
    }

    SECTION("destroying a selected entity drops it") {
        sel.Add(a);
        sel.Add(b);
        scene.DestroyEntity(a);
        CHECK(sel.GetCount() == 1);
        CHECK(sel.GetAt(0) == b);
    }

    SECTION("hover is independent of selection") {
        sel.Add(a);
        sel.SetHovered(b);
        CHECK(sel.GetHovered() == b);
        CHECK_FALSE(sel.Contains(b));

        scene.DestroyEntity(b);
        CHECK(sel.GetHovered() == kInvalidEntity);
    }

    SECTION("sub-element selection needs exactly one entity") {
        sel.Add(a);
        sel.SetSubElement(PickKind::Face, 3);
        CHECK(sel.GetSubElementKind() == PickKind::Face);
        CHECK(sel.GetSubElementIndex() == 3);

        // "the third face" means nothing across two entities.
        sel.Add(b);
        CHECK(sel.GetSubElementKind() == PickKind::None);
    }
}

TEST_CASE("unbuilt subsystems report the milestone that brings them", "[scene][pending]") {
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();

    Aabb bounds{};
    CHECK_FALSE(scene.GetBounds(bounds));  // Nothing in the scene to bound.

    PickResult hit{};
    const Ray ray{Vec3d{0, 0, 10}, Vec3d{0, 0, -1}};
    CHECK_FALSE(scene.Raycast(ray, PickFilter_All, hit));
    CHECK(fixture.Engine().GetLastError() == CgResult::NotImplemented);

    // A group is a legitimate entity with no geometry, so it bounds nothing.
    scene.CreateGroup("Empty", kInvalidEntity);
    CHECK_FALSE(scene.GetBounds(bounds));
}

TEST_CASE("tessellation quality round-trips through the builder", "[scene]") {
    cgtest::EngineFixture fixture;
    IGeometryBuilder& builder = *fixture.Scene().GetGeometryBuilder();

    TessParams defaults{};
    builder.GetTessParams(defaults);
    CHECK(defaults.chordTolerance > 0.0);
    CHECK(defaults.angularTolerance > 0.0);

    TessParams fine{};
    fine.chordTolerance = 0.001;
    fine.angularTolerance = 0.05;
    builder.SetTessParams(fine);

    TessParams readback{};
    builder.GetTessParams(readback);
    CHECK(readback.chordTolerance == Approx(0.001));

    // Zero means "leave it alone", not "subdivide forever".
    builder.SetTessParams(TessParams{});
    builder.GetTessParams(readback);
    CHECK(readback.chordTolerance == Approx(0.001));
}
