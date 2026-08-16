// CadGeom demo host — milestone M0.
//
// M0's acceptance is "the demo creates and releases an engine with no leaks",
// so this walks the entire public surface once and reports what it finds. The
// parts that are not built yet answer with the milestone that brings them,
// which makes this file double as a live status report on the engine.
//
// M1 replaces the body of RunDemo() with a real window, a grid and an orbit
// camera; the setup and teardown around it stay as they are.

#include <cadgeom/CadGeomRAII.h>

#include <GLFW/glfw3.h>

#include <cstdio>
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

// ---------------------------------------------------------------------------

bool RunDemo(cadgeom::ICadEngine& engine) {
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

    ViewportDesc vpDesc{};
    vpDesc.surface.title = "CadGeom";
    if (engine.CreateViewport(vpDesc) == nullptr) {
        Pending("CreateViewport", engine);
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

/// The Vulkan SDK is the one thing not vendored, and its absence is what blocks
/// M1. Checking it here means the demo tells you before you get there.
void ReportM1Readiness() {
    Section("M1 readiness");
    if (!glfwInit()) {
        std::printf("  [FAIL] GLFW failed to initialise\n");
        return;
    }
    std::printf("  [ ok ] GLFW %s\n", glfwGetVersionString());

    // The loader ships with the GPU driver; the headers, validation layers and
    // glslc come from the LunarG SDK. Having the first says nothing about the
    // second, and it is the second that M1 needs.
    if (glfwVulkanSupported()) {
        std::printf("  [ ok ] runtime Vulkan loader found (driver-provided)\n");
    } else {
        std::printf("  [ -- ] no Vulkan loader; this machine cannot present a Vulkan surface\n");
    }
#if CADGEOM_HAS_VULKAN
    std::printf("  [ ok ] built against the Vulkan SDK\n");
#else
    std::printf("  [ -- ] built without the Vulkan SDK; install LunarG >= 1.3.275 and "
                "reconfigure before starting M1\n");
#endif
    glfwTerminate();
}

} // namespace

int main() {
    std::printf("CadGeom demo host\n");

    Section("ABI handshake");
    std::printf("  headers   : %u.%u.%u\n", CADGEOM_API_VERSION_MAJOR, CADGEOM_API_VERSION_MINOR,
                CADGEOM_API_VERSION_PATCH);
    const uint32_t dllVersion = CadGeom_GetApiVersion();
    std::printf("  library   : %u.%u.%u\n", CADGEOM_VERSION_MAJOR(dllVersion),
                CADGEOM_VERSION_MINOR(dllVersion), CADGEOM_VERSION_PATCH(dllVersion));
    std::printf("  build     : %s\n", CadGeom_GetBuildInfo());

    bool ok = Check(CadGeom_IsApiVersionCompatible(CADGEOM_API_VERSION) != 0,
                    "the loaded library speaks our API version");
    if (!ok) {
        return 1;
    }

    const uint64_t objectsBefore = CadGeom_GetLiveObjectCount();

#if CADGEOM_DEMO_HEAP_CHECK
    // Bracketing the engine's whole lifetime rather than dumping at exit: this
    // measures exactly what the engine allocated and freed, with no false
    // positives from statics that outlive main. The DLL and this executable
    // share the debug CRT heap, so DLL-side allocations are counted too.
    _CrtMemState heapBefore;
    _CrtMemCheckpoint(&heapBefore);
#endif

    {
        cadgeom::EngineDesc desc{};
        desc.applicationName = "CadGeom demo host";
        desc.enableValidation = true;
        desc.logLevel = cadgeom::LogLevel::Debug;
        desc.logCallback = &LogToConsole;

        cadgeom::EnginePtr engine = cadgeom::CreateEngine(desc);
        if (!engine) {
            std::printf("  [FAIL] engine creation: %s\n", CadGeom_GetCreateEngineError());
            return 1;
        }
        std::printf("  [ ok ] engine created\n");

        ok &= RunDemo(*engine);
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

    ReportM1Readiness();

    std::printf("\n%s\n", ok ? "M0 demo passed." : "M0 demo FAILED.");
    return ok ? 0 : 1;
}
