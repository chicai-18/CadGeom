/**
 * @file GltfIO.cpp
 * @brief glTF 2.0 读写，含 `extras` 参数化往返（docs/architecture.md §7）。
 *
 * glTF 事实上是 CadGeom 的原生格式，省掉了自研一套文件格式的功夫。诀窍只有一条：
 * **把参数化定义序列化进节点的 `extras`**。
 *
 *   - 自家导出 → 自家导入：`extras` 在，圆还是圆、拉伸还是拉伸，半径和高度照样能改。
 *     网格数据这时候是被**忽略**的 —— 参数才是真相，三角形是从参数重新生成的缓存
 *     （§3.1）。让导入器去信文件里的三角形，等于让缓存反过来定义真相。
 *   - Blender 或者别家读：`extras` 是规范允许的自定义字段，读不懂就跳过，剩下的是
 *     一个再普通不过的 mesh，完全兼容。
 *
 * 精度：`extras` 里的参数是 double，往返分毫不差；而 glTF 的 POSITION 按规范只能是
 * float32，所以**没有**参数化定义的那些实体（导入进来的网格）往返一次是 float 精度。
 * 这是格式本身的下限，不是这里偷的懒。
 *
 * 轴向：整棵场景挂在一个带 -90° 绕 X 旋转的根节点下（io/Axis.h）。顶点和 `extras`
 * 因此都留在对象空间里，两者永远对得上；换算只在那一个节点上发生。
 */
#include "io/Registry.h"

#include <cadgeom/Version.h>

#include "core/Error.h"
#include "core/File.h"
#include "core/Log.h"
#include "core/Math.h"
#include "core/ObjectTracker.h"
#include "io/Axis.h"
#include "io/GltfCommon.h"
#include "io/SceneAccess.h"

