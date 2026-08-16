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

#include <memory>
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

    /// Solid 才有：被扫掠的那个轮廓的定义，**一份自己的拷贝**。
    ///
    /// `params.extrude.profile` 里的 ShapeId 只是出处，不是引用：轮廓那个实体随
    /// 时可能被删掉，而实体拥有自己的形状（CLAUDE.md 的约定），跟着它一起进坟墓
    /// 的形状会让实体的参数化定义悬空。带一份拷贝，实体就是自足的 —— 改拉伸高度
    /// 依旧重新扫掠，与轮廓还在不在无关。
    ///
    /// shared_ptr 而不是 unique_ptr：ShapeDef 到处按值传（命令要存两份做撤销），
    /// 而轮廓定义一旦捕获就不再变，共享一份只读的正合适。
    std::shared_ptr<const ShapeDef> profile;

    /// Mesh 才有：导入进来的三角网格，**这就是它的定义本身**。
    ///
    /// 别的类型都是「参数是真相、网格是缓存」，导入的网格反过来 —— 文件里给的就是
    /// 三角形，没有可以重新生成它们的参数。所以它存在 def 里（改细分容差不会让它
    /// 重新生成），Tessellate 只是把它抄进缓存、顺带认出特征边。
    ///
    /// shared_ptr 的理由和 profile 一样：ShapeDef 按值传，而一份几万个三角形的网格
    /// 每次撤销都深拷一遍是白花钱 —— 它导入之后就不再变了。
    std::shared_ptr<const MeshData> mesh;

    ShapeType Type() const { return params.type; }
};

struct Shape {
    ShapeId id{kInvalidShape};
    ShapeDef def;

    /// Derived cache. Valid only while `dirty` is false.
    MeshData mesh;
    PolylineData wire;
    /// 实体的面 / 边 / 顶点，索引指向上面的 mesh 和 wire。曲线为空。
    Topology topology;
    /// Object-space bounds of whichever of the two is populated.
    Aabb bounds{AabbEmpty()};

    bool dirty{true};
};

} // namespace cadgeom::geom
