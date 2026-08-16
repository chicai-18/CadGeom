// CadGeom — engine root and the DLL's factory entry points.
#ifndef CADGEOM_IENGINE_H
#define CADGEOM_IENGINE_H

#include <cadgeom/Export.h>
#include <cadgeom/Types.h>

namespace cadgeom {

class IIoRegistry;
class IScene;
class IToolManager;
class IViewport;

/// The root object. One per host process is the norm, but several may coexist.
///
/// Everything reachable from here is owned by the engine and dies with it —
/// except viewports, which the host may release early. Never `delete` an
/// interface: the DLL allocated it and only the DLL can free it correctly
/// (docs/architecture.md §2.2 rule 2).
class CADGEOM_API ICadEngine {
public:
    /// Destroys the engine and everything it owns, including viewports the
    /// host has not released. After this the pointer is dangling.
    virtual void Release() = 0;

    virtual IScene* GetScene() = 0;
    virtual const IScene* GetScene() const = 0;

    /// Creates a viewport on the surface described by `desc`. Null on failure;
    /// call GetLastErrorMessage() for the reason.
    virtual IViewport* CreateViewport(const ViewportDesc& desc) = 0;
    virtual uint32_t GetViewportCount() const = 0;
    virtual IViewport* GetViewportAt(uint32_t index) = 0;

    virtual IToolManager* GetToolManager() = 0;
    virtual IIoRegistry* GetIoRegistry() = 0;

    /// Advances animations, flushes dirty transforms, re-tessellates changed
    /// geometry and uploads to the GPU. Call once per frame before rendering
    /// viewports; rendering without ticking shows the previous frame's state.
    virtual void Tick(double deltaSeconds) = 0;

    // -- Errors (§2.2 rule 4: no exception crosses the boundary) ------------

    /// Result of the most recent failing call on this thread.
    virtual CgResult GetLastError() const = 0;
    /// UTF-8 detail for it, never null. Valid until the next failing call on
    /// the same thread.
    virtual const char* GetLastErrorMessage() const = 0;
    virtual void ClearLastError() = 0;

    // -- Diagnostics --------------------------------------------------------

    virtual void SetLogLevel(LogLevel level) = 0;
    virtual LogLevel GetLogLevel() const = 0;

    /// UTF-8 name of the selected GPU, or a placeholder before a viewport has
    /// forced device creation.
    virtual const char* GetDeviceName() const = 0;

    /// Extension slot. Reserved for post-1.0 growth so new capabilities can
    /// arrive without touching this vtable (§2.2 rule 3). Returns null today.
    virtual void* GetExtension(uint32_t interfaceId) = 0;

protected:
    virtual ~ICadEngine() = default;
};

} // namespace cadgeom

extern "C" {

/// Creates an engine. Returns null when `desc.apiVersion` is incompatible with
/// this binary, or when initialization fails.
///
/// This is the only allocation entry point; everything else hangs off the
/// returned object and is freed by ICadEngine::Release().
CADGEOM_API cadgeom::ICadEngine* CadGeom_CreateEngine(const cadgeom::EngineDesc& desc);

/// UTF-8 description of why the most recent CadGeom_CreateEngine returned null.
/// Never null. Valid until the next create attempt on the same thread.
CADGEOM_API const char* CadGeom_GetCreateEngineError(void);

/// Live interface objects allocated by this module. Diagnostics only: a host
/// that has released everything must see zero. Used by the test suite to hold
/// the "no leaks" line from the very first milestone.
CADGEOM_API uint64_t CadGeom_GetLiveObjectCount(void);

} // extern "C"

#endif // CADGEOM_IENGINE_H
