// CadGeom demo host — milestones M0 through M6.
//
// Two phases, deliberately separate:
//
//   1. A pass over the public surface with no GPU in sight: the scene graph, the
//      geometry kernel, picking, undo, and the file round trip. This is M0's
//      acceptance test — create, exercise, release, prove nothing leaked — and it
//      is bracketed by a CRT heap checkpoint, so it has to stay free of anything
//      that allocates behind the engine's back.
//   2. A viewport: a window, a grid, an orbit camera, a drawing made of points,
//      lines, circles and rectangles, the tools that create them, the
//      select/move/rotate/scale gestures that edit them, the extrusions that turn
//      two of those profiles into solids, and the measurement that reads a
//      distance back off them. Run with --headless it renders off-screen and
//      writes a PNG instead, which is how the renderer gets verified on a machine
//      with no display — and it drives the tools through synthetic mouse events,
//      so interactive creation, snapping and the gizmos are checked there too
//      rather than only by hand.
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
#include <vector>

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

struct Options {
    bool headless{false};
    bool apiOnly{false};
    bool perspective{false};
    uint32_t frames{0};  ///< 0 = until the window closes (or 4 when headless).
    std::string screenshotPath;
    /// --import PATH：看一个真实文件，而不是内置的那张图纸。
    std::string importPath;
    /// --export PATH：把画完的场景写出去。和 --import 配成一对，M5 的往返就在
    /// 命令行上跑得通了。
    std::string exportPath;
    uint32_t width{1280};
    uint32_t height{720};
    /// --samples N：MSAA。设备支持不到那么多会被降下来，降了会说一声（M6）。
    uint32_t samples{4};
    /// --viewports N：开几个视口。第二个用正视图 + 隐藏线，一眼看出两件事
    /// —— 视口之间只共享设备和几何，显示状态各自独立（M6）。
    uint32_t viewports{1};
    /// --hidden-line：主视口用隐藏线模式。没有显示器时检查这一条渲染改动，靠的
    /// 就是它加 --screenshot（M6）。
    bool hiddenLine{false};
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
        } else if (std::strcmp(arg, "--import") == 0) {
            if (const char* path = next()) {
                options.importPath = path;
            }
        } else if (std::strcmp(arg, "--export") == 0) {
            if (const char* path = next()) {
                options.exportPath = path;
            }
        } else if (std::strcmp(arg, "--size") == 0) {
            const char* w = next();
            const char* h = next();
            if (w && h) {
                options.width = static_cast<uint32_t>(std::atoi(w));
                options.height = static_cast<uint32_t>(std::atoi(h));
            }
        } else if (std::strcmp(arg, "--hidden-line") == 0) {
            options.hiddenLine = true;
        } else if (std::strcmp(arg, "--samples") == 0) {
            if (const char* count = next()) {
                options.samples = static_cast<uint32_t>(std::atoi(count));
            }
        } else if (std::strcmp(arg, "--viewports") == 0) {
            if (const char* count = next()) {
                options.viewports = static_cast<uint32_t>(std::atoi(count));
                if (options.viewports < 1) {
                    options.viewports = 1;
                }
            }
        } else {
            std::printf("usage: glfw_viewer [--headless] [--api-only] [--perspective]\n"
                        "                   [--screenshot PATH] [--frames N] [--size W H]\n"
                        "                   [--samples N] [--viewports N] [--hidden-line]\n"
                        "                   [--import MODEL] [--export MODEL]  (.gltf/.glb/.obj)\n");
        }
    }
    if (options.headless && options.frames == 0) {
        options.frames = 4;
    }
    return options;
}

