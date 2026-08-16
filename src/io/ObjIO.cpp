/**
 * @file ObjIO.cpp
 * @brief Wavefront OBJ 读写（docs/architecture.md §7）。
 *
 * OBJ 是一种网格格式：没有层级、没有变换、没有参数化定义可言。所以这里的往返是
 * **有损**的 —— 一个圆导出去是一条折线，导回来还是一条折线，不再是圆。要保住参数化
 * 就用 glTF，它的 `extras` 带得动那些东西（GltfIO.cpp）。
 *
 * 读用 tinyobjloader，写是自己写的：格式简单到不值得为它引第二个依赖，而自己写才
 * 能决定往里放什么 —— 曲线走 `l`、点图元走 `p`，这两样绝大多数导出器根本不写。
 *
 * 坐标：写出去和读进来都在 IO 层做 Z-up ↔ Y-up 换算（io/Axis.h）。OBJ 没有规范规定
 * 朝向，但 Blender 和一整代 DCC 的默认设置都是 Y-up，跟着它们走，导出的东西打开
 * 就是站着的。
 */
#include "io/Registry.h"

#include "core/Error.h"
#include "core/File.h"
#include "core/Log.h"
#include "core/Math.h"
#include "core/ObjectTracker.h"
#include "io/Axis.h"
#include "io/ObjCommon.h"
#include "io/SceneAccess.h"

#include <stdio.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace cadgeom::io {
namespace {

// ---------------------------------------------------------------------------
// 写出
// ---------------------------------------------------------------------------

/// 一行数字。OBJ 是文本格式，精度全靠这里：%.10g 在十进制上留出十位有效数字，
/// 足够毫米级零件到微米，又不会让文件里塞满 17 位的浮点噪声。
void AppendVector(std::string& out, const char* tag, const Vec3d& v) {
    char line[128];
    snprintf(line, sizeof(line), "%s %.10g %.10g %.10g\n", tag, v.x, v.y, v.z);
    out.append(line);
}

/// `o` 的取值是这一行剩下的全部，空格照留 —— 「Solid 1」这样的名字才回得来。
/// 换行和制表符不行：它们会把一行拆成两行，读回来就是另一个文件了。
std::string SanitizeName(const char* name) {
    std::string clean(name && *name ? name : "Object");
    for (char& c : clean) {
        if (c == '\t' || c == '\r' || c == '\n') {
            c = '_';
        }
    }
    return clean;
}

/// 法线要走逆转置，否则非均匀缩放会把它拧歪。
Vec3d WorldNormal(const Mat4d& world, const Vec3d& local) {
    return Normalized(TransformDirection(Transpose(Inverse(world)), local));
}

class ObjExporter final : public IExporter {
public:
    explicit ObjExporter(ISceneSource& source) : source_(source) {}

    void Release() override { delete this; }

    const char* GetName() const override { return "Wavefront OBJ"; }

    CgResult Export(const char* utf8Path, const IScene* scene, const ExportOptions& options,
                    IoProgressCallback progress, void* userData) override;

private:
    ~ObjExporter() override = default;

