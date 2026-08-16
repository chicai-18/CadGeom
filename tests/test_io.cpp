// The built-in file formats (M5, docs/architecture.md §7).
//
// The milestone's acceptance criterion is a round trip that keeps the parametric
// definitions, so that is what most of this file measures: export a drawing,
// read it back, and check that a circle is still a circle you can edit — not a
// pile of triangles that merely looks like one.
//
// Everything here goes through the public headers and the file system. No Vulkan:
// reading a file has nothing to do with drawing one.

#include "TestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace cadgeom;

namespace {

/// A scratch file that deletes itself, so a failed run does not poison the next.
class TempFile {
public:
    explicit TempFile(const char* name) {
        std::filesystem::path directory =
            std::filesystem::temp_directory_path() / "cadgeom_tests";
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        path_ = (directory / name).string();
        Remove();
    }

    ~TempFile() { Remove(); }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    const char* Path() const { return path_.c_str(); }

    /// Companion files an exporter writes beside the main one (OBJ's .mtl).
    std::string Sibling(const char* extension) const {
        std::filesystem::path p(path_);
        p.replace_extension(extension);
        return p.string();
    }

    bool Exists() const { return std::filesystem::exists(path_); }
    uintmax_t Size() const {
        std::error_code ec;
        const uintmax_t size = std::filesystem::file_size(path_, ec);
        return ec ? 0 : size;
    }

private:
    void Remove() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
        std::filesystem::remove(Sibling("mtl"), ec);
    }

    std::string path_;
};

EntityId FindByName(IScene& scene, const char* name) {
    for (uint32_t i = 0; i < scene.GetEntityCount(); ++i) {
        const EntityId id = scene.GetEntityAt(i);
        const IEntity* entity = scene.GetEntity(id);
        if (entity && std::string(entity->GetName()) == name) {
            return id;
        }
    }
    return kInvalidEntity;
}

uint32_t CountOfType(IScene& scene, ShapeType type) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < scene.GetEntityCount(); ++i) {
        const IEntity* entity = scene.GetEntity(scene.GetEntityAt(i));
        if (entity && entity->GetShapeType() == type) {
            ++count;
        }
    }
    return count;
}

/// One of every kind of geometry the engine can make, so a round trip has
/// something to lose. Returns the entity count it added.
uint32_t BuildDrawing(IScene& scene) {
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();
    const Plane xy{Vec3d{0, 0, 0}, Vec3d{0, 0, 1}};

    const EntityId circle = builder.MakeCircle(xy, 25.0);
    builder.MakeRectangle(Plane{Vec3d{60, 0, 0}, Vec3d{0, 0, 1}}, Vec3d{1, 0, 0}, 30.0, 18.0);
    builder.MakeLine(Vec3d{-40, -40, 0}, Vec3d{40, -40, 0});
    builder.MakePoint(Vec3d{0, 40, 12});
    builder.MakeArc(Plane{Vec3d{0, 0, 20}, Vec3d{0, 1, 0}}, 15.0, 0.0, 1.5);

    const Vec3d corners[] = {{-30.0, 30.0, 0.0}, {-10.0, 30.0, 0.0}, {-10.0, 45.0, 0.0}};
    builder.MakePolyline(CgSpan<const Vec3d>{corners, 3}, /*closed=*/true);

    ExtrudeOptions options{};
    builder.Extrude(circle, Vec3d{0, 0, 1}, 30.0, options);
    return scene.GetEntityCount();
}

} // namespace