#include <string.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace cadgeom::io {
namespace {

/// `extras.cadgeom` 的版本。读到更高的版本就只当它是个普通网格 —— 半懂不懂地解析
/// 一个未来格式，比老老实实承认读不懂糟得多。
constexpr int kExtrasVersion = 1;
constexpr const char* kExtrasKey = "cadgeom";

// ---------------------------------------------------------------------------
// extras 编解码 —— 参数化保真的全部所在
// ---------------------------------------------------------------------------

using GltfObject = tinygltf::Value::Object;
using GltfArray = tinygltf::Value::Array;

tinygltf::Value MakeVec3(const Vec3d& v) {
    return tinygltf::Value(GltfArray{tinygltf::Value(v.x), tinygltf::Value(v.y),
                                     tinygltf::Value(v.z)});
}

tinygltf::Value MakePlane(const Plane& plane) {
    return tinygltf::Value(GltfObject{{"origin", MakeVec3(plane.origin)},
                                      {"normal", MakeVec3(plane.normal)}});
}

const tinygltf::Value* Field(const tinygltf::Value& value, const char* key) {
    if (!value.IsObject() || !value.Has(key)) {
        return nullptr;
    }
    return &value.Get(key);
}

bool ReadNumber(const tinygltf::Value& value, const char* key, double& out) {
    const tinygltf::Value* field = Field(value, key);
    if (!field || !field->IsNumber()) {
        return false;
    }
    out = field->GetNumberAsDouble();
    return true;
}

bool ReadBool(const tinygltf::Value& value, const char* key, bool& out) {
    const tinygltf::Value* field = Field(value, key);
    if (!field || !field->IsBool()) {
        return false;
    }
    out = field->Get<bool>();
    return true;
}

bool ReadVec3(const tinygltf::Value& value, const char* key, Vec3d& out) {
    const tinygltf::Value* field = Field(value, key);
    if (!field || !field->IsArray() || field->ArrayLen() < 3) {
        return false;
    }
    out = Vec3d{field->Get(0).GetNumberAsDouble(), field->Get(1).GetNumberAsDouble(),
                field->Get(2).GetNumberAsDouble()};
    return true;
}

bool ReadPlane(const tinygltf::Value& value, const char* key, Plane& out) {
    const tinygltf::Value* field = Field(value, key);
    if (!field) {
        return false;
    }
    return ReadVec3(*field, "origin", out.origin) && ReadVec3(*field, "normal", out.normal);
}

std::string ReadString(const tinygltf::Value& value, const char* key) {
    const tinygltf::Value* field = Field(value, key);
    return (field && field->IsString()) ? field->Get<std::string>() : std::string{};
}

/// 类型名写成字符串而不是枚举值：`"type": "Circle"` 是给人看的，而且哪天枚举重排了
/// 也不会把老文件读成别的东西。
const char* ShapeTypeName(ShapeType type) {
    switch (type) {
        case ShapeType::Point:     return "Point";
        case ShapeType::Line:      return "Line";
        case ShapeType::Circle:    return "Circle";
        case ShapeType::Arc:       return "Arc";
        case ShapeType::Rectangle: return "Rectangle";
        case ShapeType::Polyline:  return "Polyline";
        case ShapeType::Solid:     return "Solid";
        case ShapeType::Mesh:
        case ShapeType::None:
        default:                   return "";
    }
}

ShapeType ShapeTypeFromName(const std::string& name) {
    if (name == "Point")     return ShapeType::Point;
    if (name == "Line")      return ShapeType::Line;
    if (name == "Circle")    return ShapeType::Circle;
    if (name == "Arc")       return ShapeType::Arc;
    if (name == "Rectangle") return ShapeType::Rectangle;
    if (name == "Polyline")  return ShapeType::Polyline;
    if (name == "Solid")     return ShapeType::Solid;
    return ShapeType::None;
}

/// @brief 参数化定义 → `extras` 里的一个对象。
/// @return 类型为空的对象表示这个形状没有参数可写（导入的网格）。
tinygltf::Value EncodeShape(const geom::ShapeDef& def) {
    const ShapeParams& p = def.params;
    const char* typeName = ShapeTypeName(p.type);
    if (!*typeName) {
        return tinygltf::Value();
    }

    GltfObject object{{"type", tinygltf::Value(std::string(typeName))}};
    switch (p.type) {
        case ShapeType::Point:
            object["position"] = MakeVec3(p.point.position);
            break;
        case ShapeType::Line:
            object["start"] = MakeVec3(p.line.start);
            object["end"] = MakeVec3(p.line.end);
            break;
        case ShapeType::Circle:
            object["plane"] = MakePlane(p.circle.plane);
            object["radius"] = tinygltf::Value(p.circle.radius);
            break;
        case ShapeType::Arc:
            object["plane"] = MakePlane(p.arc.plane);
            object["radius"] = tinygltf::Value(p.arc.radius);
            object["startAngle"] = tinygltf::Value(p.arc.startAngle);
            object["sweepAngle"] = tinygltf::Value(p.arc.sweepAngle);
            break;
        case ShapeType::Rectangle:
            object["plane"] = MakePlane(p.rectangle.plane);
            object["uAxis"] = MakeVec3(p.rectangle.uAxis);
            object["width"] = tinygltf::Value(p.rectangle.width);
            object["height"] = tinygltf::Value(p.rectangle.height);
            break;
        case ShapeType::Polyline: {
            // 多段线的点表是 ShapeParams 那个冻结的 POD union 装不下的东西
            // （CLAUDE.md 里记着这个缺口）。文件格式没有这个限制，所以 glTF 的往返
            // 反而比公开接口的 GetParams 还全。
            GltfArray points;
            points.reserve(def.points.size());
            for (const Vec3d& point : def.points) {
                points.push_back(MakeVec3(point));
            }
            object["points"] = tinygltf::Value(std::move(points));
            object["closed"] = tinygltf::Value(def.closed);
            break;
        }
        case ShapeType::Solid:
            object["direction"] = MakeVec3(p.extrude.direction);
            object["distance"] = tinygltf::Value(p.extrude.distance);
            object["draftAngle"] = tinygltf::Value(p.extrude.options.draftAngle);
            object["bothDirections"] = tinygltf::Value(p.extrude.options.bothDirections);
            object["capEnds"] = tinygltf::Value(p.extrude.options.capEnds);
            // 轮廓带的是一份定义的拷贝，不是一个 id（geom/Shape.h）。所以它跟着实体
            // 一起写进文件，轮廓那个实体在不在场景里都不影响 —— 也正因如此，读回来的
            // 实体照样能改高度重新扫掠。
            if (def.profile) {
                object["profile"] = EncodeShape(*def.profile);
            }
            break;
        default:
            break;
    }
    return tinygltf::Value(std::move(object));
}

/// @brief `extras` 里的对象 → 参数化定义。
/// @return 认不出的类型为 false，调用方退回读网格。
bool DecodeShape(const tinygltf::Value& value, geom::ShapeDef& out) {
    const ShapeType type = ShapeTypeFromName(ReadString(value, "type"));
    out = geom::ShapeDef{};
    out.params.type = type;

    switch (type) {
        case ShapeType::Point:
            return ReadVec3(value, "position", out.params.point.position);
        case ShapeType::Line:
            return ReadVec3(value, "start", out.params.line.start) &&
                   ReadVec3(value, "end", out.params.line.end);
        case ShapeType::Circle:
            return ReadPlane(value, "plane", out.params.circle.plane) &&
                   ReadNumber(value, "radius", out.params.circle.radius);
        case ShapeType::Arc:
            return ReadPlane(value, "plane", out.params.arc.plane) &&
                   ReadNumber(value, "radius", out.params.arc.radius) &&
                   ReadNumber(value, "startAngle", out.params.arc.startAngle) &&
                   ReadNumber(value, "sweepAngle", out.params.arc.sweepAngle);
        case ShapeType::Rectangle:
            return ReadPlane(value, "plane", out.params.rectangle.plane) &&
                   ReadVec3(value, "uAxis", out.params.rectangle.uAxis) &&
                   ReadNumber(value, "width", out.params.rectangle.width) &&
                   ReadNumber(value, "height", out.params.rectangle.height);
        case ShapeType::Polyline: {
            const tinygltf::Value* points = Field(value, "points");
            if (!points || !points->IsArray()) {
                return false;
            }
            out.points.reserve(points->ArrayLen());
            for (size_t i = 0; i < points->ArrayLen(); ++i) {
                const tinygltf::Value& point = points->Get(i);
                if (!point.IsArray() || point.ArrayLen() < 3) {
                    return false;
                }
                out.points.push_back(Vec3d{point.Get(0).GetNumberAsDouble(),
                                           point.Get(1).GetNumberAsDouble(),
                                           point.Get(2).GetNumberAsDouble()});
            }
            ReadBool(value, "closed", out.closed);
            return out.points.size() >= 2;
        }
        case ShapeType::Solid: {
            const tinygltf::Value* profile = Field(value, "profile");
            if (!profile) {
                return false;
            }
            auto profileDef = std::make_shared<geom::ShapeDef>();
            if (!DecodeShape(*profile, *profileDef)) {
                return false;
            }
            out.profile = profileDef;
            // 出处那个 ShapeId 有意不写进文件：它是本次会话里的一个下标，换一个
            // 进程就没有意义了。实体自足靠的是上面那份轮廓拷贝，不是这个 id。
            out.params.extrude.profile = kInvalidShape;
            if (!ReadVec3(value, "direction", out.params.extrude.direction) ||
                !ReadNumber(value, "distance", out.params.extrude.distance)) {
                return false;
            }
            ReadNumber(value, "draftAngle", out.params.extrude.options.draftAngle);
            ReadBool(value, "bothDirections", out.params.extrude.options.bothDirections);
            ReadBool(value, "capEnds", out.params.extrude.options.capEnds);
            return true;
        }
        default:
            return false;
    }
}

tinygltf::Value EncodeStyle(const EntityStyle& style, bool visible) {
    const auto color = [](const Color& c) {
        return tinygltf::Value(GltfArray{tinygltf::Value(static_cast<double>(c.r)),
                                         tinygltf::Value(static_cast<double>(c.g)),
                                         tinygltf::Value(static_cast<double>(c.b)),
                                         tinygltf::Value(static_cast<double>(c.a))});
    };
    // 线宽、虚线样式、边的颜色在 glTF 里没有对应物，只有 extras 装得下 —— 而一张
    // 图纸的可读性有一半在这些东西上。
    return tinygltf::Value(GltfObject{
        {"color", color(style.color)},
        {"edgeColor", color(style.edgeColor)},
        {"lineWidth", tinygltf::Value(static_cast<double>(style.lineWidth))},
        {"pointSize", tinygltf::Value(static_cast<double>(style.pointSize))},
        {"lineStyle", tinygltf::Value(static_cast<int>(style.lineStyle))},
        {"visible", tinygltf::Value(visible)},
    });
}

void DecodeStyle(const tinygltf::Value& value, EntityStyle& style, bool& visible) {
    const auto color = [&](const char* key, Color& out) {
        const tinygltf::Value* field = Field(value, key);
        if (!field || !field->IsArray() || field->ArrayLen() < 4) {
            return;
        }
        out = Color{static_cast<float>(field->Get(0).GetNumberAsDouble()),
                    static_cast<float>(field->Get(1).GetNumberAsDouble()),
                    static_cast<float>(field->Get(2).GetNumberAsDouble()),
                    static_cast<float>(field->Get(3).GetNumberAsDouble())};
    };
    color("color", style.color);
    color("edgeColor", style.edgeColor);

    double number = 0.0;
    if (ReadNumber(value, "lineWidth", number)) {
        style.lineWidth = static_cast<float>(number);
    }
    if (ReadNumber(value, "pointSize", number)) {
        style.pointSize = static_cast<float>(number);
    }
    if (ReadNumber(value, "lineStyle", number)) {
        style.lineStyle = static_cast<LineStyle>(static_cast<int32_t>(number));
    }
    ReadBool(value, "visible", visible);
    style.visible = visible;
}

// ---------------------------------------------------------------------------
// 写出
// ---------------------------------------------------------------------------

/// 往模型的那一个 buffer 里追加一段数据，返回它的 bufferView 下标。
int AddBufferView(tinygltf::Model& model, const void* data, size_t size, int target) {
    tinygltf::Buffer& buffer = model.buffers.front();
    // 每个 view 都对齐到 4 字节：glTF 规范要求 accessor 的偏移量能被它的分量大小
    // 整除，而我们最大的分量正是 4 字节。
    while (buffer.data.size() % 4 != 0) {
        buffer.data.push_back(0);
    }

    tinygltf::BufferView view{};
    view.buffer = 0;
    view.byteOffset = buffer.data.size();
    view.byteLength = size;
    view.target = target;

    const auto* bytes = static_cast<const unsigned char*>(data);
    buffer.data.insert(buffer.data.end(), bytes, bytes + size);

    model.bufferViews.push_back(view);
    return static_cast<int>(model.bufferViews.size() - 1);
}

/// vec3 的 float 数组 → accessor。`writeBounds` 只对 POSITION 为真：规范要求它带
/// min/max，别的属性带了也没人看。
int AddVec3Accessor(tinygltf::Model& model, const std::vector<float>& values, bool writeBounds) {
    const int view = AddBufferView(model, values.data(), values.size() * sizeof(float),
                                   TINYGLTF_TARGET_ARRAY_BUFFER);
    tinygltf::Accessor accessor{};
    accessor.bufferView = view;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    accessor.type = TINYGLTF_TYPE_VEC3;
    accessor.count = values.size() / 3;

    if (writeBounds && accessor.count > 0) {
        std::vector<double> lo{values[0], values[1], values[2]};
        std::vector<double> hi = lo;
        for (size_t i = 3; i + 2 < values.size(); i += 3) {
            for (size_t c = 0; c < 3; ++c) {
                const double value = values[i + c];
                lo[c] = std::min(lo[c], value);
                hi[c] = std::max(hi[c], value);
            }
        }
        accessor.minValues = std::move(lo);
        accessor.maxValues = std::move(hi);
    }

    model.accessors.push_back(accessor);
    return static_cast<int>(model.accessors.size() - 1);
}

int AddIndexAccessor(tinygltf::Model& model, const std::vector<uint32_t>& indices) {
    const int view = AddBufferView(model, indices.data(), indices.size() * sizeof(uint32_t),
                                   TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);
    tinygltf::Accessor accessor{};
    accessor.bufferView = view;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
    accessor.type = TINYGLTF_TYPE_SCALAR;
    accessor.count = indices.size();
    model.accessors.push_back(accessor);
    return static_cast<int>(model.accessors.size() - 1);
}

int AddMaterial(tinygltf::Model& model, const char* name, const Color& color) {
    tinygltf::Material material{};
    material.name = name;
    // baseColorFactor 按规范就是线性光，而 EntityStyle::color 也是线性的
    // （CLAUDE.md 的约定），所以这里不需要任何编码换算。
    material.pbrMetallicRoughness.baseColorFactor = {color.r, color.g, color.b, color.a};
    material.pbrMetallicRoughness.metallicFactor = 0.0;
    material.pbrMetallicRoughness.roughnessFactor = 0.65;
    material.doubleSided = true;
    if (color.a < 1.0f) {
        material.alphaMode = "BLEND";
    }
    model.materials.push_back(material);
    return static_cast<int>(model.materials.size() - 1);
}

class GltfExporter final : public IExporter {
public:
    GltfExporter(ISceneSource& source, bool binary) : source_(source), binary_(binary) {}

