// One shape as the kernel holds it (docs/architecture.md §3.1).
//
// The split is the whole point: `def` is the parametric definition and the
// single source of truth, `mesh`/`wire` are a derived cache. Changing a radius
// edits `def` and marks the cache dirty; nothing but Tessellate() ever writes
// the other two. Get that backwards once and you get ghost geometry that no
// amount of downstream fixing will straighten out.
#pragma once

#include <cadgeom/IGeometryBuilder.h>
#include <cadgeom/Types.h>

#include "geom/MeshData.h"

#include <vector>

namespace cadgeom::geom {

/// Parametric definition. A superset of the public ShapeParams: that struct is
/// a POD union frozen into every host's build (§2.2), so it cannot carry a
/// polyline's variable-length point list. The extra members here are what the
/// ABI could not express.
struct ShapeDef {
    ShapeParams params{};

    /// Polyline only.
    std::vector<Vec3d> points;
    bool closed{false};

    ShapeType Type() const { return params.type; }
};

struct Shape {
    ShapeId id{kInvalidShape};
    ShapeDef def;

    /// Derived cache. Valid only while `dirty` is false.
    MeshData mesh;
    PolylineData wire;
    /// Object-space bounds of whichever of the two is populated.
    Aabb bounds{AabbEmpty()};

    bool dirty{true};
};

} // namespace cadgeom::geom