#if CADGEOM_DEMO_HEAP_CHECK
/// Runs one silent export/import so the one-time cost of the file layer is
/// already on the heap before the checkpoint below takes its baseline. Says
/// nothing and checks nothing: whether it worked is phase 1's business.
void WarmUpFileIo(const cadgeom::EngineDesc& desc) {
    cadgeom::EngineDesc quiet = desc;
    quiet.logLevel = cadgeom::LogLevel::Off;
    quiet.logCallback = nullptr;

    cadgeom::EnginePtr engine = cadgeom::CreateEngine(quiet);
    if (!engine) {
        return;
    }
    const char* path = "cadgeom_warmup.glb";
    engine->GetScene()->GetGeometryBuilder()->MakeCircle(
        cadgeom::Plane{cadgeom::Vec3d{0, 0, 0}, cadgeom::Vec3d{0, 0, 1}}, 1.0);

    cadgeom::ExportOptions exportOptions{};
    if (cadgeom::CgSucceeded(
            engine->GetIoRegistry()->Export(path, exportOptions, nullptr, nullptr))) {
        cadgeom::ImportOptions importOptions{};
        engine->GetIoRegistry()->Import(path, importOptions, nullptr, nullptr);
    }
    engine.reset();
    std::remove(path);
}
#endif

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

    Section("Extrude");
    // 圆 → 圆柱。拉伸出来的是一个参数化实体，改高度是重新扫掠而不是编辑三角形，
    // 拓扑也一并生成，面拾取因此立刻可用（§3.3）。
    ExtrudeOptions extrudeOptions{};
    const EntityId cylinder = builder->Extrude(circle, Vec3d{0, 0, 1}, 30.0, extrudeOptions);
    ok &= Check(IsValid(cylinder) && scene->GetEntity(cylinder)->GetShapeType() == ShapeType::Solid,
                "a circle sweeps into a solid");
    ok &= Check(scene->GetEntity(cylinder)->GetWorldBounds(bounds) &&
                    std::abs(bounds.max.z - 30.0) < 1e-9,
                "the solid is as tall as it was asked to be");

    ShapeParams solidParams{};
    ok &= Check(builder->GetParams(cylinder, solidParams) &&
                    solidParams.type == ShapeType::Solid &&
                    std::abs(solidParams.extrude.distance - 30.0) < 1e-9,
                "and it reads back as a parametric extrusion");
    solidParams.extrude.distance = 12.0;
    ok &= Check(CgSucceeded(builder->SetParams(cylinder, solidParams)) &&
                    scene->GetEntity(cylinder)->GetWorldBounds(bounds) &&
                    std::abs(bounds.max.z - 12.0) < 1e-9,
                "changing the height re-sweeps it");

    // 面拾取报的是**面**下标，不是三角形下标 —— 「点某个面 → 设为工作平面 → 在上面
    // 画圆 → 拉伸」这条工作流靠的就是它（§6.3）。
    PickResult faceHit{};
    ok &= Check(scene->Raycast(Ray{Vec3d{0, 0, 100}, Vec3d{0, 0, -1}}, PickFilter_Face, faceHit) &&
                    faceHit.entity == cylinder && faceHit.kind == PickKind::Face,
                "a ray onto the top face reports a face hit");
    ok &= Check(std::abs(faceHit.normal.z - 1.0) < 1e-9 && std::abs(faceHit.point.z - 12.0) < 1e-9,
                "with the face's own normal and the exact hit point");

    ok &= Check(CgSucceeded(commands->Undo()) && CgSucceeded(commands->Undo()) &&
                    !scene->Exists(cylinder),
                "the height edit and the extrude undo one step each");

    const EntityId openLine = builder->MakeLine(Vec3d{0, 0, 0}, Vec3d{10, 0, 0});
    ok &= Check(!IsValid(builder->Extrude(openLine, Vec3d{0, 0, 1}, 5.0, extrudeOptions)) &&
                    CgFailed(engine.GetLastError()),
                "a shape that encloses no area is refused with a reason");
    engine.ClearLastError();

    Section("Picking");
    // 一条垂直打向上面那个 25 mm 圆的射线。拾取是 CPU 对着 BVH 做的，全程不需要
    // 任何设备（§6.3）。
    PickResult hit{};
    ok &= Check(scene->Raycast(Ray{Vec3d{25, 0, 100}, Vec3d{0, 0, -1}}, PickFilter_All, hit) &&
                    hit.entity == circle,
                "a ray finds the circle it passes through");
    ok &= Check(hit.kind == PickKind::Edge || hit.kind == PickKind::Vertex,
                "and resolves which sub-element it hit");
    ok &= Check(std::abs(hit.normal.z) > 0.9,
                "a planar curve reports its own plane as the hit normal");

    // 没选中是一个普通答案，不是失败：它不能在错误槽里留下东西，否则宿主每帧
    // 轮询悬停都会读到一条假故障。
    PickResult miss{};
    engine.ClearLastError();
    ok &= Check(!scene->Raycast(Ray{Vec3d{500, 500, 100}, Vec3d{0, 0, -1}}, PickFilter_All, miss) &&
                    engine.GetLastError() == CgResult::Ok,
                "an empty region reports no hit and no error");

    ISelection* pickSelection = scene->GetSelection();
    pickSelection->Set(CgSpan<const EntityId>{&hit.entity, 1});
    pickSelection->SetSubElement(hit.kind, hit.subIndex);
    ok &= Check(pickSelection->Contains(circle) &&
                    pickSelection->GetSubElementKind() == hit.kind,
                "the pick drives the selection, sub-element and all");
    pickSelection->Clear();

    Section("Data IO");
    // M5 的验收标准：导出再导入，参数化信息一个不丢（§9）。glTF 的 `extras` 带着
    // 圆心半径、拉伸方向和距离一起走，所以读回来的圆还是圆 —— 不是一堆看着像圆的
    // 三角形。别家软件读同一个文件时 `extras` 被忽略，看到的是一个普通 mesh。
    IIoRegistry* io = engine.GetIoRegistry();
    ok &= Check(io->CanExport("gltf") && io->CanExport("obj") && io->CanImport("glb"),
                "the built-in formats are registered at startup");

    ExportOptions exportOptions{};
    ok &= Check(CgSucceeded(io->Export("cadgeom_demo.glb", exportOptions, nullptr, nullptr)),
                "exported the scene to cadgeom_demo.glb");
    ok &= Check(CgSucceeded(io->Export("cadgeom_demo.obj", exportOptions, nullptr, nullptr)),
                "and to cadgeom_demo.obj (triangles and polylines, no parameters)");

    // 读回一个干净的场景。mergeIntoScene = false 会先清空 —— 连撤销栈一起，所以这
    // 一次导入本身撤不回来，这是「替换而不是合并」明码标价的代价。
    ImportOptions importOptions{};
    importOptions.mergeIntoScene = false;
    ok &= Check(CgSucceeded(io->Import("cadgeom_demo.glb", importOptions, nullptr, nullptr)),
                "read the whole thing back from the .glb");

    EntityId reloaded = kInvalidEntity;
    for (uint32_t i = 0; i < scene->GetEntityCount(); ++i) {
        const EntityId id = scene->GetEntityAt(i);
        if (scene->GetEntity(id)->GetShapeType() == ShapeType::Circle) {
            reloaded = id;
            break;
        }
    }
    ShapeParams reloadedParams{};
    ok &= Check(IsValid(reloaded) && builder->GetParams(reloaded, reloadedParams) &&
                    reloadedParams.circle.radius == 25.0,
                "the circle came back as a circle, radius and all");

    // 参数是真相：改半径依旧是重新生成几何，和它有没有进过文件无关。
    reloadedParams.circle.radius = 33.0;
    ok &= Check(CgSucceeded(builder->SetParams(reloaded, reloadedParams)) &&
                    scene->GetEntity(reloaded)->GetWorldBounds(bounds) &&
                    std::abs(bounds.max.x - 33.0) < 0.1,
                "and it is still editable after the round trip");

    Section("Units and the extension slot");
    // M6 的新能力从 GetExtension 进来，而不是往冻结的接口上加虚函数（§2.2 第 3 条）。
    // 宿主拿到 null 就是「这个版本的库没有这件东西」，据此降级而不是崩。
    auto* ext = static_cast<ICadEngine2*>(engine.GetExtension(ExtensionId_Engine2));
    ok &= Check(ext != nullptr, "the engine hands out its M6 extension");
    ok &= Check(engine.GetExtension(0xBADC0DEu) == nullptr,
                "and answers null for an id it does not know");

    if (ext) {
        char reading[64];
        UnitSettings units{};
        ext->GetUnitSettings(units);
        ok &= Check(units.modelUnit == LengthUnit::Millimetre, "the model is millimetres by default");

        ext->FormatLength(25.4, reading, sizeof(reading));
        ok &= Check(std::string(reading) == "25.40 mm", "a length formats in the display unit");

        // 换显示单位只换读数。下面那个圆的半径一个数都不会动 —— 单位是显示层的事，
        // 几何是 SSOT（§3.1）。
        units.displayUnit = LengthUnit::Inch;
        units.linearPrecision = 3;
        ext->SetUnitSettings(units);
        ext->FormatLength(25.4, reading, sizeof(reading));
        ok &= Check(std::string(reading) == "1.000 in", "and follows the display unit when it changes");
        ok &= Check(std::abs(ext->ToModelLength(1.0) - 25.4) < 1e-12,
                    "the host's input box converts back the same way");

        ShapeParams stillMetric{};
        ok &= Check(builder->GetParams(reloaded, stillMetric) &&
                        stillMetric.circle.radius == 33.0,
                    "changing units moved no geometry at all");

        units.displayUnit = LengthUnit::Millimetre;
        units.linearPrecision = 2;
        ext->SetUnitSettings(units);

        // 垂足吸附缺的那个「上一个点」现在是引擎的一份状态，所以冻结的
        // IToolContext::SnapAt 一个字都不用改就能报出垂足（§6.3）。
        Vec3d reference{};
        ok &= Check(!ext->GetSnapReference(reference), "no perpendicular reference to start with");
        ext->SetSnapReference(Vec3d{10.0, 20.0, 0.0});
        ok &= Check(ext->GetSnapReference(reference) && reference.y == 20.0,
                    "and one can be set for Snap_Perpendicular to work from");
        ext->ClearSnapReference();

        ok &= Check((engine.GetToolManager()->GetSnapMask() & Snap_Perpendicular) != 0,
                    "Snap_Perpendicular is live in the default mask");
    }

    ok &= Check(CgSucceeded(engine.GetToolManager()->Activate(ToolId::Scale)) &&
                    CgSucceeded(engine.GetToolManager()->Activate(ToolId::Measure)),
                "the last two built-in tools are registered");
    engine.GetToolManager()->Activate(ToolId::Select);

    return ok;
}