    void Release() override { delete this; }

    const char* GetName() const override { return binary_ ? "glTF 2.0 Binary" : "glTF 2.0"; }

    CgResult Export(const char* utf8Path, const IScene* scene, const ExportOptions& options,
                    IoProgressCallback progress, void* userData) override;

private:
    ~GltfExporter() override = default;

    core::ObjectTracker tracker_;
    ISceneSource& source_;
    bool binary_;
};

CgResult GltfExporter::Export(const char* utf8Path, const IScene* /*scene*/,
                              const ExportOptions& options, IoProgressCallback progress,
                              void* userData) {
    // 先把整个场景收下来：层级要用父子关系，选择集导出还要往上找祖先，两趟都得
    // 能随机访问。
    std::vector<SceneEntityView> views;
    const uint32_t count = source_.GetEntityCount();
    views.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        SceneEntityView view{};
        if (source_.GetEntity(i, view)) {
            views.push_back(view);
        }
    }

    std::unordered_map<uint64_t, size_t> indexOf;
    for (size_t i = 0; i < views.size(); ++i) {
        indexOf[views[i].id.value] = i;
    }

    // selectionOnly：选中的实体连同它的整棵子树都要，而它们的祖先即使没被选中也
    // 得留成空节点 —— 变换挂在祖先身上，跳过它子节点就会落到别处去。
    std::vector<bool> keep(views.size(), true);
    std::vector<bool> content(views.size(), true);
    if (options.selectionOnly) {
        // 父节点一定排在子节点前面，所以「祖先被选中」一趟就传下去了。
        for (size_t i = 0; i < views.size(); ++i) {
            const auto parent = indexOf.find(views[i].parent.value);
            const bool parentSelected = parent != indexOf.end() && content[parent->second];
            content[i] = views[i].selected || parentSelected;
            keep[i] = content[i];
        }
        for (size_t i = views.size(); i-- > 0;) {
            if (!keep[i]) {
                continue;
            }
            const auto parent = indexOf.find(views[i].parent.value);
            if (parent != indexOf.end()) {
                keep[parent->second] = true;
            }
        }
    }

