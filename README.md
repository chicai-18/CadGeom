# CadGeom

An embeddable CAD 3D engine: Vulkan-rendered, shipped as a single shared library
behind a stable C++ interface, designed to drop into a Qt/MFC/Win32 host or run
standalone.

Design document: [`docs/architecture.md`](docs/architecture.md).

> **Status: milestone M4 (extrude into solids).** The public API is
> complete and frozen. Working: the Vulkan renderer, the geometry kernel,
> points/lines/arcs/circles/rectangles/polylines, screen-space line rendering with
> dash patterns, the work plane, the interactive tools, ray picking through a BVH
> with vertex/edge/face priority, geometry snapping, selection highlighting,
> translate/rotate gizmos, undo/redo, and extrusion into solids with topology,
> per-face picking and feature-edge rendering. Not built yet: OBJ/glTF (M5), MSAA
> and UI panels (M6). Calls into those return `CgResult::NotImplemented` with a
> message naming the milestone that brings them.

## Building

Requires CMake >= 3.24 and a C++20 compiler. Tested with MSVC 19.38 (VS 2022).

```sh
git clone <url> CadGeom && cd CadGeom
git submodule update --init --recursive

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel

ctest --test-dir build -C Debug --output-on-failure
./build/bin/Debug/glfw_viewer.exe
```

The [Vulkan SDK](https://vulkan.lunarg.com/) (>= 1.3.275) is the only dependency
not vendored. Without it the build simply omits the renderer and tells you so —
the kernel, the scene, the tools and the whole test suite build and run fine on a
machine that has never seen it. Everything else lives in `external/` as a pinned
git submodule.

```sh
# No display, or checking a render change without eyeballing a window:
./build/bin/Debug/glfw_viewer.exe --headless --screenshot shot.png
./build/bin/Debug/glfw_viewer.exe --headless --perspective --screenshot iso.png
```

| Option | Default | |
|---|---|---|
| `CADGEOM_BUILD_TESTS` | `ON` | Unit tests. No Vulkan dependency, CI-friendly. |
| `CADGEOM_BUILD_EXAMPLES` | `ON` | The standalone demo. |
| `CADGEOM_ENABLE_VULKAN` | `ON` | Auto-disabled when no SDK is found. |
| `CADGEOM_WARNINGS_AS_ERRORS` | `OFF` | |

## Using it from a host

```cmake
find_package(CadGeom REQUIRED)
target_link_libraries(my_app PRIVATE CadGeom::cadgeom)
```

```cpp
#include <cadgeom/CadGeom.h>

// Refuse to run against a library that does not match these headers, rather
// than crashing three vtable slots later.
if (!CadGeom_IsApiVersionCompatible(CADGEOM_API_VERSION)) {
    return report("CadGeom version mismatch");
}

cadgeom::EngineDesc desc{};
desc.applicationName = "MyCad";
desc.enableValidation = true;

cadgeom::ICadEngine* engine = CadGeom_CreateEngine(desc);
if (!engine) {
    return report(CadGeom_GetCreateEngineError());
}

cadgeom::IScene* scene = engine->GetScene();
const cadgeom::EntityId part = scene->CreateGroup("Part", cadgeom::kInvalidEntity);

engine->Release();   // never `delete` — the DLL owns the allocation
```

`include/cadgeom/CadGeomRAII.h` is an optional header-only layer that wraps the
raw pointers in `unique_ptr`-style guards. It compiles entirely on the host side
and has no effect on the ABI, so use it or ignore it as you prefer.

## Layout

```
include/cadgeom/   public headers — the only contract
src/core/          double math, logging, errors, object tracking
src/geom/          the kernel: parametric curves, tessellation, intersection
src/scene/         the BVH that picking and snapping query
src/render/        Vulkan RHI, passes, surfaces
src/interact/      camera, work plane, picker, snapping, gizmo, tools, overlay
src/api/           interface implementations + the exported factory
shaders/           GLSL, compiled to SPIR-V and embedded in the DLL
examples/          standalone demo
tests/             unit tests (never depend on Vulkan)
external/          pinned submodules
docs/              architecture
```

Dependencies point down only: `api/ → interact/ → scene · io · render → geom → core`.

## Design notes

- **Z-up, right-handed**, the CAD convention. glTF's Y-up is converted at the IO
  boundary.
- **`double` everywhere above the GPU.** Coordinates get narrowed to float only
  at upload, relative to the camera, so a model at survey coordinates does not
  jitter.
- **Parametric definitions are the source of truth**; meshes are a derived cache.
  Editing a circle's radius regenerates geometry rather than moving triangles.
- **Every scene mutation is a command**, so undo/redo is not something to retrofit.
  A whole gizmo drag is one step: the transform changes live, and the command is
  pushed on mouse-up.
- **Picking is CPU-side**, a `double` ray against a BVH, so it can distinguish a
  vertex from an edge from a face and feed snapping — semantics a GPU ID buffer
  cannot give.
- The interface obeys a strict ABI discipline (no STL across the boundary, no
  exceptions, append-only vtables) documented in `docs/architecture.md` §2.2 and
  summarised in `CLAUDE.md`.
