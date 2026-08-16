// CadGeom demo host — milestones M0 through M2.
//
// Two phases, deliberately separate:
//
//   1. A pass over the public surface with no GPU in sight: the scene graph, the
//      geometry kernel, undo. This is M0's acceptance test — create, exercise,
//      release, prove nothing leaked — and it is bracketed by a CRT heap
//      checkpoint, so it has to stay free of anything that allocates behind the
//      engine's back.
//   2. A viewport: a window, a grid, an orbit camera, a drawing made of points,
//      lines, circles and rectangles, and the tools that create them. Run with
//      --headless it renders off-screen and writes a PNG instead, which is how
//      the renderer gets verified on a machine with no display — and it drives
//      the tools through synthetic mouse events, so interactive creation is
//      checked there too rather than only by hand.
//
// Note what this file does *not* link: GLFW. The window belongs to the engine
// (SurfaceKind::Glfw), and the host only forwards a frame loop.

#include <cadgeom/CadGeomRAII.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#define CADGEOM_DEMO_HEAP_CHECK 1
#else
#define CADGEOM_DEMO_HEAP_CHECK 0
#endif

namespace {

// ---------------------------------------------------------------------------
// Host-side plumbing
// ---------------------------------------------------------------------------

void LogToConsole(cadgeom::LogLevel level, const char* message, void* /*userData*/) {
    const char* tag = "?";
    switch (level) {
        case cadgeom::LogLevel::Trace:   tag = "trace"; break;
        case cadgeom::LogLevel::Debug:   tag = "debug"; break;
        case cadgeom::LogLevel::Info:    tag = "info "; break;
        case cadgeom::LogLevel::Warning: tag = "WARN "; break;
        case cadgeom::LogLevel::Error:   tag = "ERROR"; break;
        case cadgeom::LogLevel::Fatal:   tag = "FATAL"; break;
        case cadgeom::LogLevel::Off:     return;
    }
    std::printf("      [cadgeom %s] %s\n", tag, message);
}

/// A command implemented on the *host* side, to prove the boundary works in
/// both directions: the engine executes it, owns it, and calls Release() on it,
/// so the allocation and the free stay in this binary.
class RenameCommand final : public cadgeom::ICommand {
public:
    RenameCommand(cadgeom::EntityId target, const char* newName)
        : target_(target), newName_(newName) {}

    void Release() override { delete this; }

    cadgeom::CgResult Execute(cadgeom::IScene* scene) override {
        cadgeom::IEntity* e = scene->GetEntity(target_);
        if (!e) {
            return cadgeom::CgResult::InvalidHandle;
        }
        previousName_ = e->GetName();
        e->SetName(newName_.c_str());
        return cadgeom::CgResult::Ok;
    }

    cadgeom::CgResult Undo(cadgeom::IScene* scene) override {
        cadgeom::IEntity* e = scene->GetEntity(target_);
        if (!e) {
            return cadgeom::CgResult::InvalidHandle;
        }
        e->SetName(previousName_.c_str());
        return cadgeom::CgResult::Ok;
    }

    const char* GetName() const override { return "Rename"; }

private:
    ~RenameCommand() override = default;

    cadgeom::EntityId target_;
    std::string newName_;
    std::string previousName_;
};

void Section(const char* title) {
    std::printf("\n== %s ==\n", title);
}

bool Check(bool condition, const char* what) {
    std::printf("  [%s] %s\n", condition ? " ok " : "FAIL", what);
    return condition;
}

/// Reports an expected-not-yet-implemented result without counting it a failure.
void Pending(const char* what, cadgeom::ICadEngine& engine) {
    std::printf("  [ -- ] %s: %s\n", what, engine.GetLastErrorMessage());
    engine.ClearLastError();
}

struct Options {
    bool headless{false};
    bool apiOnly{false};
    bool perspective{false};
    uint32_t frames{0};  ///< 0 = until the window closes (or 4 when headless).
    std::string screenshotPath;
    uint32_t width{1280};
    uint32_t height{720};
};

Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : nullptr; };

        if (std::strcmp(arg, "--headless") == 0) {
            options.headless = true;
            if (options.screenshotPath.empty()) {
                options.screenshotPath = "cadgeom_m1.png";
            }
        } else if (std::strcmp(arg, "--api-only") == 0) {
            options.apiOnly = true;
        } else if (std::strcmp(arg, "--perspective") == 0) {
            options.perspective = true;
        } else if (std::strcmp(arg, "--screenshot") == 0) {
            if (const char* path = next()) {
                options.screenshotPath = path;
            }
        } else if (std::strcmp(arg, "--frames") == 0) {
            if (const char* count = next()) {
                options.frames = static_cast<uint32_t>(std::atoi(count));
            }
        } else if (std::strcmp(arg, "--size") == 0) {
            const char* w = next();
            const char* h = next();
            if (w && h) {
                options.width = static_cast<uint32_t>(std::atoi(w));
                options.height = static_cast<uint32_t>(std::atoi(h));
            }
        } else {
            std::printf("usage: glfw_viewer [--headless] [--api-only] [--perspective]\n"
                        "                   [--screenshot PATH] [--frames N] [--size W H]\n");
        }
    }
    if (options.headless && options.frames == 0) {
        options.frames = 4;
    }
    return options;
}