    tinygltf::Model model;
    model.asset.version = "2.0";
    model.asset.generator = CadGeom_GetBuildInfo();
    model.buffers.emplace_back();

    // 根节点扛着 Z-up → Y-up 的那一次旋转。烤进顶点里也能得到同样的画面，但那样
    // `extras` 里的对象空间参数就和顶点对不上了，往返也就不再精确。
    tinygltf::Node root{};
    root.name = "CadGeom";
    const Quatd swap = YUpRotation();
    root.rotation = {swap.x, swap.y, swap.z, swap.w};
    model.nodes.push_back(root);

    std::unordered_map<uint64_t, int> nodeOf;
    uint32_t exported = 0;

    for (size_t i = 0; i < views.size(); ++i) {
        if (!keep[i]) {
            continue;
        }
        const SceneEntityView& view = views[i];
        if (progress && !progress(static_cast<float>(i) / static_cast<float>(views.size()),
                                  view.name, userData)) {
            return core::SetError(CgResult::Unknown, "Export: cancelled by the host");
        }

        tinygltf::Node node{};
        node.name = view.name ? view.name : "";
        node.translation = {view.local.translation.x, view.local.translation.y,
                            view.local.translation.z};
        node.rotation = {view.local.rotation.x, view.local.rotation.y, view.local.rotation.z,
                         view.local.rotation.w};
        node.scale = {view.local.scale.x, view.local.scale.y, view.local.scale.z};

        if (options.writeParametricExtras && view.def) {
            const tinygltf::Value shape = EncodeShape(*view.def);
            GltfObject payload{{"version", tinygltf::Value(kExtrasVersion)},
                               {"style", EncodeStyle(view.style, view.visible)}};
            if (shape.IsObject()) {
                payload["shape"] = shape;
            }
            node.extras = tinygltf::Value(
                GltfObject{{kExtrasKey, tinygltf::Value(std::move(payload))}});
        }

        // content 为假的是「只为了保住变换而留下来的祖先」，几何不写。
        if (content[i] && view.shape) {
            const geom::MeshData& mesh = view.shape->mesh;
            const geom::PolylineData& wire = view.shape->wire;

            tinygltf::Mesh gltfMesh{};
            gltfMesh.name = node.name;

            if (!mesh.IsEmpty()) {
                std::vector<float> positions;
                std::vector<float> normals;
                positions.reserve(mesh.vertices.size() * 3);
                normals.reserve(mesh.vertices.size() * 3);
                for (const geom::MeshVertex& v : mesh.vertices) {
                    positions.push_back(static_cast<float>(v.position.x));
                    positions.push_back(static_cast<float>(v.position.y));
                    positions.push_back(static_cast<float>(v.position.z));
                    normals.push_back(static_cast<float>(v.normal.x));
                    normals.push_back(static_cast<float>(v.normal.y));
                    normals.push_back(static_cast<float>(v.normal.z));
                }

                tinygltf::Primitive primitive{};
                primitive.mode = TINYGLTF_MODE_TRIANGLES;
                primitive.attributes["POSITION"] = AddVec3Accessor(model, positions, true);
                primitive.attributes["NORMAL"] = AddVec3Accessor(model, normals, false);
                primitive.indices = AddIndexAccessor(model, mesh.indices);
                primitive.material = AddMaterial(model, node.name.c_str(), view.style.color);
                gltfMesh.primitives.push_back(primitive);
            } else if (!wire.positions.empty()) {
                // 曲线走 LINES，点图元走 POINTS。两种模式都在规范里，读不懂它们的
                // 查看器会跳过这个 primitive，而参数化定义在 `extras` 上，和这里
                // 写不写得成无关。
                std::vector<float> positions;
                positions.reserve(wire.positions.size() * 3);
                for (const Vec3d& p : wire.positions) {
                    positions.push_back(static_cast<float>(p.x));
                    positions.push_back(static_cast<float>(p.y));
                    positions.push_back(static_cast<float>(p.z));
                }

                tinygltf::Primitive primitive{};
                primitive.attributes["POSITION"] = AddVec3Accessor(model, positions, true);
                primitive.material = AddMaterial(model, node.name.c_str(), view.style.color);
                if (wire.SegmentCount() > 0) {
                    primitive.mode = TINYGLTF_MODE_LINE;
                    primitive.indices = AddIndexAccessor(model, wire.indices);
                } else {
                    primitive.mode = TINYGLTF_MODE_POINTS;
                }
                gltfMesh.primitives.push_back(primitive);
            }

            if (!gltfMesh.primitives.empty()) {
                model.meshes.push_back(gltfMesh);
                node.mesh = static_cast<int>(model.meshes.size() - 1);
                ++exported;
            }
        }

        const auto nodeIndex = static_cast<int>(model.nodes.size());
        model.nodes.push_back(node);
        nodeOf[view.id.value] = nodeIndex;

        // 父节点排在前面，所以这时候它一定已经在 nodeOf 里了；找不到就说明这是
        // 一个根，挂到那个换轴的节点下面去。
        const auto parent = nodeOf.find(view.parent.value);
        model.nodes[parent != nodeOf.end() ? static_cast<size_t>(parent->second) : 0]
            .children.push_back(nodeIndex);
    }

