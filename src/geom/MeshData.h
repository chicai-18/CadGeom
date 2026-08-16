// Tessellated geometry — the derived cache, never the source of truth.
//
// Parameters are the SSOT; a MeshData is what Tessellate() produces from them
// (docs/architecture.md §3.1). M1 only needs the container and one built-in
// primitive so MeshPass has something to draw; M2's kernel fills these from
// real parametric shapes.
//
// Positions are double like everything upstream of the GPU. Narrowing happens
// once, at upload, relative to the camera origin.
#pragma once

#include <cadgeom/Types.h>

#include "core/Math.h"

#include <stdint.h>

#include <vector>

namespace cadgeom::geom {

struct MeshVertex {
    Vec3d position;
    Vec3d normal;
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    /// Object-space bounds, kept in step by RecomputeBounds().
    Aabb bounds{AabbEmpty()};

    bool IsEmpty() const { return indices.empty(); }

    void Clear() {
        vertices.clear();
        indices.clear();
        bounds = AabbEmpty();
    }

    void RecomputeBounds() {
        bounds = AabbEmpty();
        for (const MeshVertex& v : vertices) {
            Expand(bounds, v.position);
        }
    }
};

/// Axis-aligned box with flat (per-face) normals, centred on `center`.
/// `size` is the full extent along each axis, not the half-extent.
MeshData MakeBox(const Vec3d& center, const Vec3d& size);

} // namespace cadgeom::geom
