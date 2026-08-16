#include "geom/MeshData.h"

#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace cadgeom::geom {
namespace {

/// 焊接用的量化坐标。导入的网格里，同一个角点因为法线不同会被拆成好几个顶点，
/// 而「棱」是几何上的事：先按位置合成一个点，边的邻接关系才成立。
struct WeldKey {
    int64_t x, y, z;

    bool operator==(const WeldKey& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct WeldHash {
    size_t operator()(const WeldKey& key) const noexcept {
        // 三个 64 位数混成一个：常数是 splitmix64 那一族里常用的奇质数。
        uint64_t h = static_cast<uint64_t>(key.x) * 0x9E3779B97F4A7C15ull;
        h ^= static_cast<uint64_t>(key.y) + 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
        h ^= static_cast<uint64_t>(key.z) + 0x165667B19E3779F9ull + (h << 6) + (h >> 2);
        return static_cast<size_t>(h);
    }
};

/// 一条无向边被哪几个三角形用到，以及它们的法线。
struct EdgeRecord {
    Vec3d normalA{0.0, 0.0, 0.0};
    Vec3d normalB{0.0, 0.0, 0.0};
    uint32_t count{0};
};

/// 无向边的键：两个焊接后的下标，小的在高位，这样 (a,b) 和 (b,a) 是同一条边。
uint64_t EdgeKey(uint32_t a, uint32_t b) {
    const uint32_t lo = a < b ? a : b;
    const uint32_t hi = a < b ? b : a;
    return (static_cast<uint64_t>(lo) << 32) | hi;
}

} // namespace

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

void GenerateFlatNormals(MeshData& mesh) {
    std::vector<MeshVertex> split;
    split.reserve(mesh.indices.size());

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vec3d& a = mesh.vertices[mesh.indices[i]].position;
        const Vec3d& b = mesh.vertices[mesh.indices[i + 1]].position;
        const Vec3d& c = mesh.vertices[mesh.indices[i + 2]].position;
        // 退化三角形的法线是零向量，留着就是了：它画不出面积，遮不住任何东西。
        const Vec3d normal = Normalized(Cross(b - a, c - a));
        split.push_back(MeshVertex{a, normal});
        split.push_back(MeshVertex{b, normal});
        split.push_back(MeshVertex{c, normal});
    }

    mesh.vertices = std::move(split);
    mesh.indices.resize(mesh.vertices.size());
    std::iota(mesh.indices.begin(), mesh.indices.end(), 0u);
    mesh.RecomputeBounds();
}

void BuildFeatureEdges(const MeshData& mesh, double creaseAngle, PolylineData& wire,
                       Topology& topology) {
    const auto triangleCount = static_cast<uint32_t>(mesh.indices.size() / 3);
    if (triangleCount == 0) {
        return;
    }

    // 整块网格算一个面。三角形汤里没有「这几百个三角形是同一个平面」这种信息，
    // 硬按共面切分只会把一个零件拆成一地碎片 —— 面拾取因此报的是「这个网格」，
    // 而不是某个假面。
    Topology::Face face{};
    face.firstTriangle = 0;
    face.triangleCount = triangleCount;
    face.planar = false;
    topology.faces.push_back(face);

    // -- 1. 按位置焊接 ------------------------------------------------------
    Aabb bounds = AabbEmpty();
    for (const MeshVertex& v : mesh.vertices) {
        Expand(bounds, v.position);
    }
    // 量化步长跟着模型尺寸走：一米的零件和一毫米的零件对「同一个点」的容忍度
    // 不一样，绝对值写死会在其中一头出错。
    const double diagonal = IsEmpty(bounds) ? 1.0 : DiagonalLength(bounds);
    const double step = std::max(diagonal * 1e-7, 1e-12);

    std::unordered_map<WeldKey, uint32_t, WeldHash> welded;
    welded.reserve(mesh.vertices.size());
    std::vector<uint32_t> vertexToWelded(mesh.vertices.size(), 0);
    std::vector<Vec3d> weldedPositions;

    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3d& p = mesh.vertices[i].position;
        const WeldKey key{static_cast<int64_t>(std::llround(p.x / step)),
                          static_cast<int64_t>(std::llround(p.y / step)),
                          static_cast<int64_t>(std::llround(p.z / step))};
        const auto index = static_cast<uint32_t>(weldedPositions.size());
        const auto [it, inserted] = welded.try_emplace(key, index);
        if (inserted) {
            weldedPositions.push_back(p);
        }
        vertexToWelded[i] = it->second;
    }

    // -- 2. 每条无向边用到它的三角形 ----------------------------------------
    std::unordered_map<uint64_t, EdgeRecord> edges;
    edges.reserve(static_cast<size_t>(triangleCount) * 3);