    if (model.nodes.size() <= 1) {
        return core::SetError(CgResult::InvalidState, "Export: nothing to write%s",
                              options.selectionOnly ? " (the selection is empty)" : "");
    }

    tinygltf::Scene scene{};
    scene.name = "CadGeom";
    scene.nodes.push_back(0);
    model.scenes.push_back(scene);
    model.defaultScene = 0;

    // 一个字节都没写进去（整场景全是组节点）时把 buffer 摘掉：byteLength 为 0 的
    // buffer 在规范里是非法的，写出去会得到一个别人读不了的文件。
    if (model.buffers.front().data.empty()) {
        model.buffers.clear();
    }

    // 先序列化到内存再自己落盘：tinygltf 自带的写文件走的是 narrow 路径，中文目录
    // 下会失败，而公开接口承诺的是 UTF-8（core/File.h）。
    tinygltf::TinyGLTF writer;
    std::ostringstream stream(std::ios::binary);
    if (!writer.WriteGltfSceneToStream(&model, stream, /*prettyPrint=*/!binary_,
                                       /*writeBinary=*/binary_)) {
        return core::SetError(CgResult::IoError, "cannot serialise '%s'", utf8Path);
    }
    const std::string bytes = stream.str();

    const CgResult r = core::WriteFile(utf8Path, bytes.data(), bytes.size());
    if (CgFailed(r)) {
        return r;
    }