// ---------------------------------------------------------------------------
// Phase 1 — the public surface, no GPU involved (M0)
// ---------------------------------------------------------------------------

bool RunApiWalk(cadgeom::ICadEngine& engine) {
    using namespace cadgeom;
    bool ok = true;

    Section("Scene");
    IScene* scene = engine.GetScene();
    ok &= Check(scene != nullptr, "engine exposes a scene");

    const EntityId assembly = scene->CreateGroup("Assembly", kInvalidEntity);
    const EntityId bracket = scene->CreateGroup("Bracket", assembly);
    const EntityId boss = scene->CreateGroup("Boss", bracket);
    ok &= Check(IsValid(assembly) && IsValid(bracket) && IsValid(boss),
                "created a three-level hierarchy");
    ok &= Check(scene->GetEntityCount() == 3, "scene holds three entities");
    ok &= Check(scene->GetRootCount() == 1, "one of them is a root");

    IEntity* bracketEntity = scene->GetEntity(bracket);
    ok &= Check(bracketEntity != nullptr && bracketEntity->GetParent() == assembly,
                "child reports its parent");

    Transform xform{};
    xform.translation = Vec3d{100.0, 0.0, 25.0};
    bracketEntity->SetLocalTransform(xform);

    Mat4d world{};
    scene->GetEntity(boss)->GetWorldTransform(world);
    ok &= Check(world.m[12] == 100.0 && world.m[14] == 25.0,
                "grandchild inherits the parent's world transform");

    ok &= Check(CgFailed(scene->SetParent(assembly, boss)),
                "reparenting onto a descendant is rejected");

    Section("Selection");
    ISelection* selection = scene->GetSelection();
    selection->Add(bracket);
    selection->Add(boss);
    ok &= Check(selection->GetCount() == 2, "two entities selected");
    ok &= Check(selection->Contains(bracket), "selection membership is queryable");
    selection->Toggle(bracket);
    ok &= Check(selection->GetCount() == 1, "toggling removes an entity");

    Section("Undo / redo");
    ICommandStack* commands = scene->GetCommandStack();
    ok &= Check(CgSucceeded(commands->Push(new RenameCommand(bracket, "Bracket-Rev-B"))),
                "host command executed and pushed");
    ok &= Check(std::string(scene->GetEntity(bracket)->GetName()) == "Bracket-Rev-B",
                "the rename took effect");
    ok &= Check(commands->CanUndo() && std::string(commands->PeekUndoName()) == "Rename",
                "the stack names what it would undo");
    ok &= Check(CgSucceeded(commands->Undo()) &&
                    std::string(scene->GetEntity(bracket)->GetName()) == "Bracket",
                "undo restored the old name");
    ok &= Check(CgSucceeded(commands->Redo()) &&
                    std::string(scene->GetEntity(bracket)->GetName()) == "Bracket-Rev-B",
                "redo reapplied it");

    commands->BeginGroup("Rename both");
    commands->Push(new RenameCommand(bracket, "A"));
    commands->Push(new RenameCommand(boss, "B"));
    commands->EndGroup();
    ok &= Check(commands->GetUndoCount() == 2, "a group collapses into one undo step");
    commands->Undo();
    ok &= Check(std::string(scene->GetEntity(bracket)->GetName()) == "Bracket-Rev-B" &&
                    std::string(scene->GetEntity(boss)->GetName()) == "Boss",
                "undoing the group reverted both renames");

    Section("Destruction");
    ok &= Check(CgSucceeded(scene->DestroyEntity(bracket)), "destroyed a subtree");
    ok &= Check(scene->GetEntityCount() == 1, "the descendant went with it");
    ok &= Check(!scene->Exists(boss), "stale ids no longer resolve");
    ok &= Check(scene->GetSelection()->GetCount() == 0,
                "the selection dropped the destroyed entity");

    Section("Parametric geometry");
    IGeometryBuilder* builder = scene->GetGeometryBuilder();
    ok &= Check(builder != nullptr, "geometry builder is reachable");

    const Plane xy{Vec3d{0, 0, 0}, Vec3d{0, 0, 1}};
    const EntityId circle = builder->MakeCircle(xy, 25.0);
    ok &= Check(IsValid(circle), "created a circle from a plane and a radius");

    Aabb bounds{};
    ok &= Check(scene->GetEntity(circle)->GetWorldBounds(bounds) &&
                    std::abs(bounds.max.x - 25.0) < 0.1,
                "its bounds come from the tessellated curve");

    // The point of a CAD kernel: changing a parameter regenerates the geometry.
    // Nothing here edits a triangle.
    ShapeParams params{};
    ok &= Check(builder->GetParams(circle, params) && params.type == ShapeType::Circle,
                "the parametric definition reads back");
    params.circle.radius = 40.0;
    ok &= Check(CgSucceeded(builder->SetParams(circle, params)), "the radius is editable");
    ok &= Check(scene->GetEntity(circle)->GetWorldBounds(bounds) &&
                    std::abs(bounds.max.x - 40.0) < 0.1,
                "the geometry regenerated at the new radius");

    ok &= Check(CgSucceeded(commands->Undo()) &&
                    scene->GetEntity(circle)->GetWorldBounds(bounds) &&
                    std::abs(bounds.max.x - 25.0) < 0.1,
                "undo took the radius back");

    ok &= Check(!IsValid(builder->MakeCircle(xy, -1.0)) &&
                    engine.GetLastError() == CgResult::InvalidArgument,
                "a negative radius is refused with a reason");
    engine.ClearLastError();

    ok &= Check(CgSucceeded(scene->DestroyEntity(circle)) && !scene->Exists(circle),
                "deleting removes it");
    ok &= Check(CgSucceeded(commands->Undo()) && scene->Exists(circle),
                "and deleting is undoable too");

    Section("Not built yet");
    ExtrudeOptions extrudeOptions{};
    if (!IsValid(builder->Extrude(circle, Vec3d{0, 0, 1}, 30.0, extrudeOptions))) {
        Pending("Extrude", engine);
    }

    PickResult hit{};
    if (!scene->Raycast(Ray{Vec3d{0, 0, 100}, Vec3d{0, 0, -1}}, PickFilter_All, hit)) {
        Pending("Raycast", engine);
    }

    ExportOptions exportOptions{};
    if (CgFailed(engine.GetIoRegistry()->Export("out.glb", exportOptions, nullptr, nullptr))) {
        Pending("Export", engine);
    }

    if (CgFailed(engine.GetToolManager()->Activate(ToolId::Extrude))) {
        Pending("Activate(Extrude)", engine);
    }

    return ok;
}

