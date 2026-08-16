// Scene geometry as the GPU sees it (docs/architecture.md §4.3).
//
// The renderer is fed a snapshot rather than reaching into the scene graph:
// render/ sits below scene/ and api/ in the layering and must not know either
// exists. Building the snapshot is the api layer's job, and it is also where
// the double-to-float narrowing stays honest, because the snapshot still
// carries doubles.
#pragma once

#include "geom/MeshData.h"
#include "render/vk/Context.h"
#include "render/vk/DeleteQueue.h"
#include "render/vk/Resources.h"

#include <vector>

namespace cadgeom::render {

/// One shaded draw. `meshIndex` indexes SceneSnapshot::meshes.
struct DrawItem {
    Mat4d worldTransform{Mat4Identity()};
    Color color{0.75f, 0.76f, 0.78f, 1.0f};
    uint32_t meshIndex{0};
};

/// One wireframe draw: every segment of SceneSnapshot::curves[curveIndex], in
/// one instanced call.
struct CurveItem {
    Mat4d worldTransform{Mat4Identity()};
    Color color{0.75f, 0.76f, 0.78f, 1.0f};
    float width{1.5f};  ///< Screen-space pixels.
    LineStyle style{LineStyle::Solid};
    uint32_t curveIndex{0};
};

/// One point-primitive draw: every position of SceneSnapshot::curves[index].
struct PointItem {
    Mat4d worldTransform{Mat4Identity()};
    Color color{0.75f, 0.76f, 0.78f, 1.0f};
    float size{6.0f};  ///< Screen-space pixels.
    uint32_t curveIndex{0};
};

struct SceneSnapshot {
    /// Bumped by the scene whenever anything the renderer cares about changed,
    /// including a change of colour or highlight. The api layer rebuilds this
    /// snapshot when it moves.
    uint64_t revision{0};

    /// @brief 只在顶点真的变了时前进 —— 增删实体、改参数、动变换、改可见性。
    ///
    /// 和 `revision` 分开是因为悬停高亮跟着鼠标每一次移动变：它要重建 draw
    /// item（颜色在里面），但缓冲区里的顶点一个都没动。GpuScene 跟这一个，所以
    /// 鼠标划过几何不会引发一次全量重传。
    uint64_t geometryRevision{0};

    /// Non-owning; the api layer keeps the geometry alive across the call.
    std::vector<const geom::MeshData*> meshes;
    /// Wire geometry, shared by both curve and point drawing: a curve's segments
    /// become line instances and its vertices become point instances, so a Point
    /// shape is just a wire with one vertex and no segments.
    std::vector<const geom::PolylineData*> curves;

    std::vector<DrawItem> items;
    std::vector<CurveItem> curveItems;
    /// 实体的特征边。和 `curveItems` 同样是 CurveItem、同样指向 `curves`，分开一份
    /// 是因为它们的画法不同：走 EdgePass，用 EntityStyle::edgeColor，不写深度，而且
    /// RenderMode::Shaded 下整批不画。
    std::vector<CurveItem> edgeItems;
    std::vector<PointItem> pointItems;

    void Clear() {
        meshes.clear();
        curves.clear();
        items.clear();
        curveItems.clear();
        edgeItems.clear();
        pointItems.clear();
    }
};

/// What one shaded vertex looks like once it reaches the GPU. Float, and in
/// object space — the camera-relative offset rides on the per-draw model matrix,
/// so the vertex data itself never has to be re-uploaded when the camera moves.
struct GpuVertex {
    float position[3];
    float normal[3];
};

/// One line segment, one instance. The arc lengths are what let the fragment
/// shader phase a dash pattern along a whole curve rather than restarting it at
/// every chord.
struct GpuLineInstance {
    float a[3];
    float arcA;
    float b[3];
    float arcB;
};

/// One point primitive, one instance.
struct GpuPointInstance {
    float position[3];
};

class GpuScene {
public:
    struct MeshRange {
        uint32_t firstIndex;
        uint32_t indexCount;
        int32_t vertexOffset;
    };

    struct InstanceRange {
        uint32_t firstInstance;
        uint32_t instanceCount;
    };

    /// Re-uploads when `snapshot.revision` has moved on, otherwise does
    /// nothing. Old buffers go through the delete queue because the GPU may
    /// still be reading them for a frame or two.
    CgResult Sync(vk::Context& ctx, vk::DeleteQueue& deleteQueue, uint64_t frame,
                  const SceneSnapshot& snapshot);

    void Destroy(vk::Context& ctx);

    bool HasGeometry() const { return !meshRanges_.empty() && vertices_.IsValid(); }
    bool HasLines() const { return lineInstances_.IsValid(); }
    bool HasPoints() const { return pointInstances_.IsValid(); }

    VkBuffer VertexBuffer() const { return vertices_.handle; }
    VkBuffer IndexBuffer() const { return indices_.handle; }
    VkBuffer LineInstanceBuffer() const { return lineInstances_.handle; }
    VkBuffer PointInstanceBuffer() const { return pointInstances_.handle; }

    /// Null when the index is out of range or that geometry was empty.
    const MeshRange* RangeFor(uint32_t meshIndex) const {
        return meshIndex < meshRanges_.size() && meshRanges_[meshIndex].indexCount > 0
                   ? &meshRanges_[meshIndex]
                   : nullptr;
    }
    const InstanceRange* LineRangeFor(uint32_t curveIndex) const {
        return curveIndex < lineRanges_.size() && lineRanges_[curveIndex].instanceCount > 0
                   ? &lineRanges_[curveIndex]
                   : nullptr;
    }
    const InstanceRange* PointRangeFor(uint32_t curveIndex) const {
        return curveIndex < pointRanges_.size() && pointRanges_[curveIndex].instanceCount > 0
                   ? &pointRanges_[curveIndex]
                   : nullptr;
    }

    uint32_t TriangleCount() const { return triangleCount_; }
    uint32_t SegmentCount() const { return segmentCount_; }

private:
    void Retire(vk::Context& ctx, vk::DeleteQueue& deleteQueue, uint64_t frame);

    vk::Buffer vertices_;
    vk::Buffer indices_;
    vk::Buffer lineInstances_;
    vk::Buffer pointInstances_;

    std::vector<MeshRange> meshRanges_;
    std::vector<InstanceRange> lineRanges_;
    std::vector<InstanceRange> pointRanges_;

    uint64_t uploadedRevision_{0};
    bool everUploaded_{false};
    uint32_t triangleCount_{0};
    uint32_t segmentCount_{0};
};

} // namespace cadgeom::render
