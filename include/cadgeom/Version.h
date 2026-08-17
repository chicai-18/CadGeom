// CadGeom — API version contract.
//
// The host compiles against a header-defined CADGEOM_API_VERSION and asks the
// loaded DLL for its own via CadGeom_GetApiVersion(). A mismatch must be a hard
// error at startup: a loud message beats a mysterious crash three vtable slots
// later (see docs/architecture.md §2.2 rule 7).
#ifndef CADGEOM_VERSION_H
#define CADGEOM_VERSION_H

#include <stdint.h>

#include <cadgeom/Export.h>

// Vulkan-style packing: 10 bits major, 10 bits minor, 12 bits patch.
#define CADGEOM_MAKE_VERSION(major, minor, patch) \
    ((((uint32_t)(major)) << 22) | (((uint32_t)(minor)) << 12) | ((uint32_t)(patch)))

#define CADGEOM_VERSION_MAJOR(v) (((uint32_t)(v)) >> 22)
#define CADGEOM_VERSION_MINOR(v) ((((uint32_t)(v)) >> 12) & 0x3FFu)
#define CADGEOM_VERSION_PATCH(v) (((uint32_t)(v)) & 0xFFFu)

#define CADGEOM_API_VERSION_MAJOR 0
#define CADGEOM_API_VERSION_MINOR 2
#define CADGEOM_API_VERSION_PATCH 0

// Bump MINOR when appending to an interface, MAJOR when breaking one.
// The engine accepts a host whose MAJOR matches and whose MINOR is <= its own.
#define CADGEOM_API_VERSION                                         \
    CADGEOM_MAKE_VERSION(CADGEOM_API_VERSION_MAJOR,                 \
                         CADGEOM_API_VERSION_MINOR,                 \
                         CADGEOM_API_VERSION_PATCH)

extern "C" {

/// Version of the API the loaded binary actually implements.
CADGEOM_API uint32_t CadGeom_GetApiVersion(void);

/// Human-readable build string, e.g. "CadGeom 0.1.0 (msvc 19.38, Debug)".
/// Owned by the DLL; valid for the lifetime of the module.
CADGEOM_API const char* CadGeom_GetBuildInfo(void);

/// Non-zero when `hostApiVersion` can safely talk to this binary.
CADGEOM_API int CadGeom_IsApiVersionCompatible(uint32_t hostApiVersion);

} // extern "C"

#endif // CADGEOM_VERSION_H