// ---------------------------------------------------------------------------
// Phase 2 — the renderer and the tools (M1, M2)
// ---------------------------------------------------------------------------

void PrintCameraHelp() {
    std::printf("  middle drag  orbit          shift+middle / right drag  pan\n"
                "  wheel        zoom at cursor  double middle-click        zoom to fit\n"
                "  1..7         standard views  F fit   P ortho/persp   G grid   W wireframe\n"
                "  V select  X point  L line  C circle  R rectangle  Y polyline  Esc cancel\n"
                "  Del        delete the selection (Ctrl+Z / Ctrl+Y are host-side)\n");
}

/// Applies a style to one entity, since EntityStyle is set wholesale.
void Style(cadgeom::IScene& scene, cadgeom::EntityId entity, cadgeom::Color color, float width,
           cadgeom::LineStyle lineStyle) {
    using namespace cadgeom;
    IEntity* e = scene.GetEntity(entity);
    if (!e) {
        return;
    }
    EntityStyle style{};
    e->GetStyle(style);
    style.color = color;
    style.lineWidth = width;
    style.lineStyle = lineStyle;
    e->SetStyle(style);
}

/// A mounting plate, drawn the way a draughtsman would: outline, bore, bolt
/// holes on a hidden pitch circle, centre lines through the middle.
///
/// It exists to put every M2 primitive and every line style on screen at once —
/// which is also the only way to see that the screen-space line expansion is
/// giving the right widths and that the dash patterns phase correctly around a
/// tessellated curve.
void BuildDemoDrawing(cadgeom::IScene& scene) {
    using namespace cadgeom;
    IGeometryBuilder& builder = *scene.GetGeometryBuilder();

    const Vec3d up{0.0, 0.0, 1.0};
    const Plane xy{Vec3d{0, 0, 0}, up};

    const Color kOutline{0.82f, 0.84f, 0.88f, 1.0f};
    const Color kBore{0.30f, 0.68f, 0.95f, 1.0f};
    const Color kCentre{0.95f, 0.55f, 0.15f, 1.0f};
    const Color kHidden{0.45f, 0.48f, 0.55f, 1.0f};
    const Color kConstruction{0.35f, 0.75f, 0.45f, 1.0f};

    Style(scene, builder.MakeRectangle(xy, Vec3d{1, 0, 0}, 120.0, 80.0), kOutline, 2.5f,
          LineStyle::Solid);
    Style(scene, builder.MakeCircle(xy, 22.0), kBore, 2.0f, LineStyle::Solid);

    // The bolt pitch circle is a construction feature, so it gets the dash-dot
    // every drawing standard uses for one.
    Style(scene, builder.MakeCircle(xy, 45.0), kHidden, 1.25f, LineStyle::DashDot);

    // Centre lines: long-short-long, running past the outline as they should.
    Style(scene, builder.MakeLine(Vec3d{-70, 0, 0}, Vec3d{70, 0, 0}), kCentre, 1.25f,
          LineStyle::Center);
    Style(scene, builder.MakeLine(Vec3d{0, -50, 0}, Vec3d{0, 50, 0}), kCentre, 1.25f,
          LineStyle::Center);

    // Four bolt holes on the pitch circle, each with its centre marked.
    for (int i = 0; i < 4; ++i) {
        const double angle = (45.0 + 90.0 * i) * 3.14159265358979323846 / 180.0;
        const Vec3d centre{45.0 * std::cos(angle), 45.0 * std::sin(angle), 0.0};
        Style(scene, builder.MakeCircle(Plane{centre, up}, 6.0), kBore, 1.75f, LineStyle::Solid);
        Style(scene, builder.MakePoint(centre), kCentre, 7.0f, LineStyle::Solid);
    }

    // A chamfered corner, as an open polyline.
    const Vec3d chamfer[] = {{-60.0, 28.0, 0.0}, {-52.0, 40.0, 0.0}, {-40.0, 40.0, 0.0}};
    Style(scene, builder.MakePolyline(CgSpan<const Vec3d>{chamfer, 3}, false), kConstruction, 2.0f,
          LineStyle::Dashed);

    // Two more on other planes, because a work plane is the whole reason 2D
    // creation has an answer in a 3D scene at all (§6.1). These are what make
    // the drawing read as three-dimensional when the camera orbits.
    Style(scene, builder.MakeCircle(Plane{Vec3d{0, 0, 30}, Vec3d{1, 0, 0}}, 18.0), kBore, 1.75f,
          LineStyle::Dotted);
    Style(scene, builder.MakeArc(Plane{Vec3d{0, 0, 0}, Vec3d{0, 1, 0}}, 55.0, 0.0, 3.14159 * 0.5),
          kConstruction, 1.75f, LineStyle::Hidden);
}