// ---------------------------------------------------------------------------
// Phase 2 — the renderer and the tools (M1, M2)
// ---------------------------------------------------------------------------

void PrintCameraHelp() {
    std::printf("  middle drag  orbit          shift+middle / right drag  pan\n"
                "  wheel        zoom at cursor  double middle-click        fit\n"
                "  1..7  standard views   F fit (selection if any)   P ortho/persp\n"
                "  G grid   H heads-up display   W cycle shaded/edges/wireframe/hidden-line\n"
                "  V select  X point  L line  C circle  R rectangle  Y polyline  Esc cancel\n"
                "  E extrude — click a closed profile, then drag along its normal\n"
                "  M move    T rotate    S scale  — drag an axis, plane, ring or the centre\n"
                "  D measure — click two points; the readout is in the status line\n"
                "  ctrl+click / shift+click  add to the selection    Del  delete it\n"
                "  Ctrl+Z undo   Ctrl+Y (or Ctrl+Shift+Z) redo\n");
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

    ISelection& selection = *scene.GetSelection();
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

    // -- M3：吸附 -----------------------------------------------------------
    //
    // 对着一个已知的孔心「点歪」几个像素。默认吸附掩码里有端点和圆心，所以工具
    // 拿到的应该是那个孔心本身，而不是光标底下的那个近似位置。
    {
        const double angle = 45.0 * 3.14159265358979323846 / 180.0;
        const Vec3d holeCentre{45.0 * std::cos(angle), 45.0 * std::sin(angle), 0.0};
        Vec2d holePixel{};
        if (camera.WorldToScreen(holeCentre, holePixel)) {
            const auto nudged = [&](double dx, double dy, MouseAction action) {
                MouseEvent e{};
                e.button = MouseButton::Left;
                e.action = action;
                e.x = holePixel.x + dx;
                e.y = holePixel.y + dy;
                return viewport.OnMouseEvent(e);
            };

            tools.Activate(ToolId::Line);
            nudged(3.0, -2.0, MouseAction::Move);
            nudged(3.0, -2.0, MouseAction::Down);
            nudged(3.0, -2.0, MouseAction::Up);
            send(Vec3d{80, 0, 0}, MouseAction::Move);
            send(Vec3d{80, 0, 0}, MouseAction::Down);

            ShapeParams snapped{};
            const EntityId snappedLine = scene.GetEntityAt(scene.GetEntityCount() - 1);
            ok &= Check(scene.GetGeometryBuilder()->GetParams(snappedLine, snapped) &&
                            snapped.type == ShapeType::Line &&
                            std::abs(snapped.line.start.x - holeCentre.x) < 1e-9 &&
                            std::abs(snapped.line.start.y - holeCentre.y) < 1e-9,
                        "a click near a hole centre snapped exactly onto it");
            scene.GetCommandStack()->Undo();
        }
    }

    // -- M4：把画出来的矩形拉成一个实体 --------------------------------------
    //
    // 先转到轴测视角。高度是把鼠标射线投到扫掠轴上求出来的，而在顶视图里那根轴正
    // 对着观察者 —— 射线和它平行，无解。这不是引擎的毛病，是拉伸这件事本身在那个
    // 视角下没有答案，任何 CAD 里都一样。
    camera.SetStandardView(StandardView::Isometric);
    camera.ZoomToFit(nullptr, 1.25);

    // 轮廓走选择集而不是靠点：真人点下去之前有预高亮告诉他会选中什么，脚本没有，
    // 而一个像素在任意视角下命中哪条曲线是说不准的。工具在 OnActivate 时会把已经
    // 选好的东西接过来，「先选中，再按 E」本来也是 CAD 里更常见的那一半习惯。
    const EntityId rectangle = scene.GetEntityAt(before + 1);
    selection.Set(CgSpan<const EntityId>{&rectangle, 1});
    ok &= Check(CgSucceeded(tools.Activate(ToolId::Extrude)), "activated the extrude tool");

    // 第一次移动定下高度的零点，第二次给出高度。两个点都落在扫掠轴上，所以正交和
    // 透视下算出来的是同一个数。
    const Vec3d rectCentre{-35.0, 57.5, 0.0};
    const Vec3d rectTop{rectCentre.x, rectCentre.y, 20.0};
    send(rectCentre, MouseAction::Move);
    send(rectTop, MouseAction::Move);
    ok &= Check(send(rectTop, MouseAction::Down), "the tool took the height");

    ok &= Check(scene.GetEntityCount() == before + 3, "dragging a height produced a solid");
    const EntityId solid = scene.GetEntityAt(scene.GetEntityCount() - 1);
    ok &= Check(scene.GetEntity(solid)->GetShapeType() == ShapeType::Solid,
                "and what it produced is a parametric solid");

    Aabb solidBounds{};
    ok &= Check(scene.GetEntity(solid)->GetWorldBounds(solidBounds) && solidBounds.max.z > 1.0,
                "the solid stands up off the work plane");
    ok &= Check(std::string(scene.GetCommandStack()->PeekUndoName()) == "Extrude",
                "the whole extrusion is one undo step");

    // 面拾取：实体做出来之后，「点某个面 → 设为工作平面」这条路才真的通了（§6.3）。
    Vec2d topPixel{};
    if (camera.WorldToScreen(Vec3d{(solidBounds.min.x + solidBounds.max.x) * 0.5,
                                   (solidBounds.min.y + solidBounds.max.y) * 0.5,
                                   solidBounds.max.z},
                             topPixel)) {
        PickResult facePick{};
        ok &= Check(viewport.Pick(topPixel.x, topPixel.y, PickFilter_Face, facePick) &&
                        facePick.entity == solid && facePick.kind == PickKind::Face,
                    "clicking the top face picks a face, not a triangle");
        ok &= Check(CgSucceeded(viewport.SetWorkPlaneFromPick(facePick)),
                    "and that face becomes the work plane");
        WorkPlane onFace{};
        viewport.GetWorkPlane(onFace);
        ok &= Check(std::abs(onFace.normal.z - 1.0) < 1e-6,
                    "the work plane took the face's own normal");
    }

    // 工作平面挪到了实体顶面上，后面的绘制都会落在那儿；截图要的是原来那张图，
    // 所以放回 XY 平面。
    viewport.SetWorkPlane(WorkPlane{Vec3d{0, 0, 0}, Vec3d{0, 0, 1}, Vec3d{1, 0, 0},
                                    Vec3d{0, 1, 0}});

    // 同一个工具，换一种轮廓：中间那个 22 mm 的孔拉成一个凸台。圆的侧面是**一个**
    // 光滑面而不是六十四个四边形，所以圆柱身上不该有一根竖线 —— 这正是 M4 里
    // 「圆→圆柱、矩形→立方体」两条验收标准的另一半。
    const EntityId bore = scene.GetEntityAt(1);  // BuildDemoDrawing 里的第二个图元。
    selection.Set(CgSpan<const EntityId>{&bore, 1});
    tools.Activate(ToolId::Select);
    tools.Activate(ToolId::Extrude);
    send(Vec3d{0, 0, 0}, MouseAction::Move);
    send(Vec3d{0, 0, 18}, MouseAction::Move);
    send(Vec3d{0, 0, 18}, MouseAction::Down);

    ok &= Check(scene.GetEntityCount() == before + 4, "a circle extruded into a cylinder");
    const EntityId boss = scene.GetEntityAt(scene.GetEntityCount() - 1);
    ok &= Check(scene.GetEntity(boss)->GetShapeType() == ShapeType::Solid,
                "and it is a solid too");
    Aabb bossBounds{};
    ok &= Check(scene.GetEntity(boss)->GetWorldBounds(bossBounds) &&
                    std::abs(bossBounds.max.z - 18.0) < 1e-6,
                "to exactly the height the cursor was at");

    // 有意停在半途：这个圆永远不会变成形状。预览几何既不进场景也不进撤销栈（§6.2）。
    ok &= Check(CgSucceeded(tools.Activate(ToolId::Circle)), "activated the circle tool");
    send(Vec3d{40, 55, 0}, MouseAction::Move);
    send(Vec3d{40, 55, 0}, MouseAction::Down);
    send(Vec3d{62, 55, 0}, MouseAction::Move);
    ok &= Check(scene.GetEntityCount() == before + 4,
                "a rubber-band preview stays out of the scene");

    // -- M3：先选中，再拖 Gizmo ---------------------------------------------
    //
    // Gizmo 的手柄是按固定像素尺寸摆在屏幕空间里的，所以宿主要瞄准一个手柄，办法
    // 是把它的原点投到屏幕上、再沿投影后的轴走几个像素。这不需要知道引擎内部的手柄
    // 尺寸 —— 真实用户的鼠标掌握的信息也就这么多。
    ok &= Check(CgSucceeded(tools.Activate(ToolId::Select)), "activated the select tool");

    // 点在上面那条线身上。
    send(Vec3d{0, -60, 0}, MouseAction::Move);
    send(Vec3d{0, -60, 0}, MouseAction::Down);
    send(Vec3d{0, -60, 0}, MouseAction::Up);
    ok &= Check(selection.GetCount() == 1, "clicking an object selected it");
    const EntityId picked = selection.GetAt(0);

    Aabb selectionBounds{};
    ok &= Check(selection.GetBounds(selectionBounds), "the selection reports its bounds");
    const Vec3d gizmoOrigin{(selectionBounds.min.x + selectionBounds.max.x) * 0.5,
                            (selectionBounds.min.y + selectionBounds.max.y) * 0.5,
                            (selectionBounds.min.z + selectionBounds.max.z) * 0.5};

    Transform beforeDrag{};
    scene.GetEntity(picked)->GetLocalTransform(beforeDrag);

    // +X 手柄在屏幕上的方向。沿轴取任意一段世界偏移都行 —— 这里只要方向。
    Vec2d originPixel{};
    Vec2d alongPixel{};
    const bool projected =
        camera.WorldToScreen(gizmoOrigin, originPixel) &&
        camera.WorldToScreen(Vec3d{gizmoOrigin.x + 10.0, gizmoOrigin.y, gizmoOrigin.z},
                             alongPixel);
    ok &= Check(projected, "the gizmo origin and its X axis project to the screen");

    if (projected) {
        double dx = alongPixel.x - originPixel.x;
        double dy = alongPixel.y - originPixel.y;
        const double length = std::sqrt(dx * dx + dy * dy);
        dx /= length;
        dy /= length;

        const auto atPixel = [&](double pixels, MouseAction action) {
            MouseEvent e{};
            e.button = MouseButton::Left;
            e.action = action;
            e.x = originPixel.x + dx * pixels;
            e.y = originPixel.y + dy * pixels;
            return viewport.OnMouseEvent(e);
        };

        ok &= Check(CgSucceeded(tools.Activate(ToolId::Move)), "activated the move tool");
        // 沿轴 30 像素稳稳落在手柄上；90 像素是拖到的位置。
        ok &= Check(atPixel(30.0, MouseAction::Down), "grabbed the X translate handle");
        atPixel(90.0, MouseAction::Move);
        atPixel(90.0, MouseAction::Up);

        Transform afterDrag{};
        scene.GetEntity(picked)->GetLocalTransform(afterDrag);
        ok &= Check(afterDrag.translation.x > beforeDrag.translation.x + 1e-6,
                    "dragging the X handle moved the object along X only");
        ok &= Check(std::abs(afterDrag.translation.y - beforeDrag.translation.y) < 1e-9 &&
                        std::abs(afterDrag.translation.z - beforeDrag.translation.z) < 1e-9,
                    "and left the other two axes alone");

        // 一次拖拽是一步撤销，而不是每个鼠标移动一步（§6.5）。
        ICommandStack& stack = *scene.GetCommandStack();
        ok &= Check(std::string(stack.PeekUndoName()) == "Move", "the drag pushed one Move");
        ok &= Check(CgSucceeded(stack.Undo()), "and it undoes");
        Transform undone{};
        scene.GetEntity(picked)->GetLocalTransform(undone);
        ok &= Check(std::abs(undone.translation.x - beforeDrag.translation.x) < 1e-9,
                    "undo put the object back where it was");
        stack.Redo();
    }

    // -- M6：垂足吸附 --------------------------------------------------------
    //
    // 从空白处的一个点向竖直中心线（x = 0，y 从 -50 到 50）画一条线，第二个点瞄在
    // 垂足附近但不精确。垂足吸附该把它拉到 (0, 30) —— 从第一个点作垂线的那个落点，
    // 而不是光标底下那个差了一点的位置。M3 里这件事做不到，因为签名里没有地方传
    // 「第一个点」（§6.3）。
    {
        tools.Activate(ToolId::Line);
        const Vec3d start{-40.0, 30.0, 0.0};
        send(start, MouseAction::Move);
        send(start, MouseAction::Down);
        send(start, MouseAction::Up);

        // 差了几个像素 —— 吸附本来就是「点得差不多」时把光标拉到位，所以目标必须
        // 落在容差之内，垂足也不例外。
        const Vec3d askew{0.35, 29.65, 0.0};
        send(askew, MouseAction::Move);
        send(askew, MouseAction::Down);

        ShapeParams perpendicular{};
        const EntityId perpLine = scene.GetEntityAt(scene.GetEntityCount() - 1);
        const bool got = scene.GetGeometryBuilder()->GetParams(perpLine, perpendicular) &&
                         perpendicular.type == ShapeType::Line;
        ok &= Check(got && std::abs(perpendicular.line.end.x) < 1e-9 &&
                        std::abs(perpendicular.line.end.y - start.y) < 1e-9,
                    "a click near a line snapped to the foot of the perpendicular");
        scene.GetCommandStack()->Undo();
    }

    // -- M6：测量 ------------------------------------------------------------
    //
    // 唯一一个什么都不改的工具。点两个已知的点，结果从扩展接口读回来。
    {
        auto* ext = static_cast<ICadEngine2*>(engine.GetExtension(ExtensionId_Engine2));
        const uint32_t entitiesBefore = scene.GetEntityCount();
        const uint32_t undoBefore = scene.GetCommandStack()->GetUndoCount();

        tools.Activate(ToolId::Measure);
        const Vec3d a{-60.0, -40.0, 0.0};
        const Vec3d b{-60.0, 0.0, 0.0};
        send(a, MouseAction::Move);
        send(a, MouseAction::Down);
        send(b, MouseAction::Move);
        send(b, MouseAction::Down);

        Vec3d from{};
        Vec3d to{};
        double distance = 0.0;
        ok &= Check(ext && ext->GetMeasurement(from, to, distance) &&
                        std::abs(distance - 40.0) < 1e-6,
                    "measuring two points reports the distance between them");
        ok &= Check(scene.GetEntityCount() == entitiesBefore &&
                        scene.GetCommandStack()->GetUndoCount() == undoBefore,
                    "and it left the scene and the undo stack untouched");

        if (ext) {
            char reading[64];
            ext->FormatLength(distance, reading, sizeof(reading));
            std::printf("         %s\n", reading);
        }
    }

    // -- M6：缩放 Gizmo ------------------------------------------------------
    //
    // 和 Move 的手法一样：把 Gizmo 原点投到屏幕上，沿投影后的 X 轴走几个像素抓住
    // 手柄，再往外拖。等比手柄在原点上，轴手柄沿着轴。
    {
        selection.Set(CgSpan<const EntityId>{&rectangle, 1});
        Aabb scaleBounds{};
        if (selection.GetBounds(scaleBounds)) {
            const Vec3d origin{(scaleBounds.min.x + scaleBounds.max.x) * 0.5,
                               (scaleBounds.min.y + scaleBounds.max.y) * 0.5,
                               (scaleBounds.min.z + scaleBounds.max.z) * 0.5};
            Vec2d scaleOrigin{};
            Vec2d scaleAlong{};
            if (camera.WorldToScreen(origin, scaleOrigin) &&
                camera.WorldToScreen(Vec3d{origin.x + 10.0, origin.y, origin.z}, scaleAlong)) {
                double sx = scaleAlong.x - scaleOrigin.x;
                double sy = scaleAlong.y - scaleOrigin.y;
                const double axisLength = std::sqrt(sx * sx + sy * sy);
                sx /= axisLength;
                sy /= axisLength;

                const auto atPixel = [&](double pixels, MouseAction action) {
                    MouseEvent e{};
                    e.button = MouseButton::Left;
                    e.action = action;
                    e.x = scaleOrigin.x + sx * pixels;
                    e.y = scaleOrigin.y + sy * pixels;
                    return viewport.OnMouseEvent(e);
                };

                Transform beforeScale{};
                scene.GetEntity(rectangle)->GetLocalTransform(beforeScale);

                ok &= Check(CgSucceeded(tools.Activate(ToolId::Scale)), "activated the scale tool");
                ok &= Check(atPixel(40.0, MouseAction::Down), "grabbed the X scale handle");
                atPixel(80.0, MouseAction::Move);
                atPixel(80.0, MouseAction::Up);

                Transform afterScale{};
                scene.GetEntity(rectangle)->GetLocalTransform(afterScale);
                ok &= Check(afterScale.scale.x > beforeScale.scale.x + 1e-6,
                            "dragging the X handle scaled the object along X only");
                ok &= Check(std::abs(afterScale.scale.y - beforeScale.scale.y) < 1e-9,
                            "and left the other two axes alone");
                ok &= Check(std::string(scene.GetCommandStack()->PeekUndoName()) == "Scale",
                            "the drag pushed one Scale");
                scene.GetCommandStack()->Undo();
            }
        }
    }

    // 结束时停在「Move 工具 + 一个非空选择集」上，截图里因此带着 M3 的验收内容：
    // 高亮的对象，和落在它身上的 Gizmo。
    selection.Set(CgSpan<const EntityId>{&picked, 1});
    tools.Activate(ToolId::Move);
    return ok;
}

