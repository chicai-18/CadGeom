// CadGeom — embeddable Vulkan CAD engine.
//
// This is the whole public surface; including it is enough.
//
//   #include <cadgeom/CadGeom.h>
//
//   cadgeom::EngineDesc desc{};
//   desc.applicationName = "MyCad";
//   auto* engine = CadGeom_CreateEngine(desc);
//   ...
//   engine->Release();
//
// Contract rules that hosts and contributors both depend on
// (docs/architecture.md §2.2):
//
//   1. No STL type ever appears in an interface. Strings are UTF-8 `const char*`,
//      arrays are CgSpan<const T>, results come back through out-parameters.
//   2. The DLL allocates, the DLL frees. Call Release(), never delete.
//   3. Interfaces are append-only. Published vtable slots never move.
//   4. No exception crosses the boundary. Failures return CgResult.
//   5. Interfaces carry no data members and no non-virtual functions.
//   6. No inline code in an interface — it would freeze the host's build into
//      the caller.
//   7. Check CadGeom_GetApiVersion() against CADGEOM_API_VERSION at startup.
//
// For a friendlier host-side surface, CadGeomRAII.h wraps these raw pointers in
// scope guards. It is header-only and compiles entirely on the host side, so it
// costs nothing at the boundary.
#ifndef CADGEOM_CADGEOM_H
#define CADGEOM_CADGEOM_H

#include <cadgeom/Export.h>
#include <cadgeom/Version.h>
#include <cadgeom/Types.h>

#include <cadgeom/ICamera.h>
#include <cadgeom/ICommandStack.h>
#include <cadgeom/IEngine.h>
#include <cadgeom/IEngineExt.h>
#include <cadgeom/IEntity.h>
#include <cadgeom/IGeometryBuilder.h>
#include <cadgeom/IImportExport.h>
#include <cadgeom/IScene.h>
#include <cadgeom/ISelection.h>
#include <cadgeom/ITool.h>
#include <cadgeom/IViewport.h>

#endif // CADGEOM_CADGEOM_H