/// Feeds the active tool the events a user would generate, so the interactive
/// creation path is exercised on a machine with no display. This is M2's
/// acceptance criterion: the mouse can draw.
bool DriveTools(cadgeom::ICadEngine& engine, cadgeom::IViewport& viewport) {
    using namespace cadgeom;
    bool ok = true;

    IScene& scene = *engine.GetScene();
    IToolManager& tools = *engine.GetToolManager();
    ICamera& camera = *viewport.GetCamera();

    // The work plane is what turns a pixel into a point. Every world position
    // below sits on it, so the round trip through the screen lands back where it
    // started (§6.1).
    const auto send = [&](const Vec3d& world, MouseAction action) {
        Vec2d pixel{};
        if (!camera.WorldToScreen(world, pixel)) {
            return false;
        }
        MouseEvent e{};
        e.button = MouseButton::Left;
        e.action = action;
        e.x = pixel.x;
        e.y = pixel.y;
        return viewport.OnMouseEvent(e);
    };

    const uint32_t before = scene.GetEntityCount();

    // Click, then click: the habit of every CAD package.
    ok &= Check(CgSucceeded(tools.Activate(ToolId::Line)), "activated the line tool");
    // An idle tool tracks the cursor without claiming the event, so the first
    // move is reported unconsumed on purpose; the press is what it takes.
    send(Vec3d{-30, -60, 0}, MouseAction::Move);
    ok &= Check(send(Vec3d{-30, -60, 0}, MouseAction::Down), "the tool took the first point");
    send(Vec3d{-30, -60, 0}, MouseAction::Up);
    send(Vec3d{30, -60, 0}, MouseAction::Move);
    send(Vec3d{30, -60, 0}, MouseAction::Down);
    ok &= Check(scene.GetEntityCount() == before + 1, "click-click drew a line");

    // Press, drag, release: the other habit, same tool machinery.
    ok &= Check(CgSucceeded(tools.Activate(ToolId::Rectangle)), "activated the rectangle tool");
    send(Vec3d{-50, 50, 0}, MouseAction::Move);
    send(Vec3d{-50, 50, 0}, MouseAction::Down);
    send(Vec3d{-20, 65, 0}, MouseAction::Move);
    send(Vec3d{-20, 65, 0}, MouseAction::Up);
    ok &= Check(scene.GetEntityCount() == before + 2, "press-drag-release drew a rectangle");

    const EntityId drawn = scene.GetEntityAt(scene.GetEntityCount() - 1);
    ok &= Check(scene.GetEntity(drawn)->GetShapeType() == ShapeType::Rectangle,
                "and what it drew is a parametric rectangle");

    ok &= Check(CgSucceeded(scene.GetCommandStack()->Undo()) &&
                    scene.GetEntityCount() == before + 1,
                "an interactively drawn shape undoes like any other");
    scene.GetCommandStack()->Redo();

    // Left mid-gesture on purpose: the circle below never becomes a shape, it
    // just shows up in the overlay for the screenshot. Preview geometry never
    // enters the scene and never enters the undo stack (§6.2).
    ok &= Check(CgSucceeded(tools.Activate(ToolId::Circle)), "activated the circle tool");
    send(Vec3d{40, 55, 0}, MouseAction::Move);
    send(Vec3d{40, 55, 0}, MouseAction::Down);
    send(Vec3d{62, 55, 0}, MouseAction::Move);
    ok &= Check(scene.GetEntityCount() == before + 2,
                "a rubber-band preview stays out of the scene");

    return ok;
}