    core::ObjectTracker tracker_;
    ISceneSource& source_;
};

CgResult ObjExporter::Export(const char* utf8Path, const IScene* /*scene*/,
                             const ExportOptions& options, IoProgressCallback progress,
                             void* userData) {
    const std::string mtlName = core::StemOf(utf8Path) + ".mtl";

    std::string obj;
    std::string mtl;
    obj.append("# Wavefront OBJ written by CadGeom\n");
    obj.append("# Y-up, right-handed (the scene itself is Z-up; the swap happens here)\n");
    obj.append("mtllib ").append(mtlName).append("\n");
    mtl.append("# CadGeom materials. Kd is linear light, matching EntityStyle::color.\n");

    // 下标是全文件累计的，而且从 1 开始 —— OBJ 的老规矩。
    uint32_t vertexBase = 1;
    uint32_t normalBase = 1;
    uint32_t written = 0;
    uint32_t materialIndex = 0;

    const uint32_t count = source_.GetEntityCount();
    for (uint32_t i = 0; i < count; ++i) {
        SceneEntityView view{};
        if (!source_.GetEntity(i, view) || !view.shape) {
            continue;
        }
        if (options.selectionOnly && !view.selected) {
            continue;
        }
        if (progress && !progress(count > 0 ? static_cast<float>(i) / static_cast<float>(count) : 1.0f,
                                  view.name, userData)) {
            return core::SetError(CgResult::Unknown, "Export: cancelled by the host");
        }

        const geom::MeshData& mesh = view.shape->mesh;
        const geom::PolylineData& wire = view.shape->wire;
        if (mesh.IsEmpty() && wire.positions.empty()) {
            continue;
        }

        obj.append("o ").append(SanitizeName(view.name)).append("\n");

        char material[64];
        snprintf(material, sizeof(material), "cadgeom_%u", materialIndex++);
        char kd[192];
        snprintf(kd, sizeof(kd), "newmtl %s\nKd %.6f %.6f %.6f\nd %.6f\nillum 2\n", material,
                 static_cast<double>(view.style.color.r), static_cast<double>(view.style.color.g),
                 static_cast<double>(view.style.color.b), static_cast<double>(view.style.color.a));
        mtl.append(kd);
        obj.append("usemtl ").append(material).append("\n");

        if (!mesh.IsEmpty()) {
            for (const geom::MeshVertex& v : mesh.vertices) {
                AppendVector(obj, "v", ToYUp(TransformPoint(view.world, v.position)));
            }
            for (const geom::MeshVertex& v : mesh.vertices) {
                AppendVector(obj, "vn", ToYUp(WorldNormal(view.world, v.normal)));
            }
            for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                char face[128];
                snprintf(face, sizeof(face), "f %u//%u %u//%u %u//%u\n",
                         vertexBase + mesh.indices[t], normalBase + mesh.indices[t],
                         vertexBase + mesh.indices[t + 1], normalBase + mesh.indices[t + 1],
                         vertexBase + mesh.indices[t + 2], normalBase + mesh.indices[t + 2]);
                obj.append(face);
            }
            vertexBase += static_cast<uint32_t>(mesh.vertices.size());
            normalBase += static_cast<uint32_t>(mesh.vertices.size());
            ++written;
            continue;
        }

        // 曲线和点图元。绝大多数导出器到这儿就把它们丢了；OBJ 其实有 `l` 和 `p`，
        // 一张只有中心线和孔位的图纸导出去之后不该是一个空文件。
        for (const Vec3d& p : wire.positions) {
            AppendVector(obj, "v", ToYUp(TransformPoint(view.world, p)));
        }
        const uint32_t segments = wire.SegmentCount();
        if (segments > 0) {
            // 先把首尾相接的线段串成链，一条链写一行 `l`。一段一行也是合法的 OBJ，
            // 但那样一个矩形出去是四行互不相干的 `l`，读回来就成了四条断开的折线
            // —— 而它本来是一个闭环，闭不闭合决定了它还能不能当拉伸的轮廓。
            uint32_t s = 0;
            while (s < segments) {
                std::string line = "l";
                const auto append = [&](uint32_t index) {
                    char number[16];
                    snprintf(number, sizeof(number), " %u", vertexBase + index);
                    line.append(number);
                };
                append(wire.indices[s * 2]);
                append(wire.indices[s * 2 + 1]);

                uint32_t last = wire.indices[s * 2 + 1];
                uint32_t next = s + 1;
                while (next < segments && wire.indices[next * 2] == last) {
                    last = wire.indices[next * 2 + 1];
                    append(last);
                    ++next;
                }
                // 闭链的最后一段绕回起点，写出来就是首点重复一次 —— 读的人靠这个
                // 认出它是闭的。
                obj.append(line).append("\n");
                s = next;
            }
        } else if (!wire.positions.empty()) {
            for (size_t p = 0; p < wire.positions.size(); ++p) {
                char point[32];
                snprintf(point, sizeof(point), "p %u\n", vertexBase + static_cast<uint32_t>(p));
                obj.append(point);
            }
        }
        vertexBase += static_cast<uint32_t>(wire.positions.size());
        ++written;
    }