TEST_CASE("a drawing round-trips through glTF with its parameters intact", "[io]") {
    TempFile file("roundtrip.gltf");

    {
        cgtest::EngineFixture fixture;
        IScene& scene = fixture.Scene();
        REQUIRE(BuildDrawing(scene) == 7);

        ExportOptions options{};
        REQUIRE(CgSucceeded(fixture.Engine().GetIoRegistry()->Export(file.Path(), options, nullptr,
                                                                     nullptr)));
        CHECK(file.Exists());
        CHECK(file.Size() > 0);
    }

    // A second engine, so nothing can survive in memory and pass this by accident.
    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    ImportOptions options{};
    REQUIRE(CgSucceeded(
        fixture.Engine().GetIoRegistry()->Import(file.Path(), options, nullptr, nullptr)));

    // Seven shapes plus the container node the file itself becomes.
    CHECK(scene.GetEntityCount() == 8);
    CHECK(scene.GetRootCount() == 1);

    const EntityId circle = FindByName(scene, "Circle 1");
    REQUIRE(IsValid(circle));
    ShapeParams params{};
    REQUIRE(builder.GetParams(circle, params));
    CHECK(params.type == ShapeType::Circle);
    // Bit-exact, not approximate: the parameters travel as doubles in `extras`,
    // and a CAD file that rounds a 25 mm radius is not a CAD file.
    CHECK(params.circle.radius == 25.0);
    CHECK(params.circle.plane.normal.z == 1.0);

    const EntityId rectangle = FindByName(scene, "Rectangle 1");
    REQUIRE(IsValid(rectangle));
    REQUIRE(builder.GetParams(rectangle, params));
    CHECK(params.type == ShapeType::Rectangle);
    CHECK(params.rectangle.width == 30.0);
    CHECK(params.rectangle.height == 18.0);
    CHECK(params.rectangle.plane.origin.x == 60.0);

    const EntityId arc = FindByName(scene, "Arc 1");
    REQUIRE(IsValid(arc));
    REQUIRE(builder.GetParams(arc, params));
    CHECK(params.type == ShapeType::Arc);
    CHECK(params.arc.radius == 15.0);
    CHECK(params.arc.sweepAngle == 1.5);

    // The solid is the real test: it carries a nested copy of the profile it was
    // swept from, and without that it could not re-sweep at a new height.
    const EntityId solid = FindByName(scene, "Solid 1");
    REQUIRE(IsValid(solid));
    REQUIRE(builder.GetParams(solid, params));
    CHECK(params.type == ShapeType::Solid);
    CHECK(params.extrude.distance == 30.0);

    Aabb bounds{};
    REQUIRE(scene.GetEntity(solid)->GetWorldBounds(bounds));
    // Z-up went out as Y-up and came back Z-up; a wrong swap shows up here as a
    // solid lying on its side.
    CHECK(std::abs(bounds.max.z - 30.0) < 1e-9);
    CHECK(std::abs(bounds.max.x - 25.0) < 0.1);

    params.extrude.distance = 12.0;
    REQUIRE(CgSucceeded(builder.SetParams(solid, params)));
    REQUIRE(scene.GetEntity(solid)->GetWorldBounds(bounds));
    CHECK(std::abs(bounds.max.z - 12.0) < 1e-9);

    // A polyline's points cannot travel in ShapeParams — the union has nowhere to
    // put them — but they can travel in a file, so the round trip keeps what the
    // public getter cannot report.
    const EntityId polyline = FindByName(scene, "Polyline 1");
    REQUIRE(IsValid(polyline));
    CHECK(scene.GetEntity(polyline)->GetShapeType() == ShapeType::Polyline);
    REQUIRE(scene.GetEntity(polyline)->GetWorldBounds(bounds));
    CHECK(std::abs(bounds.min.x + 30.0) < 1e-9);
    CHECK(std::abs(bounds.max.y - 45.0) < 1e-9);

    CHECK(CountOfType(scene, ShapeType::Point) == 1);
    CHECK(CountOfType(scene, ShapeType::Line) == 1);
    // Nothing came back as a plain mesh: every shape was rebuilt from parameters.
    CHECK(CountOfType(scene, ShapeType::Mesh) == 0);
}