bool RunViewport(cadgeom::ICadEngine& engine, const Options& options) {
    using namespace cadgeom;
    bool ok = true;

    ViewportDesc desc{};
    desc.surface.kind = options.headless ? SurfaceKind::Headless : SurfaceKind::Glfw;
    desc.surface.width = options.width;
    desc.surface.height = options.height;
    desc.surface.title = "CadGeom — M2";
    desc.projection =
        options.perspective ? ProjectionMode::Perspective : ProjectionMode::Orthographic;
    // Linear light: the renderer composes in a float target and the swapchain
    // blit applies the sRGB encode, so this is darker on screen than it looks.
    desc.background = Color{0.035f, 0.038f, 0.045f, 1.0f};
    desc.showGrid = true;
    desc.vsync = true;

    IViewport* viewport = engine.CreateViewport(desc);
    if (!viewport) {
        std::printf("  [FAIL] CreateViewport: %s\n", engine.GetLastErrorMessage());
        return false;
    }
    ok &= Check(true, options.headless ? "headless viewport created" : "window created");
    std::printf("         device: %s\n", engine.GetDeviceName());
    ok &= Check(engine.GetViewportCount() == 1, "the engine tracks its viewport");

    ICamera* camera = viewport->GetCamera();
    ok &= Check(camera != nullptr, "viewport exposes its camera");

    // Prove the camera's screen mapping is self-consistent before trusting
    // anything drawn through it: the centre pixel's ray has to come back to the
    // centre pixel.
    uint32_t width = 0;
    uint32_t height = 0;
    viewport->GetSize(width, height);
    Ray ray{};
    camera->ScreenToRay(width * 0.5, height * 0.5, ray);
    Vec2d back{};
    const Vec3d ahead{ray.origin.x + ray.dir.x * 10.0, ray.origin.y + ray.dir.y * 10.0,
                      ray.origin.z + ray.dir.z * 10.0};
    ok &= Check(camera->WorldToScreen(ahead, back) &&
                    std::abs(back.x - width * 0.5) < 1.0 && std::abs(back.y - height * 0.5) < 1.0,
                "ScreenToRay and WorldToScreen agree");

    Section("Drawing");
    BuildDemoDrawing(*engine.GetScene());
    ok &= Check(engine.GetScene()->GetEntityCount() == 16, "built the demo drawing");
    Aabb sceneBounds{};
    ok &= Check(engine.GetScene()->GetBounds(sceneBounds), "the scene reports its bounds");
    // Top reads as a drawing, which is what the orthographic default is for;
    // isometric is where the geometry on the other two work planes shows up.
    camera->SetStandardView(options.perspective ? StandardView::Isometric : StandardView::Top);
    camera->ZoomToFit(nullptr, 1.25);

    Section("Tools");
    ok &= DriveTools(engine, *viewport);

    if (!options.headless) {
        // Back to a view that shows the off-plane geometry, and out of the
        // half-finished circle the tool drive left behind.
        engine.GetToolManager()->Cancel();
        camera->SetStandardView(StandardView::Isometric);
        camera->ZoomToFit(nullptr, 1.25);
        Section("Interactive");
        PrintCameraHelp();
    }

    auto previous = std::chrono::steady_clock::now();
    uint32_t frames = 0;
    CgResult lastRender = CgResult::Ok;

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const double delta = std::chrono::duration<double>(now - previous).count();
        previous = now;

        // Tick first, every frame: it drains the window's event queue, retires
        // finished GPU resources and brings the GPU's copy of the scene up to
        // date. Rendering without it would show the previous frame's state.
        engine.Tick(delta);

        lastRender = viewport->Render();
        if (CgFailed(lastRender)) {
            std::printf("  [FAIL] Render: %s\n", engine.GetLastErrorMessage());
            break;
        }
        ++frames;

        if (options.frames > 0 && frames >= options.frames) {
            break;
        }
        if (!options.headless && viewport->ShouldClose()) {
            break;
        }
    }

    ok &= Check(CgSucceeded(lastRender), "frames rendered without error");
    std::printf("         %u frame(s)\n", frames);

    if (options.headless) {
        // Only meaningful headless: a GLFW window's size belongs to the window
        // system, so Resize() there is a notification, not an instruction.
        // This is the swapchain/target rebuild path a host embedding the engine
        // hits on every window drag.
        const uint32_t halfWidth = options.width / 2;
        const uint32_t halfHeight = options.height / 2;
        viewport->Resize(halfWidth, halfHeight);
        engine.Tick(0.016);
        ok &= Check(CgSucceeded(viewport->Render()), "rendered again after a resize");

        uint32_t resizedWidth = 0;
        uint32_t resizedHeight = 0;
        viewport->GetSize(resizedWidth, resizedHeight);
        ok &= Check(resizedWidth == halfWidth && resizedHeight == halfHeight,
                    "the viewport reports its new size");

        viewport->Resize(options.width, options.height);
        engine.Tick(0.016);
        viewport->Render();
    }

    if (!options.screenshotPath.empty()) {
        const CgResult saved = viewport->SaveScreenshot(options.screenshotPath.c_str());
        ok &= Check(CgSucceeded(saved), "screenshot written");
        if (CgSucceeded(saved)) {
            std::printf("         %s\n", options.screenshotPath.c_str());
        } else {
            std::printf("         %s\n", engine.GetLastErrorMessage());
        }
    }

    viewport->Release();
    ok &= Check(engine.GetViewportCount() == 0, "releasing the viewport unregistered it");
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    const Options options = ParseOptions(argc, argv);

    std::printf("CadGeom demo host\n");

    Section("ABI handshake");
    std::printf("  headers   : %u.%u.%u\n", CADGEOM_API_VERSION_MAJOR, CADGEOM_API_VERSION_MINOR,
                CADGEOM_API_VERSION_PATCH);
    const uint32_t dllVersion = CadGeom_GetApiVersion();
    std::printf("  library   : %u.%u.%u\n", CADGEOM_VERSION_MAJOR(dllVersion),
                CADGEOM_VERSION_MINOR(dllVersion), CADGEOM_VERSION_PATCH(dllVersion));
    std::printf("  build     : %s\n", CadGeom_GetBuildInfo());