bool RunViewport(cadgeom::ICadEngine& engine, const Options& options) {
    using namespace cadgeom;
    bool ok = true;

    ViewportDesc desc{};
    desc.surface.kind = options.headless ? SurfaceKind::Headless : SurfaceKind::Glfw;
    desc.surface.width = options.width;
    desc.surface.height = options.height;
    desc.surface.title = "CadGeom — M6";
    desc.projection =
        options.perspective ? ProjectionMode::Perspective : ProjectionMode::Orthographic;
    // Linear light: the renderer composes in a float target and the swapchain
    // blit applies the sRGB encode, so this is darker on screen than it looks.
    desc.background = Color{0.035f, 0.038f, 0.045f, 1.0f};
    desc.showGrid = true;
    desc.vsync = true;
    desc.sampleCount = options.samples;

    IViewport* viewport = engine.CreateViewport(desc);
    if (!viewport) {
        std::printf("  [FAIL] CreateViewport: %s\n", engine.GetLastErrorMessage());
        return false;
    }
    if (options.hiddenLine) {
        viewport->SetRenderMode(RenderMode::HiddenLine);
    }
    ok &= Check(true, options.headless ? "headless viewport created" : "window created");
    std::printf("         device: %s\n", engine.GetDeviceName());
    ok &= Check(engine.GetViewportCount() == 1, "the engine tracks its viewport");

    auto* ext = static_cast<ICadEngine2*>(engine.GetExtension(ExtensionId_Engine2));
    if (ext && options.samples > 1) {
        // 设备支持不到那么多采样时会被降下来，所以要的和拿到的不一定是同一个数。
        const uint32_t got = ext->GetSampleCount(viewport);
        ok &= Check(got >= 1 && got <= options.samples, "the viewport reports its MSAA level");
        std::printf("         %ux MSAA requested, %ux in use\n", options.samples, got);
    }

    // 第二个视口：同一个设备、同一份几何、同一个场景，但相机、渲染模式和 HUD 各自
    // 独立。它是「多视口」这条验收标准的样子，也是一张四视图布局的起点（M6）。
    std::vector<IViewport*> extras;
    for (uint32_t i = 1; i < options.viewports; ++i) {
        ViewportDesc second = desc;
        second.surface.title = "CadGeom — M6 (front elevation / hidden line)";
        second.projection = ProjectionMode::Orthographic;
        IViewport* other = engine.CreateViewport(second);
        if (!other) {
            std::printf("  [FAIL] second CreateViewport: %s\n", engine.GetLastErrorMessage());
            ok = false;
            break;
        }
        // 正视图 + 隐藏线，也就是一张工程立面图：表面只写深度不上色，被挡住的边照画
        // 但画成虚线。圆柱背面那半圈底环因此看得见，而且看得出它在背面。整件事和第
        // 一个视口的着色模式同时存在 —— 视口之间共享的只有设备和几何。
        other->GetCamera()->SetStandardView(StandardView::Front);
        other->SetRenderMode(RenderMode::HiddenLine);
        extras.push_back(other);
    }
    if (options.viewports > 1) {
        ok &= Check(engine.GetViewportCount() == options.viewports,
                    "the engine tracks every viewport");
    }

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

    Aabb sceneBounds{};
    if (!options.importPath.empty()) {
        // M5：拿一个真实文件当输入。导入的网格没有参数化定义可谈，特征边是从三角形
        // 里按折角认出来的 —— 一个导进来的立方体因此还是长得像立方体，而不是一团
        // 没有轮廓的灰。
        Section("Import");
        ImportOptions importOptions{};
        ok &= Check(CgSucceeded(engine.GetIoRegistry()->Import(options.importPath.c_str(),
                                                              importOptions, nullptr, nullptr)),
                    "imported the model");
        if (!ok) {
            std::printf("         %s\n", engine.GetLastErrorMessage());
        }
        std::printf("         %u entit(y|ies)\n", engine.GetScene()->GetEntityCount());
        ok &= Check(engine.GetScene()->GetBounds(sceneBounds), "the scene reports its bounds");
        camera->SetStandardView(StandardView::Isometric);
        camera->ZoomToFit(nullptr, 1.25);
    } else {
        Section("Drawing");
        BuildDemoDrawing(*engine.GetScene());
        ok &= Check(engine.GetScene()->GetEntityCount() == 16, "built the demo drawing");
        ok &= Check(engine.GetScene()->GetBounds(sceneBounds), "the scene reports its bounds");
        // Top reads as a drawing, which is what the orthographic default is for;
        // isometric is where the geometry on the other two work planes shows up.
        camera->SetStandardView(options.perspective ? StandardView::Isometric : StandardView::Top);
        camera->ZoomToFit(nullptr, 1.25);

        // The tool drive knows which entity is which by index, so it only makes
        // sense on the drawing it was written against.
        Section("Tools");
        ok &= DriveTools(engine, *viewport);
    }

    // 每个视口各自把画面框好。相机是视口自己的，所以这件事得逐个来 —— 而这正是
    // 「多视口」意味着什么：共享几何，各看各的。
    for (IViewport* other : extras) {
        other->GetCamera()->ZoomToFit(nullptr, 1.25);
    }

    if (!options.exportPath.empty()) {
        Section("Export");
        ExportOptions exportOptions{};
        const CgResult written = engine.GetIoRegistry()->Export(options.exportPath.c_str(),
                                                                exportOptions, nullptr, nullptr);
        ok &= Check(CgSucceeded(written), "wrote the scene");
        std::printf("         %s\n", CgSucceeded(written) ? options.exportPath.c_str()
                                                          : engine.GetLastErrorMessage());
    }

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

        // 一次 Tick 之后每个视口各画一帧：脏几何只解析一次、只上传一次，剩下的
        // 就是每个视口自己那份相机与显示状态（docs/architecture.md §4.1）。
        lastRender = viewport->Render();
        for (IViewport* other : extras) {
            if (CgSucceeded(lastRender)) {
                lastRender = other->Render();
            }
        }
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
        // 关掉任何一个窗口就整个退出：demo 只有一份帧循环，一个死掉的窗口留在那儿
        // 只会看着像卡住了。
        bool closed = false;
        for (const IViewport* other : extras) {
            closed = closed || (!options.headless && other->ShouldClose());
        }
        if (closed) {
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

        // 每个额外视口写一张自己的图，文件名加后缀。两张图放在一起就是「多视口」
        // 这条验收标准：同一个场景、两套显示状态。
        for (size_t i = 0; i < extras.size(); ++i) {
            std::string path = options.screenshotPath;
            const size_t dot = path.rfind('.');
            const std::string suffix = "_vp" + std::to_string(i + 2);
            path.insert(dot == std::string::npos ? path.size() : dot, suffix);
            if (CgSucceeded(extras[i]->SaveScreenshot(path.c_str()))) {
                std::printf("         %s\n", path.c_str());
            } else {
                ok = false;
                std::printf("  [FAIL] %s: %s\n", path.c_str(), engine.GetLastErrorMessage());
            }
        }
    }

    for (IViewport* other : extras) {
        other->Release();
    }
    viewport->Release();
    ok &= Check(engine.GetViewportCount() == 0, "releasing the viewports unregistered them");
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
    // Pay the file layer's one-time costs before the checkpoint. Reading and
    // writing pull in locale facets that the C++ runtime keeps until the process
    // exits — a real allocation, but not a leak, and not one the engine can
    // return. Charging it to the engine would make this check cry wolf, and a
    // check that cries wolf gets switched off.
    WarmUpFileIo(desc);

    // The CRT's own report goes to the debugger by default, which is nowhere at
    // all when this runs in a console. Send it to stdout so a failure below
    // arrives with the evidence attached.
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);

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
