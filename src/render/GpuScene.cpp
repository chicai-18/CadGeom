#include "render/GpuScene.h"

namespace cadgeom::render {

CgResult GpuScene::Sync(vk::Context& ctx, vk::DeleteQueue& deleteQueue, uint64_t frame,
                        const SceneSnapshot& snapshot) {
    if (everUploaded_ && snapshot.revision == uploadedRevision_) {
        return CgResult::Ok;
    }

    // One vertex buffer and one index buffer for the whole scene, rebuilt in
    // full. Sub-allocating into a persistent buffer and patching only the
    // ranges that changed is the endgame (§4.3), but it only starts paying once
    // the scene is large enough that a rebuild is visible, and a wholesale
    // rebuild cannot go subtly out of step with the scene.
    std::vector<GpuVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshRange> ranges;
    ranges.reserve(snapshot.meshes.size());

    for (const geom::MeshData* mesh : snapshot.meshes) {
        MeshRange range{};
        range.firstIndex = static_cast<uint32_t>(indices.size());
        range.vertexOffset = static_cast<int32_t>(vertices.size());
        range.indexCount = mesh ? static_cast<uint32_t>(mesh->indices.size()) : 0;
        ranges.push_back(range);

        if (!mesh || mesh->IsEmpty()) {
            continue;
        }

        vertices.reserve(vertices.size() + mesh->vertices.size());
        for (const geom::MeshVertex& v : mesh->vertices) {
            GpuVertex gpu{};
            gpu.position[0] = static_cast<float>(v.position.x);
            gpu.position[1] = static_cast<float>(v.position.y);
            gpu.position[2] = static_cast<float>(v.position.z);
            gpu.normal[0] = static_cast<float>(v.normal.x);
            gpu.normal[1] = static_cast<float>(v.normal.y);
            gpu.normal[2] = static_cast<float>(v.normal.z);
            vertices.push_back(gpu);
        }
        indices.insert(indices.end(), mesh->indices.begin(), mesh->indices.end());
    }

    Retire(ctx, deleteQueue, frame);
    ranges_.clear();
    triangleCount_ = 0;

    if (indices.empty()) {
        uploadedRevision_ = snapshot.revision;
        everUploaded_ = true;
        return CgResult::Ok;
    }

    CgResult r = UploadToDeviceBuffer(ctx, vertices.data(),
                                      vertices.size() * sizeof(GpuVertex),
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices_);
    if (CgFailed(r)) {
        return r;
    }
    r = UploadToDeviceBuffer(ctx, indices.data(), indices.size() * sizeof(uint32_t),
                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices_);
    if (CgFailed(r)) {
        DestroyBuffer(ctx, vertices_);
        return r;
    }

    ranges_ = std::move(ranges);
    triangleCount_ = static_cast<uint32_t>(indices.size() / 3);
    uploadedRevision_ = snapshot.revision;
    everUploaded_ = true;

    CG_DEBUG("gpu scene: %zu meshes, %u triangles uploaded", snapshot.meshes.size(),
             triangleCount_);
    return CgResult::Ok;
}

void GpuScene::Retire(vk::Context& ctx, vk::DeleteQueue& deleteQueue, uint64_t frame) {
    if (vertices_.IsValid()) {
        vk::Buffer buffer = vertices_;
        deleteQueue.Push(frame, [&ctx, buffer]() mutable { DestroyBuffer(ctx, buffer); });
        vertices_ = vk::Buffer{};
    }
    if (indices_.IsValid()) {
        vk::Buffer buffer = indices_;
        deleteQueue.Push(frame, [&ctx, buffer]() mutable { DestroyBuffer(ctx, buffer); });
        indices_ = vk::Buffer{};
    }
}

void GpuScene::Destroy(vk::Context& ctx) {
    DestroyBuffer(ctx, vertices_);
    DestroyBuffer(ctx, indices_);
    ranges_.clear();
    triangleCount_ = 0;
    uploadedRevision_ = 0;
    everUploaded_ = false;
}

} // namespace cadgeom::render