#if CADGEOM_HAS_VULKAN
    std::printf("  renderer  : built against the Vulkan SDK\n");
#else
    std::printf("  renderer  : not built; install LunarG >= 1.3.275 and reconfigure\n");
#endif

    bool ok = Check(CadGeom_IsApiVersionCompatible(CADGEOM_API_VERSION) != 0,
                    "the loaded library speaks our API version");
    if (!ok) {
        return 1;
    }

    cadgeom::EngineDesc desc{};
    desc.applicationName = "CadGeom demo host";
    desc.enableValidation = true;
    desc.logLevel = cadgeom::LogLevel::Debug;
    desc.logCallback = &LogToConsole;

    // -- Phase 1 ------------------------------------------------------------
    // Bracketed on its own so the heap check measures the engine and nothing
    // else. A Vulkan device brings driver allocations with it that outlive any
    // scope we control, which is why the renderer gets its own engine below.
    const uint64_t objectsBefore = CadGeom_GetLiveObjectCount();

#if CADGEOM_DEMO_HEAP_CHECK
    _CrtMemState heapBefore;
    _CrtMemCheckpoint(&heapBefore);
#endif

    {
        cadgeom::EnginePtr engine = cadgeom::CreateEngine(desc);
        if (!engine) {
            std::printf("  [FAIL] engine creation: %s\n", CadGeom_GetCreateEngineError());
            return 1;
        }
        std::printf("  [ ok ] engine created\n");
        ok &= RunApiWalk(*engine);
    }

    Section("Teardown");
    const uint64_t leaked = CadGeom_GetLiveObjectCount() - objectsBefore;
    ok &= Check(leaked == 0, "every engine-side object was released");
    if (leaked != 0) {
        std::printf("         %llu object(s) still alive\n",
                    static_cast<unsigned long long>(leaked));
    }

