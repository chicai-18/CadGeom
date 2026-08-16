// CadGeom — optional host-side conveniences.
//
// Nothing here crosses the ABI boundary: it is ordinary C++ that compiles into
// the *host* and only ever calls the same virtual functions you would call by
// hand. That is why this header is allowed templates and <memory> while the
// interface headers are not. Skip it entirely if you prefer raw pointers.
#ifndef CADGEOM_CADGEOM_RAII_H
#define CADGEOM_CADGEOM_RAII_H

#include <cadgeom/CadGeom.h>

#include <memory>
#include <utility>

namespace cadgeom {

/// Calls Release() instead of delete, which is the whole point (§2.2 rule 2).
struct ReleaseDeleter {
    template <typename T>
    void operator()(T* p) const noexcept {
        if (p) {
            p->Release();
        }
    }
};

using EnginePtr = std::unique_ptr<ICadEngine, ReleaseDeleter>;
using ViewportPtr = std::unique_ptr<IViewport, ReleaseDeleter>;

/// Creates an engine after checking that the loaded binary speaks the same API
/// version these headers were compiled against. Returns an empty pointer on
/// mismatch or failure; CadGeom_GetCreateEngineError() explains why.
inline EnginePtr CreateEngine(const EngineDesc& desc = EngineDesc{}) {
    if (!CadGeom_IsApiVersionCompatible(CADGEOM_API_VERSION)) {
        return EnginePtr{};
    }
    return EnginePtr{CadGeom_CreateEngine(desc)};
}

inline ViewportPtr CreateViewport(ICadEngine& engine, const ViewportDesc& desc) {
    return ViewportPtr{engine.CreateViewport(desc)};
}

/// Groups everything pushed during its lifetime into one undo step. Commit by
/// letting it go out of scope normally.
class UndoGroup {
public:
    UndoGroup(ICommandStack* stack, const char* utf8Name) : stack_(stack) {
        if (stack_) {
            stack_->BeginGroup(utf8Name);
        }
    }
    ~UndoGroup() {
        if (stack_) {
            stack_->EndGroup();
        }
    }

    UndoGroup(const UndoGroup&) = delete;
    UndoGroup& operator=(const UndoGroup&) = delete;

private:
    ICommandStack* stack_;
};

/// CgSpan from any contiguous range, so callers can keep using their own
/// containers right up to the boundary.
template <typename T>
inline CgSpan<const T> MakeSpan(const T* data, size_t count) {
    return CgSpan<const T>{data, count};
}

template <typename Container>
inline auto MakeSpan(const Container& c) -> CgSpan<const decltype(*c.data())> {
    using Elem = decltype(*c.data());
    return CgSpan<const Elem>{c.data(), c.size()};
}

template <typename T, size_t N>
inline CgSpan<const T> MakeSpan(const T (&arr)[N]) {
    return CgSpan<const T>{arr, N};
}

/// Human-readable CgResult, for logs and assertion messages.
inline const char* ToString(CgResult r) {
    switch (r) {
        case CgResult::Ok: return "Ok";
        case CgResult::Unknown: return "Unknown";
        case CgResult::NotImplemented: return "NotImplemented";
        case CgResult::InvalidArgument: return "InvalidArgument";
        case CgResult::InvalidHandle: return "InvalidHandle";
        case CgResult::NotFound: return "NotFound";
        case CgResult::NotSupported: return "NotSupported";
        case CgResult::OutOfMemory: return "OutOfMemory";
        case CgResult::InvalidState: return "InvalidState";
        case CgResult::VersionMismatch: return "VersionMismatch";
        case CgResult::IoError: return "IoError";
        case CgResult::ParseError: return "ParseError";
        case CgResult::GeometryError: return "GeometryError";
        case CgResult::RenderError: return "RenderError";
        case CgResult::DeviceLost: return "DeviceLost";
    }
    return "?";
}

} // namespace cadgeom

#endif // CADGEOM_CADGEOM_RAII_H