TEST_CASE("the same round trip works through a binary .glb", "[io]") {
    TempFile file("roundtrip.glb");

    {
        cgtest::EngineFixture fixture;
        BuildDrawing(fixture.Scene());
        ExportOptions options{};
        REQUIRE(CgSucceeded(fixture.Engine().GetIoRegistry()->Export(file.Path(), options, nullptr,
                                                                     nullptr)));
    }
    // A GLB starts with the four magic bytes "glTF"; the importer sniffs those
    // rather than trusting the extension.
    std::ifstream probe(file.Path(), std::ios::binary);
    char magic[4] = {};
    probe.read(magic, 4);
    CHECK(std::string(magic, 4) == "glTF");
    probe.close();

    cgtest::EngineFixture fixture;
    ImportOptions options{};
    REQUIRE(CgSucceeded(
        fixture.Engine().GetIoRegistry()->Import(file.Path(), options, nullptr, nullptr)));

    ShapeParams params{};
    const EntityId circle = FindByName(fixture.Scene(), "Circle 1");
    REQUIRE(IsValid(circle));
    REQUIRE(fixture.Scene().GetGeometryBuilder()->GetParams(circle, params));
    CHECK(params.circle.radius == 25.0);
}

TEST_CASE("an import is one undo step", "[io]") {
    TempFile file("undo.glb");
    {
        cgtest::EngineFixture fixture;
        BuildDrawing(fixture.Scene());
        ExportOptions options{};
        REQUIRE(CgSucceeded(fixture.Engine().GetIoRegistry()->Export(file.Path(), options, nullptr,
                                                                     nullptr)));
    }

    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    ICommandStack& commands = *scene.GetCommandStack();

    // Something already drawn, so the undo has to stop in the right place.
    scene.GetGeometryBuilder()->MakePoint(Vec3d{0, 0, 0});
    const uint32_t before = scene.GetEntityCount();
    const uint32_t undoBefore = commands.GetUndoCount();

    ImportOptions options{};
    REQUIRE(CgSucceeded(
        fixture.Engine().GetIoRegistry()->Import(file.Path(), options, nullptr, nullptr)));
    CHECK(scene.GetEntityCount() > before);
    CHECK(commands.GetUndoCount() == undoBefore + 1);
    CHECK(std::string(commands.PeekUndoName()).find("Import") == 0);

    REQUIRE(CgSucceeded(commands.Undo()));
    CHECK(scene.GetEntityCount() == before);

    REQUIRE(CgSucceeded(commands.Redo()));
    CHECK(scene.GetEntityCount() > before);
    ShapeParams params{};
    const EntityId circle = FindByName(scene, "Circle 1");
    REQUIRE(IsValid(circle));
    // Redo rebuilt the geometry, not just the entities.
    REQUIRE(scene.GetGeometryBuilder()->GetParams(circle, params));
    CHECK(params.circle.radius == 25.0);
}

TEST_CASE("importing without merging replaces the scene", "[io]") {
    TempFile file("replace.glb");
    {
        cgtest::EngineFixture fixture;
        BuildDrawing(fixture.Scene());
        ExportOptions options{};
        REQUIRE(CgSucceeded(fixture.Engine().GetIoRegistry()->Export(file.Path(), options, nullptr,
                                                                     nullptr)));
    }

    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    const EntityId doomed = scene.GetGeometryBuilder()->MakePoint(Vec3d{100, 100, 100});

    ImportOptions options{};
    options.mergeIntoScene = false;
    REQUIRE(CgSucceeded(
        fixture.Engine().GetIoRegistry()->Import(file.Path(), options, nullptr, nullptr)));

    CHECK_FALSE(scene.Exists(doomed));
    CHECK(IsValid(FindByName(scene, "Circle 1")));
    // Clearing the scene takes the undo stack with it, so this import is not
    // undoable — the documented price of replacing rather than merging.
    CHECK(scene.GetCommandStack()->GetUndoCount() == 1);
}

