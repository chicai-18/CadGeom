#include "geom/MeshData.h"

namespace cadgeom::geom {

void PolylineData::AddChain(uint32_t first, uint32_t count, bool closed, double startLength) {
    if (count < 2) {
        // A single point is a legitimate chain with no segments — a Point shape
        // is exactly that — so this is not an error, just nothing to connect.
        if (count == 1) {
            length = startLength;
        }
        return;
    }

    const uint32_t segments = closed ? count : count - 1;
    indices.reserve(indices.size() + segments * 2u);
    arcStart.reserve(arcStart.size() + segments);

    double accumulated = startLength;
    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t a = first + i;
        const uint32_t b = first + ((i + 1) % count);
        indices.push_back(a);
        indices.push_back(b);
        arcStart.push_back(accumulated);
        accumulated += Distance(positions[a], positions[b]);
    }
    length = accumulated;
}

MeshData MakeBox(const Vec3d& center, const Vec3d& size) {
    const Vec3d h{0.5 * size.x, 0.5 * size.y, 0.5 * size.z};

    // Six independent quads rather than eight shared corners: a box has hard
    // edges, and a shared vertex can only carry one normal. Sharp-versus-smooth
    // is a property of the face, which is exactly the distinction the extrude
    // path will have to make in M4.
    struct Face {
        Vec3d normal;
        Vec3d u;  ///< First in-plane axis, scaled below by the half extents.
        Vec3d v;
    };
    static const Face faces[6] = {
        {{+1, 0, 0}, {0, +1, 0}, {0, 0, +1}},  // +X
        {{-1, 0, 0}, {0, 0, +1}, {0, +1, 0}},  // -X
        {{0, +1, 0}, {0, 0, +1}, {+1, 0, 0}},  // +Y
        {{0, -1, 0}, {+1, 0, 0}, {0, 0, +1}},  // -Y
        {{0, 0, +1}, {+1, 0, 0}, {0, +1, 0}},  // +Z
        {{0, 0, -1}, {0, +1, 0}, {+1, 0, 0}},  // -Z
    };

    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    for (const Face& f : faces) {
        const Vec3d n{f.normal.x * h.x, f.normal.y * h.y, f.normal.z * h.z};
        const Vec3d u{f.u.x * h.x, f.u.y * h.y, f.u.z * h.z};
        const Vec3d v{f.v.x * h.x, f.v.y * h.y, f.v.z * h.z};

        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        // u x v is +normal for every row above, so this winds counter-clockwise
        // seen from outside — front-facing under the renderer's CCW convention.
        mesh.vertices.push_back({center + n - u - v, f.normal});
        mesh.vertices.push_back({center + n + u - v, f.normal});
        mesh.vertices.push_back({center + n + u + v, f.normal});
        mesh.vertices.push_back({center + n - u + v, f.normal});

        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }

    mesh.RecomputeBounds();
    return mesh;
}

} // namespace cadgeom::geom