    if (written == 0) {
        return core::SetError(CgResult::InvalidState,
                              "Export: nothing to write%s",
                              options.selectionOnly ? " (the selection is empty)" : "");
    }

    const CgResult r = core::WriteFile(utf8Path, obj.data(), obj.size());
    if (CgFailed(r)) {
        return r;
    }
    // .mtl 写不出来不算导出失败：OBJ 本身是完整的，读它的人只是拿不到颜色。
    const std::string mtlPath = core::JoinPath(core::DirectoryOf(utf8Path), mtlName);
    if (CgFailed(core::WriteFile(mtlPath.c_str(), mtl.data(), mtl.size()))) {
        CG_WARN("wrote '%s' but not its material library: %s", utf8Path, core::LastErrorMessage());
        core::ClearError();
    }

    CG_INFO("exported %u object(s) to '%s'", written, utf8Path);
    if (progress) {
        progress(1.0f, utf8Path, userData);
    }
    return CgResult::Ok;
}

// ---------------------------------------------------------------------------
// 读入
// ---------------------------------------------------------------------------

/// `mtllib` 指的文件名。ParseFromString 明说会忽略这一行（它不碰文件系统），所以
/// 自己扫出来、自己读进来 —— 走 core::ReadTextFile 才有 UTF-8 路径。
std::string FindMtlLib(const std::string& objText) {
    size_t cursor = 0;
    while (cursor < objText.size()) {
        size_t lineEnd = objText.find('\n', cursor);
        if (lineEnd == std::string::npos) {
            lineEnd = objText.size();
        }
        const std::string line = objText.substr(cursor, lineEnd - cursor);
        cursor = lineEnd + 1;

        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line.compare(first, 7, "mtllib ") != 0) {
            continue;
        }
        std::string name = line.substr(first + 7);
        while (!name.empty() && (name.back() == '\r' || name.back() == ' ' || name.back() == '\t')) {
            name.pop_back();
        }
        if (!name.empty()) {
            return name;
        }
    }
    return {};
}

/// 顶点去重的键：OBJ 的位置和法线是两套独立下标，(v, vn) 这一对才是一个顶点。
struct ObjVertexKey {
    int position;
    int normal;

    bool operator==(const ObjVertexKey& other) const {
        return position == other.position && normal == other.normal;
    }
};

struct ObjVertexHash {
    size_t operator()(const ObjVertexKey& key) const noexcept {
        return (static_cast<size_t>(static_cast<uint32_t>(key.position)) << 16) ^
               static_cast<size_t>(static_cast<uint32_t>(key.normal));
    }
};

class ObjImporter final : public IImporter {
public:
    explicit ObjImporter(ISceneSink& sink) : sink_(sink) {}

    void Release() override { delete this; }

    const char* GetName() const override { return "Wavefront OBJ"; }

    CgResult Import(const char* utf8Path, IScene* scene, const ImportOptions& options,
                    IoProgressCallback progress, void* userData) override;

private:
    ~ObjImporter() override = default;

    core::ObjectTracker tracker_;
    ISceneSink& sink_;
};