TEST_CASE("turning the extras off leaves an ordinary mesh", "[io]") {
    TempFile file("plain.gltf");
    {
        cgtest::EngineFixture fixture;
        BuildDrawing(fixture.Scene());
        ExportOptions options{};
        options.writeParametricExtras = false;
        REQUIRE(CgSucceeded(fixture.Engine().GetIoRegistry()->Export(file.Path(), options, nullptr,
                                                                     nullptr)));
    }

    cgtest::EngineFixture fixture;
    ImportOptions options{};
    REQUIRE(CgSucceeded(
        fixture.Engine().GetIoRegistry()->Import(file.Path(), options, nullptr, nullptr)));

    // This is what a file from Blender looks like coming in: triangles, no
    // parameters. The solid is the only entity with triangles to carry.
    CHECK(CountOfType(fixture.Scene(), ShapeType::Mesh) == 1);
    CHECK(CountOfType(fixture.Scene(), ShapeType::Circle) == 0);

    const EntityId mesh = FindByName(fixture.Scene(), "Solid 1");
    REQUIRE(IsValid(mesh));
    Aabb bounds{};
    REQUIRE(fixture.Scene().GetEntity(mesh)->GetWorldBounds(bounds));
    // glTF stores positions as float32 by spec, so a mesh round trip is float
    // accurate — the parametric path above is the one that is exact.
    CHECK(std::abs(bounds.max.z - 30.0) < 1e-4);
}

TEST_CASE("a host can ask the importer to ignore the extras", "[io]") {
    TempFile file("ignore.gltf");
    {
        cgtest::EngineFixture fixture;
        BuildDrawing(fixture.Scene());
        ExportOptions options{};
        REQUIRE(CgSucceeded(fixture.Engine().GetIoRegistry()->Export(file.Path(), options, nullptr,
                                                                     nullptr)));
    }

    cgtest::EngineFixture fixture;
    ImportOptions options{};
    options.readParametricExtras = false;
    REQUIRE(CgSucceeded(
        fixture.Engine().GetIoRegistry()->Import(file.Path(), options, nullptr, nullptr)));

    CHECK(CountOfType(fixture.Scene(), ShapeType::Circle) == 0);
    CHECK(CountOfType(fixture.Scene(), ShapeType::Mesh) == 1);
}

TEST_CASE("OBJ carries the triangles, and says so by what it loses", "[io]") {
    TempFile file("part.obj");

    {
        cgtest::EngineFixture fixture;
        IScene& scene = fixture.Scene();
        const Plane xy{Vec3d{0, 0, 0}, Vec3d{0, 0, 1}};
        const EntityId profile =
            scene.GetGeometryBuilder()->MakeRectangle(xy, Vec3d{1, 0, 0}, 40.0, 20.0);
        ExtrudeOptions extrude{};
        REQUIRE(IsValid(scene.GetGeometryBuilder()->Extrude(profile, Vec3d{0, 0, 1}, 10.0, extrude)));

        ExportOptions options{};
        REQUIRE(CgSucceeded(fixture.Engine().GetIoRegistry()->Export(file.Path(), options, nullptr,
                                                                     nullptr)));
    }
    CHECK(file.Exists());
    CHECK(std::filesystem::exists(file.Sibling("mtl")));

    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    ImportOptions options{};
    REQUIRE(CgSucceeded(
        fixture.Engine().GetIoRegistry()->Import(file.Path(), options, nullptr, nullptr)));

    // A box and the rectangle it was swept from: one mesh, one polyline. OBJ has
    // no place to put "this used to be an extrusion", so it comes back as what
    // the format can hold.
    CHECK(CountOfType(scene, ShapeType::Mesh) == 1);
    CHECK(CountOfType(scene, ShapeType::Polyline) == 1);
    CHECK(CountOfType(scene, ShapeType::Solid) == 0);

    const EntityId box = FindByName(scene, "Solid 1");
    REQUIRE(IsValid(box));
    Aabb bounds{};
    REQUIRE(scene.GetEntity(box)->GetWorldBounds(bounds));
    CHECK(std::abs(bounds.max.x - 20.0) < 1e-6);
    CHECK(std::abs(bounds.min.y + 10.0) < 1e-6);
    CHECK(std::abs(bounds.max.z - 10.0) < 1e-6);
    CHECK(std::abs(bounds.min.z) < 1e-6);
}