    CG_INFO("exported %u mesh(es) in %zu node(s) to '%s'", exported, model.nodes.size() - 1,
            utf8Path);
    if (progress) {
        progress(1.0f, utf8Path, userData);
    }
    return CgResult::Ok;
}

// ---------------------------------------------------------------------------
// 读入
// ---------------------------------------------------------------------------

/// 一个节点的局部变换。glTF 的节点要么给一个 4x4 矩阵，要么给 TRS 三件套，两种
/// 写法都得认；矩阵是列主序的，和 Mat4d 的排布正好一样。
Transform LocalTransformOf(const tinygltf::Node& node) {
    Transform local{};
    if (node.matrix.size() == 16) {
        Mat4d matrix{};
        for (int i = 0; i < 16; ++i) {
            matrix.m[i] = node.matrix[static_cast<size_t>(i)];
        }
        Decompose(matrix, local);
        return local;
    }
    if (node.translation.size() == 3) {
        local.translation = Vec3d{node.translation[0], node.translation[1], node.translation[2]};
    }
    if (node.rotation.size() == 4) {
        local.rotation =
            Quatd{node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]};
    }
    if (node.scale.size() == 3) {
        local.scale = Vec3d{node.scale[0], node.scale[1], node.scale[2]};
    }
    return local;
}

/// accessor → Vec3 列表。glTF 允许交错存放，所以步长得按 bufferView 问出来。
bool ReadVec3Accessor(const tinygltf::Model& model, int accessorIndex, std::vector<Vec3d>& out) {
    if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
        return false;
    }
    const tinygltf::Accessor& accessor = model.accessors[static_cast<size_t>(accessorIndex)];
    if (accessor.type != TINYGLTF_TYPE_VEC3 ||
        accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.bufferView < 0 ||
        static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size()) {
        return false;
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<size_t>(accessor.bufferView)];
    if (view.buffer < 0 || static_cast<size_t>(view.buffer) >= model.buffers.size()) {
        return false;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<size_t>(view.buffer)];
    const size_t stride = view.byteStride ? view.byteStride : sizeof(float) * 3;
    const size_t base = view.byteOffset + accessor.byteOffset;

    out.reserve(out.size() + accessor.count);
    for (size_t i = 0; i < accessor.count; ++i) {
        const size_t offset = base + i * stride;
        if (offset + sizeof(float) * 3 > buffer.data.size()) {
            return false;
        }
        float xyz[3];
        memcpy(xyz, buffer.data.data() + offset, sizeof(xyz));
        out.push_back(Vec3d{xyz[0], xyz[1], xyz[2]});
    }
    return true;
}

/// 下标 accessor → uint32 列表。三种整型宽度都得认：小模型普遍用 16 位。
bool ReadIndexAccessor(const tinygltf::Model& model, int accessorIndex, uint32_t vertexOffset,
                       std::vector<uint32_t>& out) {
    if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
        return false;
    }
    const tinygltf::Accessor& accessor = model.accessors[static_cast<size_t>(accessorIndex)];
    if (accessor.bufferView < 0 ||
        static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size()) {
        return false;
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<size_t>(accessor.bufferView)];
    if (view.buffer < 0 || static_cast<size_t>(view.buffer) >= model.buffers.size()) {
        return false;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<size_t>(view.buffer)];

    size_t componentSize = 0;
    switch (accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  componentSize = 1; break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: componentSize = 2; break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   componentSize = 4; break;
        default: return false;
    }
    const size_t stride = view.byteStride ? view.byteStride : componentSize;
    const size_t base = view.byteOffset + accessor.byteOffset;

    out.reserve(out.size() + accessor.count);
    for (size_t i = 0; i < accessor.count; ++i) {
        const size_t offset = base + i * stride;
        if (offset + componentSize > buffer.data.size()) {
            return false;
        }
        uint32_t index = 0;
        if (componentSize == 1) {
            index = buffer.data[offset];
        } else if (componentSize == 2) {
            uint16_t value = 0;
            memcpy(&value, buffer.data.data() + offset, sizeof(value));
            index = value;
        } else {
            memcpy(&index, buffer.data.data() + offset, sizeof(index));
        }
        out.push_back(index + vertexOffset);
    }
    return true;
}

