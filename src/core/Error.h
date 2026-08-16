// Thread-local error channel — the replacement for exceptions at the boundary
// (docs/architecture.md §2.2 rule 4).
//
// State is per-thread and module-wide rather than per-engine: a failure inside
// CadGeom_CreateEngine has no engine to hang itself off, and two engines on two
// threads must not clobber each other's diagnostics.
#pragma once

#include <cadgeom/Types.h>

namespace cadgeom::core {

/// Records the failure on this thread and returns `result` so call sites can
/// write `return SetError(CgResult::InvalidArgument, "radius must be > 0");`.
CgResult SetError(CgResult result, const char* fmt, ...) noexcept;

CgResult LastError() noexcept;

/// Never null; empty string when nothing has failed. Valid until the next
/// SetError on the same thread.
const char* LastErrorMessage() noexcept;

void ClearError() noexcept;

} // namespace cadgeom::core