TEST_CASE("an imported box keeps the edges and corners of a box", "[io]") {
    TempFile file("box.obj");

    {
        cgtest::EngineFixture fixture;
        IScene& scene = fixture.Scene();
        const Plane xy{Vec3d{0, 0, 0}, Vec3d{0, 0, 1}};
        const EntityId profile =
            scene.GetGeometryBuilder()->MakeRectangle(xy, Vec3d{1, 0, 0}, 40.0, 40.0);
        ExtrudeOptions extrude{};
        scene.GetGeometryBuilder()->Extrude(profile, Vec3d{0, 0, 1}, 40.0, extrude);
        // The profile itself would land as a polyline and confuse the picks below.
        scene.DestroyEntity(profile);

        ExportOptions options{};
        REQUIRE(CgSucceeded(fixture.Engine().GetIoRegistry()->Export(file.Path(), options, nullptr,
                                                                     nullptr)));
    }

    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    ImportOptions options{};
    REQUIRE(CgSucceeded(
        fixture.Engine().GetIoRegistry()->Import(file.Path(), options, nullptr, nullptr)));

    const EntityId box = FindByName(scene, "Solid 1");
    REQUIRE(IsValid(box));
    REQUIRE(scene.GetEntity(box)->GetShapeType() == ShapeType::Mesh);

    // A triangle soup has no edges in it. The importer finds them again by
    // dihedral angle, which is what keeps an imported box readable as a box.
    PickResult hit{};
    REQUIRE(scene.Raycast(Ray{Vec3d{20, 20, 100}, Vec3d{0, 0, -1}}, PickFilter_Vertex, hit));
    CHECK(hit.entity == box);
    CHECK(hit.kind == PickKind::Vertex);
    CHECK(std::abs(hit.point.z - 40.0) < 1e-6);

    // And the face under the cursor is still a face, with its own normal.
    PickResult face{};
    REQUIRE(scene.Raycast(Ray{Vec3d{0, 0, 100}, Vec3d{0, 0, -1}}, PickFilter_Face, face));
    CHECK(face.entity == box);
    CHECK(face.kind == PickKind::Face);
    CHECK(std::abs(face.point.z - 40.0) < 1e-6);
}

TEST_CASE("exporting only the selection takes the selection and nothing else", "[io]") {
    TempFile file("selection.gltf");

    cgtest::EngineFixture fixture;
    IScene& scene = fixture.Scene();
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();
    const Plane xy{Vec3d{0, 0, 0}, Vec3d{0, 0, 1}};
    const EntityId kept = builder.MakeCircle(xy, 25.0);
    builder.MakeCircle(Plane{Vec3d{100, 0, 0}, Vec3d{0, 0, 1}}, 5.0);

    ExportOptions options{};
    options.selectionOnly = true;
    // Nothing selected is not "export everything" — it is nothing to export.
    CHECK(CgFailed(fixture.Engine().GetIoRegistry()->Export(file.Path(), options, nullptr, nullptr)));

    scene.GetSelection()->Set(CgSpan<const EntityId>{&kept, 1});
    REQUIRE(CgSucceeded(
        fixture.Engine().GetIoRegistry()->Export(file.Path(), options, nullptr, nullptr)));

    cgtest::EngineFixture reader;
    ImportOptions importOptions{};
    REQUIRE(CgSucceeded(
        reader.Engine().GetIoRegistry()->Import(file.Path(), importOptions, nullptr, nullptr)));
    CHECK(CountOfType(reader.Scene(), ShapeType::Circle) == 1);
    CHECK(IsValid(FindByName(reader.Scene(), "Circle 1")));
    CHECK_FALSE(IsValid(FindByName(reader.Scene(), "Circle 2")));
}