/// glTF 的一个 mesh（可能有好几个 primitive）→ 一份三角网格。
/// 非三角形的 primitive 会被跳过：读一个外来文件时，线和点没有对应的实体类型。
bool BuildMesh(const tinygltf::Model& model, int meshIndex, double scale, geom::MeshData& out) {
    if (meshIndex < 0 || static_cast<size_t>(meshIndex) >= model.meshes.size()) {
        return false;
    }

    const tinygltf::Mesh& gltfMesh = model.meshes[static_cast<size_t>(meshIndex)];
    for (const tinygltf::Primitive& primitive : gltfMesh.primitives) {
        if (primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != -1) {
            continue;
        }
        const auto position = primitive.attributes.find("POSITION");
        if (position == primitive.attributes.end()) {
            continue;
        }

        std::vector<Vec3d> positions;
        if (!ReadVec3Accessor(model, position->second, positions) || positions.empty()) {
            continue;
        }
        std::vector<Vec3d> normals;
        const auto normal = primitive.attributes.find("NORMAL");
        if (normal != primitive.attributes.end()) {
            ReadVec3Accessor(model, normal->second, normals);
        }

        const auto vertexOffset = static_cast<uint32_t>(out.vertices.size());
        for (size_t i = 0; i < positions.size(); ++i) {
            geom::MeshVertex vertex{};
            vertex.position = positions[i] * scale;
            vertex.normal = i < normals.size() ? normals[i] : Vec3d{0.0, 0.0, 0.0};
            out.vertices.push_back(vertex);
        }

        if (primitive.indices >= 0) {
            ReadIndexAccessor(model, primitive.indices, vertexOffset, out.indices);
        } else {
            // 无下标的 primitive：顶点按顺序三个一组。
            for (uint32_t i = 0; i < positions.size(); ++i) {
                out.indices.push_back(vertexOffset + i);
            }
        }
    }

    if (out.indices.size() < 3) {
        return false;
    }
    out.indices.resize(out.indices.size() - out.indices.size() % 3);
    for (const uint32_t index : out.indices) {
        if (index >= out.vertices.size()) {
            return false;
        }
    }
    return true;
}

class GltfImporter final : public IImporter {
public:
    explicit GltfImporter(ISceneSink& sink) : sink_(sink) {}

    void Release() override { delete this; }

    const char* GetName() const override { return "glTF 2.0"; }

    CgResult Import(const char* utf8Path, IScene* scene, const ImportOptions& options,
                    IoProgressCallback progress, void* userData) override;

private:
    ~GltfImporter() override = default;

    /// 一个节点连同它的子树。`local` 已经是最终的局部变换（顶层节点上叠了换轴和
    /// 单位换算）。
    uint32_t AddNode(const tinygltf::Model& model, int nodeIndex, EntityId parent,
                     const Transform& local, const ImportOptions& options);

    core::ObjectTracker tracker_;
    ISceneSink& sink_;
};

uint32_t GltfImporter::AddNode(const tinygltf::Model& model, int nodeIndex, EntityId parent,
                               const Transform& local, const ImportOptions& options) {
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= model.nodes.size()) {
        return 0;
    }
    const tinygltf::Node& node = model.nodes[static_cast<size_t>(nodeIndex)];
    const std::string name = node.name.empty() ? "Node" : node.name;

    EntityStyle style{};
    bool visible = true;
    geom::ShapeDef def{};
    bool hasShape = false;

    const tinygltf::Value* payload = Field(node.extras, kExtrasKey);
    if (payload && options.readParametricExtras) {
        double version = 0.0;
        if (ReadNumber(*payload, "version", version) &&
            static_cast<int>(version) <= kExtrasVersion) {
            if (const tinygltf::Value* styleValue = Field(*payload, "style")) {
                DecodeStyle(*styleValue, style, visible);
            }
            // 参数在，就以参数为准，文件里的三角形**有意**不读：参数是真相，网格是
            // 从它重新生成的缓存（§3.1）。反过来信网格，等于让缓存定义真相。
            if (const tinygltf::Value* shapeValue = Field(*payload, "shape")) {
                hasShape = DecodeShape(*shapeValue, def);
            }
        }
    }

    const double scale = options.scaleToModelUnits > 0.0 ? options.scaleToModelUnits : 1.0;
    if (!hasShape && node.mesh >= 0) {
        auto mesh = std::make_shared<geom::MeshData>();
        // 单位换算烤进顶点：变换上再乘一次缩放的话，同一个模型里参数化实体和网格
        // 实体会缩两次和一次，对不上。
        if (BuildMesh(model, node.mesh, scale, *mesh)) {
            bool hasNormals = false;
            for (const geom::MeshVertex& v : mesh->vertices) {
                if (LengthSq(v.normal) > kEpsilon) {
                    hasNormals = true;
                    break;
                }
            }
            if (!hasNormals) {
                geom::GenerateFlatNormals(*mesh);
            }
            mesh->RecomputeBounds();

            def = geom::ShapeDef{};
            def.params.type = ShapeType::Mesh;
            def.mesh = std::move(mesh);
            hasShape = true;

            // 没有 extras 的文件（别家导出的）用材质的基色，至少颜色是对的。
            if (!payload && node.mesh < static_cast<int>(model.meshes.size())) {
                const tinygltf::Mesh& gltfMesh = model.meshes[static_cast<size_t>(node.mesh)];
                if (!gltfMesh.primitives.empty()) {
                    const int material = gltfMesh.primitives.front().material;
                    if (material >= 0 && static_cast<size_t>(material) < model.materials.size()) {
                        const std::vector<double>& base =
                            model.materials[static_cast<size_t>(material)]
                                .pbrMetallicRoughness.baseColorFactor;
                        if (base.size() >= 4) {
                            style.color = Color{
                                static_cast<float>(base[0]), static_cast<float>(base[1]),
                                static_cast<float>(base[2]), static_cast<float>(base[3])};
                        }
                    }
                }
            }
        }
    }

    const EntityId entity = hasShape
                                ? sink_.AddShape(name.c_str(), parent, local, style, std::move(def))
                                : sink_.AddGroup(name.c_str(), parent, local);
    if (!IsValid(entity)) {
        return 0;
    }
    uint32_t created = 1;

    for (const int child : node.children) {
        if (child < 0 || static_cast<size_t>(child) >= model.nodes.size()) {
            continue;
        }
        // 子节点的局部变换原样传下去 —— 换轴和单位换算只叠在顶层那一次。
        created += AddNode(model, child, entity,
                           LocalTransformOf(model.nodes[static_cast<size_t>(child)]), options);
    }
    return created;
}