    for (uint32_t t = 0; t < triangleCount; ++t) {
        const uint32_t i0 = vertexToWelded[mesh.indices[t * 3]];
        const uint32_t i1 = vertexToWelded[mesh.indices[t * 3 + 1]];
        const uint32_t i2 = vertexToWelded[mesh.indices[t * 3 + 2]];
        const Vec3d normal = Normalized(Cross(weldedPositions[i1] - weldedPositions[i0],
                                              weldedPositions[i2] - weldedPositions[i0]));

        const uint32_t corners[3][2] = {{i0, i1}, {i1, i2}, {i2, i0}};
        for (const auto& corner : corners) {
            if (corner[0] == corner[1]) {
                continue;  // 退化三角形塌掉的那条边，不是棱。
            }
            EdgeRecord& record = edges[EdgeKey(corner[0], corner[1])];
            if (record.count == 0) {
                record.normalA = normal;
            } else if (record.count == 1) {
                record.normalB = normal;
            }
            ++record.count;
        }
    }

    // -- 3. 哪些边算特征边 --------------------------------------------------
    const double cosThreshold = std::cos(creaseAngle);
    std::vector<std::pair<uint32_t, uint32_t>> featureEdges;
    std::unordered_map<uint32_t, std::vector<uint32_t>> adjacency;

    for (const auto& [key, record] : edges) {
        const auto a = static_cast<uint32_t>(key >> 32);
        const auto b = static_cast<uint32_t>(key & 0xFFFFFFFFull);
        // 边界边（只有一个面用到）和非流形边（三个以上）一律留着。
        bool isFeature = record.count != 2;
        if (record.count == 2) {
            isFeature = Dot(record.normalA, record.normalB) < cosThreshold;
        }
        if (!isFeature) {
            continue;
        }
        const auto index = static_cast<uint32_t>(featureEdges.size());
        featureEdges.emplace_back(a, b);
        adjacency[a].push_back(index);
        adjacency[b].push_back(index);
    }
    if (featureEdges.empty()) {
        return;  // 一个球面上没有任何棱，这是对的答案，不是失败。
    }

    // -- 4. 串成链 ----------------------------------------------------------
    std::vector<bool> used(featureEdges.size(), false);
    // 已经进过 topology.vertices 的焊接下标。用集合而不是线性查找：一个几万个角点的
    // 网格上，「这个角记过没有」要问几万次，线性查一遍就是平方级。
    std::unordered_set<uint32_t> emittedVertices;

    const auto other = [&](uint32_t edge, uint32_t from) {
        return featureEdges[edge].first == from ? featureEdges[edge].second
                                                : featureEdges[edge].first;
    };

    const auto emitChain = [&](uint32_t startVertex, uint32_t startEdge) {
        std::vector<uint32_t> chain{startVertex};
        uint32_t current = startVertex;
        uint32_t edge = startEdge;
        bool closed = false;

        for (;;) {
            used[edge] = true;
            const uint32_t next = other(edge, current);
            current = next;
            if (current == startVertex) {
                closed = true;  // 绕回起点，是个环 —— 起点不重复记一遍。
                break;
            }
            chain.push_back(current);

            // 度不为 2 的点是链的终点：那里是个角，链在此断开而不是拐过去。
            // 用 find 而不是 operator[]：外层正在遍历 adjacency，一次意外的插入
            // 会让那个迭代器失效。
            const auto incident = adjacency.find(current);
            if (incident == adjacency.end() || incident->second.size() != 2) {
                break;
            }
            uint32_t nextEdge = UINT32_MAX;
            for (const uint32_t candidate : incident->second) {
                if (!used[candidate]) {
                    nextEdge = candidate;
                    break;
                }
            }
            if (nextEdge == UINT32_MAX) {
                break;
            }
            edge = nextEdge;
        }

        const auto first = static_cast<uint32_t>(wire.positions.size());
        for (const uint32_t v : chain) {
            wire.positions.push_back(weldedPositions[v]);
        }
        const auto firstSegment = static_cast<uint32_t>(wire.indices.size() / 2);
        // 弧长接着上一条链往下累，虚线的相位因此跨链连续。
        wire.AddChain(first, static_cast<uint32_t>(chain.size()), closed, wire.length);

        Topology::Edge topoEdge{};
        topoEdge.firstSegment = firstSegment;
        topoEdge.segmentCount = static_cast<uint32_t>(wire.indices.size() / 2) - firstSegment;
        topology.edges.push_back(topoEdge);

        if (closed) {
            return;  // 环上没有角点：圆柱端面那一圈上的每个点都是细分产物。
        }
        // 开链的两头才是零件上真的顶点。同一个角会被三条棱各碰一次，只记第一次。
        const uint32_t ends[2] = {chain.front(), chain.back()};
        for (int i = 0; i < 2; ++i) {
            if (!emittedVertices.insert(ends[i]).second) {
                continue;
            }
            topology.vertices.push_back(i == 0 ? first
                                               : first + static_cast<uint32_t>(chain.size()) - 1);
        }
    };

    // 先从角点出发，链的走向因此是确定的；剩下的才是纯环。
    for (const auto& [vertex, incident] : adjacency) {
        if (incident.size() == 2) {
            continue;
        }
        for (const uint32_t edge : incident) {
            if (!used[edge]) {
                emitChain(vertex, edge);
            }
        }
    }
    for (uint32_t edge = 0; edge < featureEdges.size(); ++edge) {
        if (!used[edge]) {
            emitChain(featureEdges[edge].first, edge);
        }
    }
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
