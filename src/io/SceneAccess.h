/**
 * @file SceneAccess.h
 * @brief IO 层看场景的两面：读出去的一面和写进来的一面。
 *
 * 依赖方向是这样定的：实体表和几何内核都在 `api/`，在 `io/` 之上。所以 `io/` 只
 * 声明自己需要什么，由 `api/` 实现并把数据递下来 —— 和 `interact/` 的
 * `IPickTargetSource` 是同一手（docs/architecture.md §1）。
 *
 * 为什么不直接使唤公开的 `IScene`：导出要三角形，导入要造出 `ShapeType::Mesh`，
 * 这两件事公开接口都给不了 —— `IScene` 没有读网格的入口，`IGeometryBuilder` 也没有
 * `MakeMesh`。那不是疏漏：宿主拿到的是一个参数化的 CAD 场景，三角形是内部缓存。
 */
#pragma once

#include <cadgeom/Types.h>

#include "geom/Shape.h"

namespace cadgeom::io {

/// @brief 导出时一个实体的全貌。
struct SceneEntityView {
    EntityId id{kInvalidEntity};
    EntityId parent{kInvalidEntity};
    const char* name{""};
    Transform local{};
    /// 合成之后的世界变换。OBJ 这种没有层级也没有变换的格式只能写世界坐标。
    Mat4d world{};
    EntityStyle style{};
    bool visible{true};
    bool selected{false};

    /// 参数化定义。组节点为 null。
    const geom::ShapeDef* def{nullptr};
    /// 细分之后的网格 / 线框 / 拓扑。组节点为 null；已按需细分过。
    const geom::Shape* shape{nullptr};
};

/// @brief 导出器看到的场景。
class ISceneSource {
public:
    virtual ~ISceneSource() = default;

    virtual uint32_t GetEntityCount() const = 0;

    /// @brief 按场景自己的顺序取第 `index` 个实体（父节点一定排在子节点之前）。
    /// @return 下标越界为 false。
    /// @note **不是** const 查询：细分是惰性的，第一次问到某个形状时才会算出来。
    virtual bool GetEntity(uint32_t index, SceneEntityView& out) = 0;
};

/// @brief 导入器往场景里放东西的那一面。
///
/// 一次导入是一步撤销：`Begin` 开组，`End(true)` 收组，`End(false)` 整批回滚。
/// 半个模型留在场景里是最糟糕的失败方式 —— 用户看不出哪些是文件里的、哪些是自己
/// 画的（`IImporter::Import` 的注释就是这么承诺的）。
class ISceneSink {
public:
    virtual ~ISceneSink() = default;

    /// @brief 开始一次导入。
    /// @param utf8Label   撤销菜单里的名字，如 "Import model.glb"。
    /// @param replaceScene true 时先清空场景。
    /// @warning 清空会连撤销栈一起清掉（`IScene::Clear` 的语义），所以
    ///          `ImportOptions::mergeIntoScene = false` 的那次导入本身撤不回来。
    virtual CgResult Begin(const char* utf8Label, bool replaceScene) = 0;

    /// @brief 加一个没有几何的组节点。
    virtual EntityId AddGroup(const char* utf8Name, EntityId parent, const Transform& local) = 0;

    /// @brief 加一个带几何的实体。
    /// @param def 形状定义。参数化的（圆、拉伸……）来自 glTF 的 `extras`，网格的
    ///            来自文件里的三角形。
    /// @return 失败时为 kInvalidEntity，原因在错误槽里。
    virtual EntityId AddShape(const char* utf8Name, EntityId parent, const Transform& local,
                              const EntityStyle& style, geom::ShapeDef def) = 0;

    /// @brief 收尾。`commit` 为 false 时把这一批全部撤销并丢掉，撤销栈上不留痕迹。
    virtual CgResult End(bool commit) = 0;
};

} // namespace cadgeom::io
