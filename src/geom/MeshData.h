// Tessellated geometry — the derived cache, never the source of truth.
//
// Parameters are the SSOT; a MeshData or PolylineData is what Tessellate()
// produces from them (docs/architecture.md §3.1). Solids fill the mesh, curves
// fill the wire, and a point fills neither — it is one position and nothing
// else.
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

/// Tessellated wireframe: the discretised form of a curve, or the feature edges
/// of a solid.
///
/// Segments are an explicit index pair list rather than a strip because the two
/// consumers disagree about connectivity — a circle is one closed chain, a
/// solid's edge set is many disjoint ones — and a pair list expresses both
/// without a restart convention.
struct PolylineData {
    std::vector<Vec3d> positions;
    /// Two indices into `positions` per segment.
    std::vector<uint32_t> indices;
    /// One per segment: arc length along the curve at the segment's first
    /// endpoint. Dash patterns are phased off this, which is what makes a
    /// tessellated circle dash as one continuous curve instead of restarting the
    /// pattern at every chord.
    std::vector<double> arcStart;
    /// Total length, so a caller can size a dash pattern without walking it.
    double length{0.0};
    Aabb bounds{AabbEmpty()};

    uint32_t SegmentCount() const { return static_cast<uint32_t>(indices.size() / 2); }
    bool IsEmpty() const { return indices.empty(); }

    void Clear() {
        positions.clear();
        indices.clear();
        arcStart.clear();
        length = 0.0;
        bounds = AabbEmpty();
    }

    void RecomputeBounds() {
        bounds = AabbEmpty();
        for (const Vec3d& p : positions) {
            Expand(bounds, p);
        }
    }

    /// Appends an open or closed chain over the points already in `positions`
    /// starting at `first`, accumulating arc length as it goes. `startLength`
    /// seeds the accumulator so several chains can share one dash phase.
    void AddChain(uint32_t first, uint32_t count, bool closed, double startLength = 0.0);
};

/// Axis-aligned box with flat (per-face) normals, centred on `center`.
/// `size` is the full extent along each axis, not the half-extent.
MeshData MakeBox(const Vec3d& center, const Vec3d& size);

} // namespace cadgeom::geom
