// CadGeom demo host — milestones M0 and M1.
//
// Two phases, deliberately separate:
//
//   1. A pass over the public surface with no GPU in sight. This is M0's
//      acceptance test — create, exercise, release, prove nothing leaked — and
//      it is bracketed by a CRT heap checkpoint, so it has to stay free of
//      anything that allocates behind the engine's back.
//   2. A viewport: a window, a grid, an orbit camera and a shaded solid. Run
//      with --headless it renders off-screen and writes a PNG instead, which is
//      how the renderer gets verified on a machine with no display.
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

    Section("Not built yet");
    IGeometryBuilder* builder = scene->GetGeometryBuilder();
    ok &= Check(builder != nullptr, "geometry builder is reachable");
    if (!IsValid(builder->MakeCircle(Plane{Vec3d{0, 0, 0}, Vec3d{0, 0, 1}}, 25.0))) {
        Pending("MakeCircle", engine);
    }

    ExportOptions exportOptions{};
    if (CgFailed(engine.GetIoRegistry()->Export("out.glb", exportOptions, nullptr, nullptr))) {
        Pending("Export", engine);
    }

    if (CgFailed(engine.GetToolManager()->Activate(ToolId::Circle))) {
        Pending("Activate(Circle)", engine);
    }

    return ok;
}

// ---------------------------------------------------------------------------
// Phase 2 — the renderer (M1)
// ---------------------------------------------------------------------------

void PrintCameraHelp() {
    std::printf("  middle drag  orbit          shift+middle / right drag  pan\n"
                "  wheel        zoom at cursor  double middle-click        zoom to fit\n"
                "  1..7         standard views  F fit   P ortho/persp   G grid   W wireframe\n");
}

bool RunViewport(cadgeom::ICadEngine& engine, const Options& options) {
    using namespace cadgeom;
    bool ok = true;

    ViewportDesc desc{};
    desc.surface.kind = options.headless ? SurfaceKind::Headless : SurfaceKind::Glfw;
    desc.surface.width = options.width;
    desc.surface.height = options.height;
    desc.surface.title = "CadGeom — M1";
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

    if (!options.headless) {
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
