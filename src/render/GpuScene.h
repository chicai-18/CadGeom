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

/// One draw. `meshIndex` indexes SceneSnapshot::meshes.
struct DrawItem {
    Mat4d worldTransform{Mat4Identity()};
    Color color{0.75f, 0.76f, 0.78f, 1.0f};
    uint32_t meshIndex{0};
};

struct SceneSnapshot {
    /// Bumped by the scene whenever anything the renderer cares about changed.
    /// Equal revisions mean the vertex data on the GPU is still current.
    uint64_t revision{0};
    /// Non-owning; the api layer keeps the meshes alive across the call.
    std::vector<const geom::MeshData*> meshes;
    std::vector<DrawItem> items;
};

/// What one vertex looks like once it reaches the GPU. Float, and in object
/// space — the camera-relative offset rides on the per-draw model matrix, so
/// the vertex data itself never has to be re-uploaded when the camera moves.
struct GpuVertex {
    float position[3];
    float normal[3];
};

class GpuScene {
public:
    struct MeshRange {
        uint32_t firstIndex;
        uint32_t indexCount;
        int32_t vertexOffset;
    };

    /// Re-uploads when `snapshot.revision` has moved on, otherwise does
    /// nothing. Old buffers go through the delete queue because the GPU may
    /// still be reading them for a frame or two.
    CgResult Sync(vk::Context& ctx, vk::DeleteQueue& deleteQueue, uint64_t frame,
                  const SceneSnapshot& snapshot);

    void Destroy(vk::Context& ctx);

    bool HasGeometry() const { return !ranges_.empty() && vertices_.IsValid(); }

    VkBuffer VertexBuffer() const { return vertices_.handle; }
    VkBuffer IndexBuffer() const { return indices_.handle; }

    /// Null when `meshIndex` is out of range or that mesh was empty.
    const MeshRange* RangeFor(uint32_t meshIndex) const {
        return meshIndex < ranges_.size() && ranges_[meshIndex].indexCount > 0
                   ? &ranges_[meshIndex]
                   : nullptr;
    }

    uint32_t TriangleCount() const { return triangleCount_; }

private:
    void Retire(vk::Context& ctx, vk::DeleteQueue& deleteQueue, uint64_t frame);

    vk::Buffer vertices_;
    vk::Buffer indices_;
    std::vector<MeshRange> ranges_;

    uint64_t uploadedRevision_{0};
    bool everUploaded_{false};
    uint32_t triangleCount_{0};
};

} // namespace cadgeom::render
