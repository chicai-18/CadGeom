#include "scene/Bvh.h"

#include <algorithm>

namespace cadgeom::scene {
namespace {

double AxisOf(const Vec3d& v, int axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

} // namespace

void Bvh::Clear() {
    items_.clear();
    nodes_.clear();
    bounds_ = AabbEmpty();
}

void Bvh::Build(std::vector<BvhItem> items) {
    items_ = std::move(items);
    nodes_.clear();
    bounds_ = AabbEmpty();
    if (items_.empty()) {
        return;
    }

    // 一个对象一个节点是最坏情况；先要够，构建过程中就不会有 reallocate 把
    // 递归里持有的引用弄失效。
    nodes_.reserve(items_.size() * 2);
    BuildRange(0, static_cast<uint32_t>(items_.size()), 0);
    bounds_ = nodes_[0].bounds;
}

uint32_t Bvh::BuildRange(uint32_t first, uint32_t count, uint32_t depth) {
    const auto index = static_cast<uint32_t>(nodes_.size());
    nodes_.push_back(Node{});

    Aabb bounds = AabbEmpty();
    Aabb centroids = AabbEmpty();
    for (uint32_t i = 0; i < count; ++i) {
        Expand(bounds, items_[first + i].bounds);
        Expand(centroids, Center(items_[first + i].bounds));
    }
    nodes_[index].bounds = bounds;

    if (count <= kLeafSize || depth >= kMaxDepth) {
        nodes_[index].first = first;
        nodes_[index].count = count;
        return index;
    }

    // 沿质心分布最长的轴切。质心全部重合时这个轴的选择没有意义，但下面的中位
    // 分割仍然把对象对半分开，树不会退化成一条链。
    const Vec3d extent = Extents(centroids);
    const int axis = extent.x > extent.y ? (extent.x > extent.z ? 0 : 2)
                                         : (extent.y > extent.z ? 1 : 2);

    const uint32_t mid = count / 2;
    const auto begin = items_.begin() + first;
    std::nth_element(begin, begin + mid, begin + count,
                     [axis](const BvhItem& a, const BvhItem& b) {
                         return AxisOf(Center(a.bounds), axis) < AxisOf(Center(b.bounds), axis);
                     });

    // 左子紧跟在自己后面，所以只需要记右子的下标。先建左，顺序才成立。
    BuildRange(first, mid, depth + 1);
    const uint32_t right = BuildRange(first + mid, count - mid, depth + 1);
    nodes_[index].right = right;
    nodes_[index].count = 0;
    return index;
}

} // namespace cadgeom::scene