// `scene` 有意不用：内置处理器往场景里放东西走的是 ISceneSink，因为公开接口造不出
// ShapeType::Mesh（io/SceneAccess.h 说了为什么）。那个参数是给宿主自己的处理器的。
CgResult ObjImporter::Import(const char* utf8Path, IScene* /*scene*/, const ImportOptions& options,
                             IoProgressCallback progress, void* userData) {
    std::string objText;
    CgResult r = core::ReadTextFile(utf8Path, objText);
    if (CgFailed(r)) {
        return r;
    }

    std::string mtlText;
    const std::string mtlName = FindMtlLib(objText);
    if (!mtlName.empty()) {
        const std::string mtlPath = core::JoinPath(core::DirectoryOf(utf8Path), mtlName);
        if (CgFailed(core::ReadTextFile(mtlPath.c_str(), mtlText))) {
            // 材质库缺失是家常便饭（模型常常单独流传），几何还在，照读不误。
            CG_WARN("'%s' references '%s', which is not there; importing without materials",
                    utf8Path, mtlName.c_str());
            core::ClearError();
        }
    }

    tinyobj::ObjReaderConfig config;
    config.triangulate = true;
    config.vertex_color = false;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromString(objText, mtlText, config) || !reader.Valid()) {
        return core::SetError(CgResult::ParseError, "cannot parse '%s': %s", utf8Path,
                              reader.Error().empty() ? "malformed OBJ" : reader.Error().c_str());
    }
    if (!reader.Warning().empty()) {
        CG_WARN("while reading '%s': %s", utf8Path, reader.Warning().c_str());
    }

    const tinyobj::attrib_t& attrib = reader.GetAttrib();
    const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();
    const std::vector<tinyobj::material_t>& materials = reader.GetMaterials();
    if (attrib.vertices.empty()) {
        return core::SetError(CgResult::ParseError, "'%s' has no vertices", utf8Path);
    }

    // 单位换算和轴向换算都烤进顶点里：OBJ 的实体身上没有变换可挂，网格就是全部。
    const double scale = options.scaleToModelUnits > 0.0 ? options.scaleToModelUnits : 1.0;
    const auto position = [&](int index) {
        const auto base = static_cast<size_t>(index) * 3;
        return ToZUp(Vec3d{attrib.vertices[base], attrib.vertices[base + 1],
                           attrib.vertices[base + 2]} *
                     scale);
    };
    const auto normal = [&](int index) {
        const auto base = static_cast<size_t>(index) * 3;
        return ToZUp(
            Vec3d{attrib.normals[base], attrib.normals[base + 1], attrib.normals[base + 2]});
    };

    std::string label = "Import ";
    label.append(core::StemOf(utf8Path));
    r = sink_.Begin(label.c_str(), !options.mergeIntoScene);
    if (CgFailed(r)) {
        return r;
    }

    uint32_t created = 0;
    for (size_t s = 0; s < shapes.size(); ++s) {
        const tinyobj::shape_t& shape = shapes[s];
        const std::string name = shape.name.empty() ? "Mesh" : shape.name;
        if (progress &&
            !progress(static_cast<float>(s) / static_cast<float>(shapes.size()), name.c_str(),
                      userData)) {
            sink_.End(/*commit=*/false);
            return core::SetError(CgResult::Unknown, "Import: cancelled by the host");
        }

        EntityStyle style{};
        if (!shape.mesh.material_ids.empty()) {
            const int material = shape.mesh.material_ids.front();
            if (material >= 0 && static_cast<size_t>(material) < materials.size()) {
                const tinyobj::material_t& m = materials[static_cast<size_t>(material)];
                style.color = Color{static_cast<float>(m.diffuse[0]), static_cast<float>(m.diffuse[1]),
                                    static_cast<float>(m.diffuse[2]),
                                    static_cast<float>(m.dissolve)};
            }
        }

        // -- 三角形 ---------------------------------------------------------
        if (!shape.mesh.indices.empty()) {
            auto mesh = std::make_shared<geom::MeshData>();
            std::unordered_map<ObjVertexKey, uint32_t, ObjVertexHash> unique;
            unique.reserve(shape.mesh.indices.size());

            bool hasNormals = true;
            for (const tinyobj::index_t& index : shape.mesh.indices) {
                if (index.vertex_index < 0) {
                    continue;
                }
                const ObjVertexKey key{index.vertex_index, index.normal_index};
                const auto [it, inserted] =
                    unique.try_emplace(key, static_cast<uint32_t>(mesh->vertices.size()));
                if (inserted) {
                    geom::MeshVertex vertex{};
                    vertex.position = position(index.vertex_index);
                    if (index.normal_index >= 0 && !attrib.normals.empty()) {
                        vertex.normal = normal(index.normal_index);
                    } else {
                        hasNormals = false;
                    }
                    mesh->vertices.push_back(vertex);
                }
                mesh->indices.push_back(it->second);
            }

            if (mesh->indices.size() >= 3) {
                if (!hasNormals) {
                    // 文件里没有法线。逐面拍平而不是猜光滑组：CAD 导出的网格绝大
                    // 多数是硬边的，猜错的代价是一个本该有棱的零件糊成一团。
                    geom::GenerateFlatNormals(*mesh);
                }
                mesh->RecomputeBounds();

                geom::ShapeDef def{};
                def.params.type = ShapeType::Mesh;
                def.mesh = std::move(mesh);
                if (IsValid(sink_.AddShape(name.c_str(), kInvalidEntity, Transform{}, style,
                                           std::move(def)))) {
                    ++created;
                }
            }
        }

        // -- 折线（`l`）------------------------------------------------------
        size_t lineCursor = 0;
        for (size_t l = 0; l < shape.lines.num_line_vertices.size(); ++l) {
            const auto vertexCount = static_cast<size_t>(shape.lines.num_line_vertices[l]);
            if (vertexCount >= 2 && lineCursor + vertexCount <= shape.lines.indices.size()) {
                geom::ShapeDef def{};
                def.params.type = ShapeType::Polyline;
                def.points.reserve(vertexCount);
                for (size_t v = 0; v < vertexCount; ++v) {
                    def.points.push_back(
                        position(shape.lines.indices[lineCursor + v].vertex_index));
                }
                // 首尾同一个点的折线是一个闭环，别把它读成一条绕回来的开口线：
                // 闭合与否决定了它还能不能当拉伸的轮廓。
                if (def.points.size() >= 4 &&
                    Distance(def.points.front(), def.points.back()) <= kEpsilon) {
                    def.points.pop_back();
                    def.closed = true;
                }
                if (IsValid(sink_.AddShape(name.c_str(), kInvalidEntity, Transform{}, style,
                                           std::move(def)))) {
                    ++created;
                }
            }
            lineCursor += vertexCount;
        }

        // -- 点（`p`）--------------------------------------------------------
        for (const tinyobj::index_t& index : shape.points.indices) {
            if (index.vertex_index < 0) {
                continue;
            }
            geom::ShapeDef def{};
            def.params.type = ShapeType::Point;
            def.params.point.position = position(index.vertex_index);
            if (IsValid(sink_.AddShape(name.c_str(), kInvalidEntity, Transform{}, style,
                                       std::move(def)))) {
                ++created;
            }
        }
    }

    if (created == 0) {
        sink_.End(/*commit=*/false);
        return core::SetError(CgResult::ParseError,
                             "'%s' parsed but produced no geometry the engine can hold", utf8Path);
    }

    r = sink_.End(/*commit=*/true);
    if (CgFailed(r)) {
        return r;
    }
    CG_INFO("imported %u object(s) from '%s'", created, utf8Path);
    if (progress) {
        progress(1.0f, utf8Path, userData);
    }
    return CgResult::Ok;
}

} // namespace

IImporter* CreateObjImporter(ISceneSink& sink) {
    return new ObjImporter(sink);
}

IExporter* CreateObjExporter(ISceneSource& source) {
    return new ObjExporter(source);
}

} // namespace cadgeom::io
