# CadGeom — working notes

Embeddable Vulkan CAD 3D engine, shipped as one shared library behind a
COM-style C++ interface. The full design is in `docs/architecture.md`; read it
before making structural decisions. This file holds the rules that are easy to
break by accident.

## Current state

Milestone **M6 complete** — the last one in the plan. Perpendicular snapping,
stroke-font overlay text and a self-drawn HUD, multi-viewport, fit-to-selection,
a display unit system, MSAA, hidden-line mode, and the Scale and Measure tools.
See `docs/architecture.md` §9 for the plan and §7.1 for what M6 actually landed.

| | Status |
|---|---|
| Public headers (`include/cadgeom/`) | Complete — the contract is frozen from here |
| Engine lifecycle, scene graph, selection, undo stack, IO registry | Working |
| Vulkan context/swapchain/RHI, orbit camera, screenshots, MSAA | Working |
| `GridPass` `MeshPass` `EdgePass` `LinePass` `PointPass` + overlay previews | Working |
| `ISurface`: GLFW, native Win32, headless | Working (X11/Wayland/Cocoa report `NotSupported`) |
| `SimpleKernel`: point/line/circle/arc/rectangle/polyline, tessellation, bounds | Working |
| WorkPlane, `IToolContext`, every `ToolId` including Scale and Measure | Working |
| BVH, `Raycast`/`Pick`/`SetWorkPlaneFromPick`, selection highlight, Move/Rotate/Scale gizmos | Working |
| Snapping, `Snap_Perpendicular` included | Working |
| `Extrude`, solid topology, per-face picking, `ExtrudeTool` | Working |
| OBJ / glTF / GLB read + write, `extras` round-trip, `ShapeType::Mesh` | Working — `ExportOptions::embedTextures` is inert, nothing has textures yet |
| All four `RenderMode`s, multi-viewport, `ICadEngine2` extension | Working |
| Overlay text, status line, HUD | Working — a stroke font through `LinePass`, ASCII only |

Every `ToolId` the engine knows now resolves to a tool, and nothing returns
`CgResult::NotImplemented` any more except `CreateViewport` in a build made
without the Vulkan SDK. Keep the habit anyway when adding something new: a host
wiring itself up against these headers should never have to guess whether it hit
a bug or a gap.

## The ABI rules — breaking one of these plants a landmine

Pure virtual interfaces work across a DLL boundary because MSVC and the Itanium
ABI both lay out "single inheritance, no virtual bases, all pure virtual" vtables
predictably. That is the entire foundation, and it holds only while all seven of
these hold (`docs/architecture.md` §2.2):

1. **No STL type in an interface.** Not `std::string`, not `std::vector`, not
   `std::shared_ptr`. Strings are UTF-8 `const char*`; arrays are
   `CgSpan<const T>`; results come back through out-parameters or index-based
   enumeration (`GetCount()` / `GetAt(i)`).
2. **The DLL allocates, the DLL frees.** Objects are destroyed through
   `Release()`, never `delete`. Interface destructors stay `protected` so the
   compiler enforces it. Objects the *host* allocates (`ICommand`, `ITool`,
   `IImporter`, `IExporter`) get a `Release()` of their own, so each side frees
   what it allocated.
3. **Interfaces are append-only.** Never reorder, remove or change the signature
   of a published virtual. Add at the end, bump `CADGEOM_API_VERSION_MINOR`, or
   introduce `IScene2 : IScene`.
4. **No exception crosses the boundary.** Return `CgResult`; put the detail in
   `core::SetError(...)`, which the host reads via `GetLastErrorMessage()`.
   `CadGeom_CreateEngine` wraps its allocation in a try/catch for this reason.
5. **Interfaces carry no data members and no non-virtual functions.**
6. **No inline code in an interface** — it would bake the host's build into the
   caller. (`CadGeomRAII.h` is exempt: it is host-side sugar over the same
   virtual calls and links nothing.)
