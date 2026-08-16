# CadGeom — working notes

Embeddable Vulkan CAD 3D engine, shipped as one shared library behind a
COM-style C++ interface. The full design is in `docs/architecture.md`; read it
before making structural decisions. This file holds the rules that are easy to
break by accident.

## Current state

Milestone **M1 complete** (Vulkan renderer). See `docs/architecture.md` §9 for
the plan.

| | Status |
|---|---|
| Public headers (`include/cadgeom/`) | Complete — the contract is frozen from here |
| Engine lifecycle, scene graph, selection, undo stack, IO registry | Working |
| Vulkan context/swapchain/RHI, `GridPass` + `MeshPass`, orbit camera, screenshots | Working |
| `ISurface`: GLFW, native Win32, headless | Working (X11/Wayland/Cocoa report `NotSupported`) |
| Geometry kernel, tessellation, line/point passes, tools | **M2** — creation calls return `NotImplemented` |
| BVH, picking, gizmos | **M3** — `Pick`, `Raycast`, `SetWorkPlaneFromPick` return `NotImplemented` |
| Extrude, topology, `EdgePass` | **M4** |
| OBJ / glTF handlers | **M5** |
| MSAA, snapping, ImGui panels | **M6** — `ViewportDesc::sampleCount` is ignored with a warning |

Anything not built yet fails with `CgResult::NotImplemented` and an error
message naming the milestone. Keep that habit: a host wiring itself up against
these headers should never have to guess whether it hit a bug or a gap.

Until the kernel lands there is nothing in a scene to draw, so `MeshPass` falls
back to a placeholder cube (`EngineImpl::UpdateSnapshot`). It disappears on its
own as soon as the snapshot contains a real mesh — do not build on it.

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

## Layering

```
api/ → interact/ → scene/ · io/ · render/ → geom/ → core/
```

Dependencies point **down only**. `geom/` must not know `render/` exists;
`render/` must not know `interact/` exists. This is what keeps the kernel
unit-testable without standing up a Vulkan device — and `tests/` must stay
Vulkan-free for the same reason. A test may create an engine, but it must never
create a Glfw or Headless viewport: that would build a device.

`render/` is fed a `render::SceneSnapshot` (meshes plus draw items, still in
double) built by `api/`, rather than reaching up into the scene graph itself.
That indirection is what keeps the arrow pointing down.

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
  `Tessellate` writes a mesh. Changing a radius regenerates geometry — it never
  edits triangles.
- **Every scene mutation goes through `ICommand`** so undo/redo stays free.
- Angles are radians unless the name says `Deg`.
- Every impl object embeds a `core::ObjectTracker`; `CadGeom_GetLiveObjectCount()`
  must return to its baseline after teardown. The tests and the demo both assert
  this — keep it that way when adding a new impl class.

## Build

```sh
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
build/bin/Debug/glfw_viewer.exe

# No display, or checking a render change without eyeballing a window:
build/bin/Debug/glfw_viewer.exe --headless --screenshot shot.png
build/bin/Debug/glfw_viewer.exe --perspective --frames 300
```

Targets: `cadgeom` (the only shipped artifact), `cadgeom_tests`, `glfw_viewer`.

The Vulkan SDK is the one dependency not vendored. Without it — or without
`glslc`, since the shaders are compiled into the DLL — the build drops the
renderer and says so; everything through M0 still builds and runs. The renderer
needs LunarG >= 1.3.275 with `VULKAN_SDK` set. A driver-provided `vulkan-1.dll`
is *not* the SDK.

Shaders live in `shaders/` and are compiled to SPIR-V and embedded as C arrays
by `cmake/CadGeomShaders.cmake`. Editing a `.glsl` include only triggers a
rebuild if it is listed in that function's `INCLUDES` — glslc does not report
its own dependencies.

Submodules are pinned to release tags and shallow-cloned. Each is wired into
CMake at the milestone that first needs it, listed in the root `CMakeLists.txt`.
