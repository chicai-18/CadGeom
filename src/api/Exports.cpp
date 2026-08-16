// The module's C entry points — the only symbols a host resolves by name.
//
// Everything else is reached through vtables, which is what keeps the exported
// surface this small (docs/architecture.md §2).
#include <cadgeom/IEngine.h>
#include <cadgeom/Version.h>

#include "api/EngineImpl.h"
#include "core/Error.h"
#include "core/Log.h"
#include "core/ObjectTracker.h"

#include <new>
#include <string>

namespace {

#define CADGEOM_STR_(x) #x
#define CADGEOM_STR(x) CADGEOM_STR_(x)

#if defined(_MSC_VER)
#define CADGEOM_COMPILER "msvc " CADGEOM_STR(_MSC_VER)
#elif defined(__clang__)
#define CADGEOM_COMPILER "clang " __clang_version__
#elif defined(__GNUC__)
#define CADGEOM_COMPILER "gcc " CADGEOM_STR(__GNUC__) "." CADGEOM_STR(__GNUC_MINOR__)
#else
#define CADGEOM_COMPILER "unknown compiler"
#endif

#if defined(NDEBUG)
#define CADGEOM_CONFIG "Release"
#else
#define CADGEOM_CONFIG "Debug"
#endif

#define CADGEOM_VERSION_TEXT                                       \
    CADGEOM_STR(CADGEOM_API_VERSION_MAJOR)                         \
    "." CADGEOM_STR(CADGEOM_API_VERSION_MINOR) "." CADGEOM_STR(CADGEOM_API_VERSION_PATCH)

const char* BuildInfoString() {
    // Function-local static: built once, thread-safe since C++11, and the
    // pointer stays valid for the module's lifetime as the header promises.
    static const std::string s = "CadGeom " CADGEOM_VERSION_TEXT " (" CADGEOM_COMPILER
                                 ", " CADGEOM_CONFIG ", built " __DATE__ ")";
    return s.c_str();
}

} // namespace

extern "C" {

CADGEOM_API uint32_t CadGeom_GetApiVersion(void) {
    return CADGEOM_API_VERSION;
}

CADGEOM_API const char* CadGeom_GetBuildInfo(void) {
    return BuildInfoString();
}

CADGEOM_API int CadGeom_IsApiVersionCompatible(uint32_t hostApiVersion) {
    // Major must match exactly — a bump means a vtable moved. Minor may lag,
    // because the rule is append-only: a host built against an older minor
    // simply never calls the slots that were added after it.
    if (CADGEOM_VERSION_MAJOR(hostApiVersion) != CADGEOM_API_VERSION_MAJOR) {
        return 0;
    }
    return CADGEOM_VERSION_MINOR(hostApiVersion) <= CADGEOM_API_VERSION_MINOR ? 1 : 0;
}

CADGEOM_API cadgeom::ICadEngine* CadGeom_CreateEngine(const cadgeom::EngineDesc& desc) {
    using namespace cadgeom;

    core::ClearError();

    // The sink goes in first so the version check below, and anything the
    // constructor has to say, actually reaches the host's log.
    core::Logger::SetLevel(desc.logLevel);
    core::Logger::SetSink(desc.logCallback, desc.logUserData);

    if (!CadGeom_IsApiVersionCompatible(desc.apiVersion)) {
        core::SetError(CgResult::VersionMismatch,
                       "API version mismatch: host built against %u.%u.%u, this binary "
                       "implements %u.%u.%u",
                       CADGEOM_VERSION_MAJOR(desc.apiVersion),
                       CADGEOM_VERSION_MINOR(desc.apiVersion),
                       CADGEOM_VERSION_PATCH(desc.apiVersion), CADGEOM_API_VERSION_MAJOR,
                       CADGEOM_API_VERSION_MINOR, CADGEOM_API_VERSION_PATCH);
        core::Logger::SetSink(nullptr, nullptr);
        return nullptr;
    }

    if (desc.kernelType == KernelType::Occt) {
        core::SetError(CgResult::NotSupported,
                       "KernelType::Occt is reserved; this build ships only the built-in "
                       "SimpleKernel");
        core::Logger::SetSink(nullptr, nullptr);
        return nullptr;
    }

    // No exception may escape into the host (§2.2 rule 4), so the one
    // allocation that can throw is contained right here.
    api::EngineImpl* engine = nullptr;
    try {
        engine = new api::EngineImpl(desc);
    } catch (const std::bad_alloc&) {
        core::SetError(CgResult::OutOfMemory, "out of memory while creating the engine");
    } catch (...) {
        core::SetError(CgResult::Unknown, "unexpected failure while creating the engine");
    }

    if (!engine) {
        core::Logger::SetSink(nullptr, nullptr);
        return nullptr;
    }
    return engine;
}

CADGEOM_API const char* CadGeom_GetCreateEngineError(void) {
    return cadgeom::core::LastErrorMessage();
}

CADGEOM_API uint64_t CadGeom_GetLiveObjectCount(void) {
    return cadgeom::core::LiveObjectCount();
}

} // extern "C"
