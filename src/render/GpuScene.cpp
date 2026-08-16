#include "render/GpuScene.h"

namespace cadgeom::render {
namespace {

void Narrow(const Vec3d& in, float out[3]) {
    out[0] = static_cast<float>(in.x);
    out[1] = static_cast<float>(in.y);
    out[2] = static_cast<float>(in.z);
}

} // namespace

CgResult GpuScene::Sync(vk::Context& ctx, vk::DeleteQueue& deleteQueue, uint64_t frame,
                        const SceneSnapshot& snapshot) {
    if (everUploaded_ && snapshot.revision == uploadedRevision_) {
        return CgResult::Ok;
    }

    // One buffer per kind for the whole scene, rebuilt in full. Sub-allocating
    // into a persistent buffer and patching only the ranges that changed is the
    // endgame (§4.3), but it only starts paying once the scene is large enough
    // that a rebuild is visible, and a wholesale rebuild cannot go subtly out of
    // step with the scene.
    std::vector<GpuVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshRange> meshRanges;
    meshRanges.reserve(snapshot.meshes.size());

    for (const geom::MeshData* mesh : snapshot.meshes) {
        MeshRange range{};
        range.firstIndex = static_cast<uint32_t>(indices.size());
        range.vertexOffset = static_cast<int32_t>(vertices.size());
        range.indexCount = mesh ? static_cast<uint32_t>(mesh->indices.size()) : 0;
        meshRanges.push_back(range);

        if (!mesh || mesh->IsEmpty()) {
            continue;
        }

        vertices.reserve(vertices.size() + mesh->vertices.size());
        for (const geom::MeshVertex& v : mesh->vertices) {
            GpuVertex gpu{};
            Narrow(v.position, gpu.position);
            Narrow(v.normal, gpu.normal);
            vertices.push_back(gpu);
        }
        indices.insert(indices.end(), mesh->indices.begin(), mesh->indices.end());
    }

    std::vector<GpuLineInstance> lines;
    std::vector<GpuPointInstance> points;
    std::vector<InstanceRange> lineRanges;
    std::vector<InstanceRange> pointRanges;
    lineRanges.reserve(snapshot.curves.size());
    pointRanges.reserve(snapshot.curves.size());

    for (const geom::PolylineData* curve : snapshot.curves) {
        InstanceRange lineRange{static_cast<uint32_t>(lines.size()), 0};
        InstanceRange pointRange{static_cast<uint32_t>(points.size()), 0};

        if (curve) {
            const uint32_t segments = curve->SegmentCount();
            lines.reserve(lines.size() + segments);
            for (uint32_t s = 0; s < segments; ++s) {
                const uint32_t ia = curve->indices[s * 2];
                const uint32_t ib = curve->indices[s * 2 + 1];

                GpuLineInstance instance{};
                Narrow(curve->positions[ia], instance.a);
                Narrow(curve->positions[ib], instance.b);
                instance.arcA = static_cast<float>(curve->arcStart[s]);
                instance.arcB = static_cast<float>(
                    curve->arcStart[s] + Distance(curve->positions[ia], curve->positions[ib]));
                lines.push_back(instance);
            }
            lineRange.instanceCount = segments;

            points.reserve(points.size() + curve->positions.size());
            for (const Vec3d& p : curve->positions) {
                GpuPointInstance instance{};
                Narrow(p, instance.position);
                points.push_back(instance);
            }
            pointRange.instanceCount = static_cast<uint32_t>(curve->positions.size());
        }

        lineRanges.push_back(lineRange);
        pointRanges.push_back(pointRange);
    }

    Retire(ctx, deleteQueue, frame);
    meshRanges_.clear();
    lineRanges_.clear();
    pointRanges_.clear();
    triangleCount_ = 0;
    segmentCount_ = 0;

    // Each upload is independent, and a failed one has to leave nothing behind:
    // the ranges are only published once every buffer they refer to exists.
    if (!indices.empty()) {
        CgResult r = UploadToDeviceBuffer(ctx, vertices.data(), vertices.size() * sizeof(GpuVertex),
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
        meshRanges_ = std::move(meshRanges);
        triangleCount_ = static_cast<uint32_t>(indices.size() / 3);
    }

    if (!lines.empty()) {
        const CgResult r =
            UploadToDeviceBuffer(ctx, lines.data(), lines.size() * sizeof(GpuLineInstance),
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, lineInstances_);
        if (CgFailed(r)) {
            return r;
        }
        lineRanges_ = std::move(lineRanges);
        segmentCount_ = static_cast<uint32_t>(lines.size());
    }

    if (!points.empty()) {
        const CgResult r =
            UploadToDeviceBuffer(ctx, points.data(), points.size() * sizeof(GpuPointInstance),
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, pointInstances_);
        if (CgFailed(r)) {
            return r;
        }
        pointRanges_ = std::move(pointRanges);
    }

    uploadedRevision_ = snapshot.revision;
    everUploaded_ = true;

    CG_DEBUG("gpu scene: %zu mesh(es) %u triangles, %zu curve(s) %u segments", snapshot.meshes.size(),
             triangleCount_, snapshot.curves.size(), segmentCount_);
    return CgResult::Ok;
}

void GpuScene::Retire(vk::Context& ctx, vk::DeleteQueue& deleteQueue, uint64_t frame) {
    const auto retire = [&](vk::Buffer& buffer) {
        if (!buffer.IsValid()) {
            return;
        }
        vk::Buffer owned = buffer;
        deleteQueue.Push(frame, [&ctx, owned]() mutable { DestroyBuffer(ctx, owned); });
        buffer = vk::Buffer{};
    };

    retire(vertices_);
    retire(indices_);
    retire(lineInstances_);
    retire(pointInstances_);
}

void GpuScene::Destroy(vk::Context& ctx) {
    DestroyBuffer(ctx, vertices_);
    DestroyBuffer(ctx, indices_);
    DestroyBuffer(ctx, lineInstances_);
    DestroyBuffer(ctx, pointInstances_);

    meshRanges_.clear();
    lineRanges_.clear();
    pointRanges_.clear();
    triangleCount_ = 0;
    segmentCount_ = 0;
    uploadedRevision_ = 0;
    everUploaded_ = false;
}

} // namespace cadgeom::render
