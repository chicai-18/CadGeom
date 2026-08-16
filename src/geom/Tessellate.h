// Exact geometry -> MeshData / PolylineData (docs/architecture.md §3).
//
// The only place allowed to write a shape's cached triangles or wire. Everything
// else edits the parametric definition and marks the cache dirty.
#pragma once

#include <cadgeom/Types.h>

#include "geom/Shape.h"

namespace cadgeom::geom {

/// Rebuilds `shape.mesh`, `shape.wire` and `shape.bounds` from `shape.def` and
/// clears the dirty flag. Fails only on a definition the validator would have
/// rejected, which the kernel does not let through — a caller reaching this with
/// bad data has a bug, not a user error.
bool Tessellate(Shape& shape, const TessParams& tess);

/// Discretises a circle straight into `out`, appending a closed chain. Used by
/// the overlay builder, which draws circles that never become shapes.
void TessellateCircle(const Plane& plane, double radius, const TessParams& tess,
                      PolylineData& out);

} // namespace cadgeom::geom