TEST_CASE("import scaling and the unit round trip", "[io]") {
    TempFile file("scaled.gltf");
    {
        cgtest::EngineFixture fixture;
        fixture.Scene().GetGeometryBuilder()->MakeCircle(Plane{Vec3d{0, 0, 0}, Vec3d{0, 0, 1}},
                                                         10.0);
        ExportOptions options{};
        REQUIRE(CgSucceeded(fixture.Engine().GetIoRegistry()->Export(file.Path(), options, nullptr,
                                                                     nullptr)));
    }

    cgtest::EngineFixture fixture;
    ImportOptions options{};
    options.scaleToModelUnits = 25.4;  // A file drawn in inches, read as millimetres.
    REQUIRE(CgSucceeded(
        fixture.Engine().GetIoRegistry()->Import(file.Path(), options, nullptr, nullptr)));

    const EntityId circle = FindByName(fixture.Scene(), "Circle 1");
    REQUIRE(IsValid(circle));
    Aabb bounds{};
    REQUIRE(fixture.Scene().GetEntity(circle)->GetWorldBounds(bounds));
    // The bounds are the tessellated polygon's, and a chord only touches the
    // circle at its ends — so the extreme is exact on the axis the sampling
    // starts from and a hair short of it elsewhere. 254 = 10 inches in mm.
    CHECK(std::abs(bounds.min.y + 254.0) < 1e-9);
    CHECK(std::abs(bounds.max.x - 254.0) < 0.1);

    // The scale rides on the transform, not on the parameters: the circle is
    // still a 10 mm circle drawn at 25.4:1, which is what a unit change means.
    ShapeParams params{};
    REQUIRE(fixture.Scene().GetGeometryBuilder()->GetParams(circle, params));
    CHECK(params.circle.radius == 10.0);
}

TEST_CASE("a file that is not there, and one that is not a model", "[io]") {
    cgtest::EngineFixture fixture;
    IIoRegistry& io = *fixture.Engine().GetIoRegistry();

    ImportOptions options{};
    const std::string missing =
        (std::filesystem::temp_directory_path() / "cadgeom_tests" / "nope.gltf").string();
    std::error_code ec;
    std::filesystem::remove(missing, ec);
    CHECK(io.Import(missing.c_str(), options, nullptr, nullptr) == CgResult::IoError);

    TempFile garbage("garbage.gltf");
    {
        std::ofstream out(garbage.Path());
        out << "this is not a glTF file, it is a sentence\n";
    }
    CHECK(io.Import(garbage.Path(), options, nullptr, nullptr) == CgResult::ParseError);
    CHECK(fixture.Scene().GetEntityCount() == 0);

    TempFile emptyObj("empty.obj");
    {
        std::ofstream out(emptyObj.Path());
        out << "# nothing but a comment\n";
    }
    CHECK(CgFailed(io.Import(emptyObj.Path(), options, nullptr, nullptr)));
    // A failed import leaves nothing behind — no half-loaded debris, and no
    // stray undo entry to redo it with.
    CHECK(fixture.Scene().GetEntityCount() == 0);
    CHECK(fixture.Scene().GetCommandStack()->GetUndoCount() == 0);
    CHECK(fixture.Scene().GetCommandStack()->GetRedoCount() == 0);
}

TEST_CASE("an empty scene has nothing to export", "[io]") {
    TempFile file("empty.gltf");
    cgtest::EngineFixture fixture;
    ExportOptions options{};
    CHECK(CgFailed(
        fixture.Engine().GetIoRegistry()->Export(file.Path(), options, nullptr, nullptr)));
    CHECK_FALSE(file.Exists());
}

TEST_CASE("the progress callback can cancel an export", "[io]") {
    TempFile file("cancel.obj");
    cgtest::EngineFixture fixture;
    BuildDrawing(fixture.Scene());

    struct Ledger {
        int calls = 0;
    } ledger;

    const auto cancel = [](float, const char*, void* userData) {
        ++static_cast<Ledger*>(userData)->calls;
        return false;
    };

    ExportOptions options{};
    CHECK(CgFailed(
        fixture.Engine().GetIoRegistry()->Export(file.Path(), options, cancel, &ledger)));
    CHECK(ledger.calls == 1);
    CHECK_FALSE(file.Exists());
}