7. **Version-check at startup.** `CadGeom_GetApiVersion()` versus the header's
   `CADGEOM_API_VERSION`. Major must match; the host's minor may lag, never lead.

`Types.h` is under the same freeze: hosts compile those layouts into their own
code. Adding a field to an existing struct is a breaking change.

Two consequences already bite:

- `ShapeParams` is a POD union with nowhere to put a polyline's variable-length
  point list. `GetParams` on a polyline therefore returns its `type` and an
  unused union, and `SetParams` returns `NotSupported`. The kernel's own
  `geom::ShapeDef` carries the points; that is the internal type and it is free
  to hold STL. The same gap is why `ExtrudeTool` can preview the top face of a
  circle or a rectangle exactly but draws only a height guide for a polyline —
  a tool sees what a host sees, and neither can read those points back. A *file*
  has no such limit: glTF's `extras` carries the point list, so a polyline
  survives a round trip intact even though the public getter cannot report it.
- `IToolContext::SnapAt` takes a pixel and nothing else, so there is nowhere to
  pass the reference point a perpendicular snap is measured *from*. M6 did not
  add a parameter: **the reference point does not belong to that call**, it
  belongs to the stroke being drawn, so it lives in `interact::ToolSettings` and
  is set by whichever tool just placed a point (or by a host through
  `ICadEngine2::SetSnapReference`). The frozen signature never moved, and
  `Snap_Perpendicular` works for built-in and host-written tools alike.

Growing the API past this point goes through the extension slot, not through the
published vtables. `ICadEngine::GetExtension(ExtensionId_Engine2)` hands back
`ICadEngine2` (`include/cadgeom/IEngineExt.h`) — display units, the snap
reference, status text, the HUD toggle, the real sample count, the last
measurement. It is an engine member, so it has **no `Release()`**: the host holds
a borrowed pointer. An id the binary does not know returns null, which is how a
host tells "old library" from "broken library". `ICadEngine2` itself is now
published, so it is append-only too; a future round adds `ICadEngine3` under a
new id.

## Layering

```
api/ → interact/ → scene/ · io/ · render/ → geom/ → core/
```

Dependencies point **down only**. `geom/` must not know `render/` exists;
`render/` must not know `interact/` exists. This is what keeps the kernel
unit-testable without standing up a Vulkan device — and `tests/` must stay
Vulkan-free for the same reason. A test may create an engine, but it must never
create a Glfw or Headless viewport: that would build a device.

`render/` is fed a `render::SceneSnapshot` (geometry plus draw items, still in
double) built by `api/`, rather than reaching up into the scene graph itself.
Preview geometry arrives the same way, as a `render::OverlayData` filled by
`interact::OverlayBuilder`. Both types live in `render/` because render/ is the
consumer and sits *below* the layers that fill them — that is the arrow pointing
down, not a violation of it.

The built-in tools reach the scene through the **public** interfaces
(`IScene`, `IGeometryBuilder`, `ISelection`), never through `api/*Impl`. That is
what keeps `interact/ → api/` from becoming a cycle, and it means a tool can only
do what a host-written tool could do — the gizmo's undo step is an `ICommand`
written against `IEntity::SetLocalTransform`, exactly what a host would write.
Whatever a tool needs that `IToolContext` does not carry goes in
`interact::ToolSettings`, which the tool manager owns.

Picking and snapping invert the same arrow. The entity table and the kernel live
in `api/`, above `interact/`, so `interact/` declares what it needs
(`IPickTargetSource`, `ISnapSource`) and `SceneImpl` implements them and hands
the data down. `scene::Bvh` indexes nothing but an `EntityId` and a world AABB,
which is why it can sit that low. `IScene` must stay `SceneImpl`'s **first** base:
the host's `IScene*` points at the primary subobject, and moving it would change
what that pointer means.

`io/` inverts it the same way, through `io::ISceneSource` (read) and
`io::ISceneSink` (write), implemented by `api::SceneIoBridge`. It needs its own
door because the public interfaces genuinely cannot serve it: `IScene` has no way
to read a mesh, and `IGeometryBuilder` has no `MakeMesh`. That is not an
oversight to patch — a host is handed a parametric CAD scene, and the triangles
under it are an internal cache. The bridge is a separate object rather than two
more bases on `SceneImpl` for the same reason as the rule above.

