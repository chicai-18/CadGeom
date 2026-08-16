/**
 * @file Bvh.h
 * @brief 场景的空间索引：拾取的粗筛，以及往后的视锥剔除
 *        （docs/architecture.md §5）。
 *
 * 索引的对象只是「一个 EntityId 加它的世界包围盒」，不是实体本身 —— 这一层不
 * 认识实体表，也不认识内核。谁被命中之后要拿什么几何，是上面那层的事。
 *
 * 结构本身很朴素：质心中位分割，叶子最多 4 个对象。SAH 在这个规模上省不出可
 * 观测的时间，而中位分割构建是线性的，脏了整棵重建也不心疼。
 */
#pragma once

#include <cadgeom/Types.h>

#include "core/Math.h"
#include "geom/Intersect.h"

#include <vector>

namespace cadgeom::scene {

/// @brief 被索引的一个对象。
struct BvhItem {
    EntityId entity{kInvalidEntity};
    /// 世界空间包围盒。点图元的盒子三个方向都是零厚度，遍历要能容忍这一点。
    Aabb bounds{AabbEmpty()};
};

class Bvh {
public:
    /// @brief 整棵重建。`items` 会被就地重排，所以按值接走。
    void Build(std::vector<BvhItem> items);

    void Clear();

    bool IsEmpty() const { return items_.empty(); }
    uint32_t ItemCount() const { return static_cast<uint32_t>(items_.size()); }

    /// @brief 根包围盒；空索引时是 AabbEmpty()。
    const Aabb& Bounds() const { return bounds_; }

    /// @brief 对每个包围盒（外扩 `padding` 后）与射线相交的对象调用 `visit`。
    /// @param padding 外扩量，世界单位。拾取的容差是像素级的，一个刚好擦过曲线
    ///                的射线并不与它的包围盒相交，不外扩就永远选不中细线。
    /// @note 不保证由近及远 —— 优先级判定要看的是子元素类型而不只是深度，排序
    ///       在这里省不掉窄阶段的任何一次计算。
    template <typename Visit>
    void Raycast(const Ray& ray, double padding, Visit&& visit) const;

    /// @brief 对每个包围盒与给定球相交的对象调用 `visit`。吸附用它找光标附近的
    ///        几何。
    template <typename Visit>
    void QuerySphere(const Vec3d& center, double radius, Visit&& visit) const;

private:
    /// count > 0 是叶节点；否则左子恰好是 index + 1，右子是 `right`。
    struct Node {
        Aabb bounds{AabbEmpty()};
        uint32_t first{0};
        uint32_t count{0};
        uint32_t right{0};
    };

    /// 构建深度上限。到顶就强制成叶子，遍历栈因此有一个静态的容量上界。
    static constexpr uint32_t kMaxDepth = 32;
    static constexpr uint32_t kLeafSize = 4;

    uint32_t BuildRange(uint32_t first, uint32_t count, uint32_t depth);

    std::vector<BvhItem> items_;
    std::vector<Node> nodes_;
    Aabb bounds_{AabbEmpty()};
};

// ---------------------------------------------------------------------------
// 遍历
//
// 模板，所以留在头里：访问器都是调用点的 lambda，走函数指针的话每个对象都要付
// 一次间接调用。
// ---------------------------------------------------------------------------

template <typename Visit>
void Bvh::Raycast(const Ray& ray, double padding, Visit&& visit) const {
    if (nodes_.empty()) {
        return;
    }

    uint32_t stack[kMaxDepth + 2];
    uint32_t depth = 0;
    stack[depth++] = 0;

    while (depth > 0) {
        const Node& node = nodes_[stack[--depth]];
        double tNear = 0.0;
        double tFar = 0.0;
        if (!geom::RayAabb(ray, Padded(node.bounds, padding), tNear, tFar)) {
            continue;
        }
        if (node.count > 0) {
            for (uint32_t i = 0; i < node.count; ++i) {
                visit(items_[node.first + i]);
            }
        } else if (depth + 2 <= kMaxDepth + 2) {
            stack[depth++] = node.right;
            stack[depth++] = static_cast<uint32_t>(&node - nodes_.data()) + 1;
        }
    }
}

template <typename Visit>
void Bvh::QuerySphere(const Vec3d& center, double radius, Visit&& visit) const {
    if (nodes_.empty() || !(radius >= 0.0)) {
        return;
    }

    uint32_t stack[kMaxDepth + 2];
    uint32_t depth = 0;
    stack[depth++] = 0;

    while (depth > 0) {
        const Node& node = nodes_[stack[--depth]];
        if (Distance(ClosestPointIn(node.bounds, center), center) > radius) {
            continue;
        }
        if (node.count > 0) {
            for (uint32_t i = 0; i < node.count; ++i) {
                visit(items_[node.first + i]);
            }
        } else if (depth + 2 <= kMaxDepth + 2) {
            stack[depth++] = node.right;
            stack[depth++] = static_cast<uint32_t>(&node - nodes_.data()) + 1;
        }
    }
}

} // namespace cadgeom::scene