// `scene` 有意不用：内置处理器往场景里放东西走的是 ISceneSink，因为公开接口造不出
// ShapeType::Mesh（io/SceneAccess.h 说了为什么）。那个参数是给宿主自己的处理器的。
CgResult GltfImporter::Import(const char* utf8Path, IScene* /*scene*/,
                              const ImportOptions& options, IoProgressCallback progress,
                              void* userData) {
    std::vector<uint8_t> bytes;
    CgResult r = core::ReadFile(utf8Path, bytes);
    if (CgFailed(r)) {
        return r;
    }
    if (bytes.size() < 4) {
        return core::SetError(CgResult::ParseError, "'%s' is too short to be glTF", utf8Path);
    }

    // .gltf 还是 .glb 看开头的魔数，不看扩展名：一个叫 .gltf 的二进制文件读起来
    // 照样该成功，而扩展名是最容易被人改错的东西。
    const bool binary = bytes[0] == 'g' && bytes[1] == 'l' && bytes[2] == 'T' && bytes[3] == 'F';
    const std::string baseDir = core::DirectoryOf(utf8Path);

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string error;
    std::string warning;
    const bool loaded =
        binary ? loader.LoadBinaryFromMemory(&model, &error, &warning, bytes.data(),
                                             static_cast<unsigned int>(bytes.size()), baseDir)
               : loader.LoadASCIIFromString(&model, &error, &warning,
                                            reinterpret_cast<const char*>(bytes.data()),
                                            static_cast<unsigned int>(bytes.size()), baseDir);
    if (!warning.empty()) {
        CG_WARN("while reading '%s': %s", utf8Path, warning.c_str());
    }
    if (!loaded) {
        return core::SetError(CgResult::ParseError, "cannot parse '%s': %s", utf8Path,
                              error.empty() ? "malformed glTF" : error.c_str());
    }

    const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (model.scenes.empty() || static_cast<size_t>(sceneIndex) >= model.scenes.size()) {
        return core::SetError(CgResult::ParseError, "'%s' has no scene to import", utf8Path);
    }
    const tinygltf::Scene& scene = model.scenes[static_cast<size_t>(sceneIndex)];

    std::string label = "Import ";
    label.append(core::StemOf(utf8Path));
    r = sink_.Begin(label.c_str(), !options.mergeIntoScene);
    if (CgFailed(r)) {
        return r;
    }

    // 换轴（Y-up → Z-up）和单位换算都叠在顶层节点的变换上。我们自己导出的文件顶层
    // 正是那个 -90° 的根节点，两个旋转在这里抵消，往返之后一个多余的角度都不剩。
    const double scale = options.scaleToModelUnits > 0.0 ? options.scaleToModelUnits : 1.0;
    const Mat4d toZUp = ToMatrix(Transform{Vec3d{0.0, 0.0, 0.0}, ZUpRotation(),
                                           Vec3d{scale, scale, scale}});

    uint32_t created = 0;
    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        const int nodeIndex = scene.nodes[i];
        if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= model.nodes.size()) {
            continue;
        }
        if (progress && !progress(static_cast<float>(i) / static_cast<float>(scene.nodes.size()),
                                  model.nodes[static_cast<size_t>(nodeIndex)].name.c_str(),
                                  userData)) {
            sink_.End(/*commit=*/false);
            return core::SetError(CgResult::Unknown, "Import: cancelled by the host");
        }

        const Transform nodeLocal = LocalTransformOf(model.nodes[static_cast<size_t>(nodeIndex)]);
        Transform local{};
        Decompose(toZUp * ToMatrix(nodeLocal), local);
        created += AddNode(model, nodeIndex, kInvalidEntity, local, options);
    }

    if (created == 0) {
        sink_.End(/*commit=*/false);
        return core::SetError(CgResult::ParseError,
                              "'%s' parsed but produced no geometry the engine can hold",
                              utf8Path);
    }

    r = sink_.End(/*commit=*/true);
    if (CgFailed(r)) {
        return r;
    }
    CG_INFO("imported %u node(s) from '%s'", created, utf8Path);
    if (progress) {
        progress(1.0f, utf8Path, userData);
    }
    return CgResult::Ok;
}

} // namespace

IImporter* CreateGltfImporter(ISceneSink& sink) {
    return new GltfImporter(sink);
}

IExporter* CreateGltfExporter(ISceneSource& source, bool binary) {
    return new GltfExporter(source, binary);
}

} // namespace cadgeom::io