#if CADGEOM_DEMO_HEAP_CHECK
    _CrtMemState heapAfter;
    _CrtMemState heapDelta;
    _CrtMemCheckpoint(&heapAfter);
    const bool heapClean = _CrtMemDifference(&heapDelta, &heapBefore, &heapAfter) == 0;
    ok &= Check(heapClean, "the CRT heap is back where it started");
    if (!heapClean) {
        _CrtMemDumpStatistics(&heapDelta);
        _CrtMemDumpAllObjectsSince(&heapBefore);
    }
#else
    std::printf("  [ -- ] CRT heap check runs in a Debug MSVC build only\n");
#endif

    // -- Phase 2 ------------------------------------------------------------
#if !CADGEOM_HAS_VULKAN
    // Not a failure: a build without the SDK is a documented configuration, and
    // everything through M0 still works in it.
    Section("Renderer");
    std::printf("  [ -- ] this library was built without the Vulkan SDK, so there is no "
                "renderer to exercise\n");
#else
    if (!options.apiOnly) {
        Section(options.headless ? "Renderer (headless)" : "Renderer");
        cadgeom::EnginePtr engine = cadgeom::CreateEngine(desc);
        if (!engine) {
            std::printf("  [FAIL] engine creation: %s\n", CadGeom_GetCreateEngineError());
            return 1;
        }
        ok &= RunViewport(*engine, options);
    }
#endif

    const uint64_t finalLeak = CadGeom_GetLiveObjectCount() - objectsBefore;
    ok &= Check(finalLeak == 0, "nothing outlived the second engine either");

    std::printf("\n%s\n", ok ? "Demo passed." : "Demo FAILED.");
    return ok ? 0 : 1;
}
