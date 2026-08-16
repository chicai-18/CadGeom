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

    /// @brief 追加一条由任意两个已有顶点构成的线段，弧长接着 `length` 往下累。
    /// @note 实体的竖直棱不属于任何一条连续链 —— 它连的是底环和顶环上隔着 n 个
    ///       下标的两个点，AddChain 的连续区间表达不了。
    void AddSegment(uint32_t a, uint32_t b);
};

/// @brief 实体的拓扑：面 / 边 / 顶点（docs/architecture.md §3.3）。
///
/// 拓扑不是可选项。拾取要能选中「某个面」，Gizmo 要能拖「某条边」，后期的布尔
/// 运算和 STEP 导出全依赖它，而这些都是补不回来的东西 —— 三角形汤里找不回一个
/// 圆柱侧面是一个面这件事。
///
/// 三个列表都是**索引**，指向同一个形状里的 MeshData 和 PolylineData，自己不存
/// 任何几何：网格重新细分时它跟着一起重建，两边因此不可能对不上。
struct Topology {
    /// @brief 一个面 —— 网格里一段连续的三角形。
    struct Face {
        /// 索引到 MeshData::indices，以三角形计（即 indices 下标 / 3）。
        uint32_t firstTriangle{0};
        uint32_t triangleCount{0};
        /// 面法线，仅当 `planar` 为 true 时有意义；否则是零向量。
        Vec3d normal{0.0, 0.0, 0.0};
        /// 平面面为 true。圆柱侧面是一个光滑的非平面面，法线逐顶点插值。
        bool planar{true};
    };

    /// @brief 一条边 —— 线框里一段连续的线段。
    struct Edge {
        /// 索引到 PolylineData::indices，以线段计。
        uint32_t firstSegment{0};
        uint32_t segmentCount{0};
    };

    std::vector<Face> faces;
    std::vector<Edge> edges;
    /// 索引到 PolylineData::positions。只有真正的角点在这儿 —— 圆柱底环上的点
    /// 是细分产物，不是顶点，把它们算进来只会让端点吸附在圆周上噪成一片。
    std::vector<uint32_t> vertices;

    bool IsEmpty() const { return faces.empty(); }

    void Clear() {
        faces.clear();
        edges.clear();
        vertices.clear();
    }

    /// @brief 某个三角形属于哪个面 —— 拾取把三角形命中翻译成 faceIndex 用的。
    /// @return 面下标；没有任何面包含它时返回 `faces.size()`。
    uint32_t FaceOfTriangle(uint32_t triangle) const;
};

// ---------------------------------------------------------------------------
// 三角形汤上的操作 —— 导入的网格没有参数化定义，能问的只有三角形本身。
// ---------------------------------------------------------------------------

/// @brief 逐三角形算平面法线，顺带把每个三角形的顶点拆开。
/// @param mesh 就地改写：顶点数变成 `indices.size()`，下标变成 0,1,2,...
/// @note 只在文件本身没带法线时调用。硬边模型（CAD 导出的绝大多数网格）这么做
///       是对的；光滑曲面会因此显出小平面 —— 但那是文件缺信息，不是这里猜错。
void GenerateFlatNormals(MeshData& mesh);

/// @brief 从三角形里提取特征边：折角超过阈值的棱，加上只被一个三角形用到的边界边。
/// @param mesh        三角网格。
/// @param creaseAngle 折角阈值（弧度）。相邻两个面的法线夹角大于它才算一条棱。
/// @param wire        追加：特征边的点与线段。每条链的点是**新追加**的一段连续区间。
/// @param topology    追加：一个覆盖整块网格的面，以及每条链一条 `Edge`。
/// @note 这是「导入的立方体看起来还是立方体」的全部理由 —— CAD 的可读性靠黑边，
///       而三角形汤里没有边这个概念，只能重新认出来。
/// @note 特征边先按连通性串成链再入拓扑，不是一段一条：立方体因此是 12 条单段棱
///       （中点吸附有意义），圆柱的两个端面圈则各是一条几十段的闭链（中点吸附不
///       会在圆周上噪成一片）。同理，只有度不为 2 的端点才进 `topology.vertices`
///       —— 立方体给出 8 个角，圆柱一个都不给。
void BuildFeatureEdges(const MeshData& mesh, double creaseAngle, PolylineData& wire,
                       Topology& topology);

} // namespace cadgeom::geom