## Conventions

- **Z-up, right-handed.** glTF's Y-up is converted in the IO layer, nowhere else.
- **`double` above the GPU, `float` at it.** Upload is camera-relative
  (`worldPos - cameraOrigin` computed in double, then narrowed) so far-from-origin
  models do not jitter. Do not narrow earlier than the upload.
- **Matrices are column-major**, `m[column * 4 + row]`, matching GLSL.
- **Clip space is Vulkan's**: z in [0,1] and **+y down**. The flip lives in the
  projection matrix (`Mat4Ortho` / `Mat4Perspective`), not in a negative viewport
  height, so `GetProjectionMatrix`, `ScreenToRay` and `WorldToScreen` all agree
  with what was drawn.
- **The renderer composes in linear light** into a half-float target and blits to
  an sRGB swapchain, which is where the encode happens. `Color` values in
  `ViewportDesc` and `EntityStyle` are linear — a "dark grey" background is
  around 0.04, not 0.2.
- **Parameters are the source of truth**; meshes are a derived cache. Only
  `Tessellate` writes a mesh or a wire. Changing a radius marks the cache dirty
  and re-tessellates — it never edits triangles. Anything that invalidates a
  cache must also bump the scene revision, or the renderer keeps drawing the old
  geometry. The one shape with no parameters behind it is `ShapeType::Mesh`,
  whose triangles arrive from a file and live in `geom::ShapeDef::mesh` — the
  *definition*, not the cache. Its `Tessellate` copies them across and finds its
  feature edges; changing the chord tolerance cannot regenerate what nobody
  generated.
- **An importer that finds parameters ignores the triangles beside them.** A
  glTF node with `extras.cadgeom.shape` is rebuilt from the parameters and its
  mesh is dropped on the floor. Trusting the mesh instead would let a stale cache
  redefine the truth, which is the one direction this whole design forbids.
