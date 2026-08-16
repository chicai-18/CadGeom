# CadGeom — working notes

Embeddable Vulkan CAD 3D engine, shipped as one shared library behind a
COM-style C++ interface. The full design is in `docs/architecture.md`; read it
before making structural decisions. This file holds the rules that are easy to
break by accident.

## Current state

Milestone **M2 complete** (geometry kernel, lines and tools). See
`docs/architecture.md` §9 for the plan.

| | Status |
|---|---|
| Public headers (`include/cadgeom/`) | Complete — the contract is frozen from here |
| Engine lifecycle, scene graph, selection, undo stack, IO registry | Working |
| Vulkan context/swapchain/RHI, orbit camera, screenshots | Working |
| `GridPass` `MeshPass` `LinePass` `PointPass` + overlay previews | Working |
| `ISurface`: GLFW, native Win32, headless | Working (X11/Wayland/Cocoa report `NotSupported`) |
| `SimpleKernel`: point/line/circle/arc/rectangle/polyline, tessellation, bounds | Working |
| WorkPlane, `IToolContext`, Select/Point/Line/Circle/Rectangle/Polyline tools | Working |
| BVH, picking, gizmos, snapping to geometry | **M3** — `Pick`, `Raycast`, `SetWorkPlaneFromPick` return `NotImplemented`; only `Snap_Grid` works |
| Extrude, topology, `EdgePass` | **M4** — `Extrude` returns `NotImplemented` |
| OBJ / glTF handlers | **M5** |
| MSAA, ImGui panels, overlay text | **M6** — `ViewportDesc::sampleCount` is ignored with a warning |

Anything not built yet fails with `CgResult::NotImplemented` and an error
message naming the milestone. Keep that habit: a host wiring itself up against
these headers should never have to guess whether it hit a bug or a gap.

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

One consequence already bites: `ShapeParams` is a POD union with nowhere to put
a polyline's variable-length point list. `GetParams` on a polyline therefore
returns its `type` and an unused union, and `SetParams` returns `NotSupported`.
The kernel's own `geom::ShapeDef` carries the points; that is the internal type
and it is free to hold STL.

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
do what a host-written tool could do. Whatever a tool needs that `IToolContext`
does not carry goes in `interact::ToolSettings`, which the tool manager owns.

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
  geometry.
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
- **Lines and points are expanded to screen-space quads in the vertex shader**,
  never `VK_POLYGON_MODE_LINE`. `wideLines` is optional, most drivers clamp to
  1.0, and neither can dash. Dash phase comes from per-segment arc length times
  `lineParams.x` (pixels per world unit), which is exact in orthographic and
  approximate in perspective.
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
```

Targets: `cadgeom` (the only shipped artifact), `cadgeom_tests`, `glfw_viewer`.

In the window, `V X L C R Y` pick the Select/Point/Line/Circle/Rectangle/
Polyline tools and Esc cancels. Those bindings live in `ViewportImpl` rather than
in the demo because with `SurfaceKind::Glfw` the *engine* owns the window, so the
host never sees the key.

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
