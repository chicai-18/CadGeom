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

void PolylineData::AddSegment(uint32_t a, uint32_t b) {
    if (a >= positions.size() || b >= positions.size()) {
        return;
    }
    indices.push_back(a);
    indices.push_back(b);
    arcStart.push_back(length);
    length += Distance(positions[a], positions[b]);
}

uint32_t Topology::FaceOfTriangle(uint32_t triangle) const {
    // 线性扫描，不是二分：一次拉伸的面数是轮廓的段数，几十个；而这个函数只在
    // 射线真的命中了某个三角形之后被调用一次。
    for (size_t i = 0; i < faces.size(); ++i) {
        const Face& face = faces[i];
        if (triangle >= face.firstTriangle && triangle < face.firstTriangle + face.triangleCount) {
            return static_cast<uint32_t>(i);
        }
    }
    return static_cast<uint32_t>(faces.size());
}

} // namespace cadgeom::geom