- **Every scene mutation goes through `ICommand`** so undo/redo stays free —
  including delete. Two consequences worth knowing:
  - A command that runs *while the stack is running one* (a host `Undo()` that
    calls `IScene::DestroyEntity`, say) is executed and released rather than
    recorded. `CommandStackImpl::Push` guards this; without it the nested push
    mutates the vector the outer call is walking.
  - Undo re-creates an entity under **the same `EntityId`**
    (`SceneImpl::CreateEntityInternal`'s `preferredId`). Anything else holding
    that id would otherwise be left pointing at nothing.
- **An entity owns its shape.** Destroying one hands the shape back to the
  kernel; an undoable delete calls `DetachShape()` first and takes ownership
  until it falls off the stack.
- **A solid carries its own copy of the profile it was swept from**
  (`geom::ShapeDef::profile`), not a reference to one. `ExtrudeParams::profile`
  is the *provenance* — the id may go stale the moment the host deletes the
  sketch, and a solid that dangled with it could not re-sweep when its height
  changed. That copy is also why `SetParams` refuses to swap the profile out:
  changing which loop is swept means a new extrusion, not an edited one.
- **A solid's feature edges are `Topology.edges`, not its triangle edges.** They
  ride in `SceneSnapshot::edgeItems` and go through `EdgePass` with
  `EntityStyle::edgeColor`; curves stay in `curveItems` and go through
  `LinePass`. A smooth face (a cylinder's side) contributes no vertical edges and
  no topology vertices at all — those points are tessellation artefacts, and
  emitting them would put a snap target every six degrees around a circle.
- **An imported mesh gets the same treatment, recovered from the triangles.**
  `geom::BuildFeatureEdges` welds by position, keeps the edges whose two faces
  meet at more than 30°, and chains them: an imported box yields twelve
  one-segment edges and eight corners, an imported cylinder two closed rings and
  no corners at all. The threshold is deliberately far above the ~12° between
  adjacent facets of a tessellated curve — anything tighter draws the
  tessellation instead of the part.
- **The depth offset that keeps edges off their own surface lives in `MeshPass`**,
  as a rasterizer depth bias pushing faces *back*. Its unit is the depth buffer's
  own resolution, so it works for a one-metre part and a one-millimetre part
  alike; a fixed NDC bias on the edges would let the far side of a small solid
  poke through the near side.
- **Lines and points are expanded to screen-space quads in the vertex shader**,
  never `VK_POLYGON_MODE_LINE`. `wideLines` is optional, most drivers clamp to
  1.0, and neither can dash. Dash phase comes from per-segment arc length times
  `lineParams.x` (pixels per world unit), which is exact in orthographic and
  approximate in perspective.
- **One import is one undo step, or none at all.** `io::ISceneSink::Begin` opens
  a command group and `End(false)` aborts it through
  `CommandStackImpl::AbortGroup`, which undoes and discards without leaving a
  redo entry — a half-read file must not be recoverable from the Redo menu. The
  exception is `ImportOptions::mergeIntoScene = false`, which clears the scene
  and the undo stack with it; that import cannot be undone, and the header says
  so.
- **Paths are UTF-8, so nothing opens a file with narrow `fopen`.** `core/File.h`
  widens on Windows, and both third-party readers are handed bytes rather than a
  filename for exactly that reason (`tinygltf`'s and `tinyobjloader`'s own file
  entry points take the active code page).
- **Units are a display concern, never a geometric one.** Everything inside the
  engine is model units; `UnitSettings` only decides how a number is *read*.
  Changing the display unit must not bump the scene revision and must not touch a
  vertex — it is the same one-way rule as params/mesh, applied to readouts.
  `core/Units.h` owns the conversion table; imperial goes through the
  international inch (1 in = 25.4 mm exactly).
- **Overlay text is a stroke font through `LinePass`, not a glyph atlas.** A
  screen-space quad expansion already exists, and a stroke font is line segments,
  so text needed no new pipeline, texture or descriptor — and it is what CAD
  drawings have always used. `interact/TextStroke.cpp` holds the glyphs: ASCII
  only, lowercase drawn as uppercase, unknown characters drawn as nothing. That
  is why HUD strings are English while the comments around them are Chinese.
- **The HUD is drawn by the engine; real panels are the host's.** ImGui is not
  linked anywhere (`docs/architecture.md` §7.1 explains why the demo cannot host
  it). A host builds its own panel from `ICadEngine2::GetStatusText()`,
  `GetMeasurement()` and `FormatLength()`, then turns the built-in HUD off with
  `SetHudVisible(false)`.
- **A pipeline is tied to its sample count**, so `render::RenderSystem` keeps one
  `PassSet` per sample count and builds them on demand. MSAA renders into a
  multisampled attachment and resolves into the same single-sample half-float
  target that was always there, so the blit and the screenshot path are unchanged.
  A requested count the device cannot do is rounded *down*, with a warning;
  `ICadEngine2::GetSampleCount()` reports what was actually used.
- **`RenderMode::HiddenLine` is two depth passes.** `MeshPass` draws with
  `colorWriteMask = 0` (invisible but still occluding), then edges and curves are
  drawn a second time with `VK_COMPARE_OP_GREATER` — the fragments that *fail*
  the depth test are exactly the occluded ones — as `LineStyle::Hidden`, before
  the visible pass. The surface depth bias stays on, so an edge lying on its own
  face is not mistaken for a hidden one.
- **Scale is about world axes, and says so when it cannot deliver.** The Scale
  gizmo composes its delta in world space and folds it back into the local
  transform, exactly like Move and Rotate — which means scaling a rotated entity
  along a world axis produces shear, and TRS cannot express shear. `Decompose`
  fails, the tool logs it and leaves the entity alone rather than inventing a
  plausible-looking wrong pose.
- Angles are radians unless the name says `Deg`.
- Every impl object embeds a `core::ObjectTracker`; `CadGeom_GetLiveObjectCount()`
  must return to its baseline after teardown. The tests and the demo both assert
  this — keep it that way when adding a new impl class.

### Comments — Chinese, in Doxygen form

Comments are written in **Chinese**, formatted for **Doxygen**. `///` above the
declaration; `@brief` for the one-line summary, then `@param` / `@return` /
`@note` / `@warning` as the thing being documented needs them. `/** … */` is for
file headers only (`@file`, `@brief`). Comments inside a function body are plain
`//` — still Chinese, still explaining *why* rather than restating the line.

Identifiers are not translated. Type names, function names, enum values
(`CgResult::InvalidArgument`), milestone tags (`M3`) and Vulkan/GLSL terms stay
in English and stay spelled exactly as the code spells them, so Doxygen can link
them and so a grep for a symbol still finds its documentation.

```cpp
/// @brief 在给定平面上创建一个圆，圆心即平面原点。
/// @param plane  所在平面；其法线决定圆的朝向，原点即圆心。
/// @param radius 半径（世界单位），必须 > 0。
/// @return 新实体的 id；失败时返回 kInvalidEntity，原因见
///         IEngine::GetLastErrorMessage()。
/// @note 可撤销 —— 内部压入 ICommand，调用方无需自行入栈。
virtual EntityId MakeCircle(const Plane& plane, double radius) = 0;
```

The existing headers still carry English `///` comments from M0–M2. Translate
the ones you are already editing; don't open a file just to translate it, and
don't mix languages inside a single comment block.

## Build

```sh
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
build/bin/Debug/glfw_viewer.exe

# No display, or checking a render change without eyeballing a window. The
# headless run also drives the tools through synthetic mouse events, so the
# interactive path is checked without a hand on the mouse.
build/bin/Debug/glfw_viewer.exe --headless --screenshot shot.png
build/bin/Debug/glfw_viewer.exe --headless --perspective --screenshot iso.png
build/bin/Debug/glfw_viewer.exe --perspective --frames 300

# M6, all headless-checkable: hidden-line mode, a chosen MSAA level, and a second
# viewport (front elevation + hidden line) that writes shot_vp2.png beside shot.png.
build/bin/Debug/glfw_viewer.exe --headless --hidden-line --screenshot hidden.png
build/bin/Debug/glfw_viewer.exe --headless --samples 8 --screenshot msaa.png
build/bin/Debug/glfw_viewer.exe --headless --viewports 2 --screenshot shot.png

# The file round trip, on the command line: write what the demo drew, then look
# at it again. --import replaces the built-in drawing (and the tool drive with
# it, since that one knows its entities by index).
build/bin/Debug/glfw_viewer.exe --headless --export part.glb --frames 1
build/bin/Debug/glfw_viewer.exe --import part.glb
```

Targets: `cadgeom` (the only shipped artifact), `cadgeom_tests`, `glfw_viewer`,
`qt_viewer`, `mfc_viewer`.

`qt_viewer` (`examples/qt_viewer/`, its own README) is the other half of the
surface story: `glfw_viewer` lets the engine own the window, this one embeds a
viewport in a host UI through `SurfaceKind::NativeWin32` — menus, toolbar, model
tree, engine log, status line, and every `ToolId` behind a shortcut. Qt is not
vendored, so it only builds when CMake can find it, and skips itself with a
message when it cannot:

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_PREFIX_PATH=D:/Qt/5.15.2/msvc2019_64
build/bin/Debug/qt_viewer.exe
# Same habit as the headless demo run: prove the embedded render path still
# works without a hand on the mouse. Exit code 2 means an object leaked.
# It opens on an empty scene, so the self-check asks for the sample drawing;
# without --sample the screenshot is a bare grid. The menu entry is the other
# way in ("文件 → 载入示例图纸", Ctrl+Shift+N).
build/bin/Debug/qt_viewer.exe --sample --screenshot shot.png --frames 60
```

Three things that bite when embedding, all written up in that README: the widget
must hand over a real HWND and stop painting (`WA_NativeWindow` +
`WA_PaintOnScreen` + a null `paintEngine()`); mouse coordinates and sizes go to
the engine in **physical** pixels (`GetClientRect`), not Qt's logical ones; and
`IToolManager::Activate` only calls `ITool::OnActivate` once a context exists, so
a host switching tools from a menu rather than from an event has to send one
mouse event first or the new tool never gets its `Reset()`.

`mfc_viewer` (`examples/mfc_viewer/`, its own README) is the same story told with
MFC instead of Qt: the same `SurfaceKind::NativeWin32` path, the same menus, and
the same self-check switches. It exists because the hard part of embedding is
never the engine side, it is the *host framework's* own rules, and two hosts side
by side separate the two. It needs the Visual Studio generator (`CMAKE_MFC_FLAG`
is a VS-generator feature) and the MFC component, which the default C++ workload
does **not** install; without either, CMake skips it with a message.

```sh
build/bin/Debug/mfc_viewer.exe --sample --screenshot shot.png --frames 60
```

Where Qt needs four widget attributes to hand a region over, MFC needs a window
class with no background brush; where Qt scales by `devicePixelRatio`, this one
declares per-monitor-v2 DPI awareness and client coordinates *are* physical
pixels; Win32 has no implicit mouse grab, so `SetCapture` is explicit. Two things
that bite are written up in that README: the `.rc` needs `#pragma
code_page(65001)` or `rc.exe` eats a byte after every Chinese character, and
`CToolBar::SetSizes` asserts on a 0×0 image size even though `TB_SETBITMAPSIZE(0,0)`
is the documented way to say "this toolbar has no images".

In the window, `V X L C R Y` pick the Select/Point/Line/Circle/Rectangle/
Polyline tools, `E` picks Extrude, `M` `T` `S` pick Move, Rotate and Scale (`R`
was taken, so `T` for turn), `D` picks Measure (`M` was taken, so `D` for
dimension), Esc cancels, and Ctrl+Z / Ctrl+Y undo and redo. `F` fits the
selection when there is one and the whole scene otherwise, `W` cycles the four
render modes, `H` toggles the HUD, `G` the grid, `P` the projection, `1`–`7` the
standard views. Those bindings live in `ViewportImpl` rather than in the demo
because with `SurfaceKind::Glfw` the *engine* owns the window, so the host never
sees the key.

Extruding needs a view that is not looking down the sweep axis: the height comes
from projecting the mouse ray onto that axis, and in Top view a Z extrusion has
no solution. Orbit first — the demo's headless run switches to isometric for
exactly this reason.

The Vulkan SDK is the one dependency not vendored. Without it — or without
`glslc`, since the shaders are compiled into the DLL — the build drops the
renderer and says so; the kernel, the scene, the tools and the whole test suite
still build and run. The renderer needs LunarG >= 1.3.275 with `VULKAN_SDK` set.
A driver-provided `vulkan-1.dll` is *not* the SDK.

Shaders live in `shaders/` and are compiled to SPIR-V and embedded as C arrays
by `cmake/CadGeomShaders.cmake`. Editing a `.glsl` include only triggers a
rebuild if it is listed in that function's `INCLUDES` — glslc does not report
its own dependencies.

Submodules are pinned to release tags and shallow-cloned. Each is wired into
CMake at the milestone that first needs it, listed in the root `CMakeLists.txt`.

tinyobjloader and tinygltf are header-plus-one-source libraries, and we
instantiate them ourselves (`src/io/TinyObjImpl.cpp`, `src/io/TinyGltfImpl.cpp`)
instead of linking their CMake targets. The switches that matter —
`TINYOBJLOADER_USE_DOUBLE`, and no stb_image in tinygltf — change the layout of
types both libraries expose, so every translation unit that sees those headers
must agree. That is what `io/ObjCommon.h` and `io/GltfCommon.h` are for: include
those, never the upstream header directly.
