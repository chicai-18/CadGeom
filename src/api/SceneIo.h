#pragma once

#include "core/ObjectTracker.h"
#include "io/SceneAccess.h"

namespace cadgeom::api {

class SceneImpl;

/// @brief 把场景递给 IO 层的那座桥。
///
/// 方向和 `SceneImpl` 实现 `IPickTargetSource` 是同一手：数据（实体表、几何内核、
/// 撤销栈）都在 `api/`，而 `io/` 在它下面，所以由这一层实现 `io/` 声明的接口，
/// 把需要的东西递下去（CLAUDE.md「Layering」）。
///
/// 单独一个类而不是再往 `SceneImpl` 上挂两个基类：`SceneImpl` 的基类列表里
/// `IScene` 必须留在第一位，而它已经挂了三个接口了 —— 桥自己一个对象，谁也不用
/// 数着位置过日子。
class SceneIoBridge final : public io::ISceneSource, public io::ISceneSink {
public:
    explicit SceneIoBridge(SceneImpl& scene);
    ~SceneIoBridge() override;

    // -- io::ISceneSource ---------------------------------------------------

    uint32_t GetEntityCount() const override;
    bool GetEntity(uint32_t index, io::SceneEntityView& out) override;

    // -- io::ISceneSink -----------------------------------------------------

    CgResult Begin(const char* utf8Label, bool replaceScene) override;
    EntityId AddGroup(const char* utf8Name, EntityId parent, const Transform& local) override;
    EntityId AddShape(const char* utf8Name, EntityId parent, const Transform& local,
                      const EntityStyle& style, geom::ShapeDef def) override;
    CgResult End(bool commit) override;

private:
    core::ObjectTracker tracker_;
    SceneImpl& scene_;

    /// 导入是否开着。防的是处理器漏掉 End()：那会让撤销组一直开着，之后每一次编辑
    /// 都被吸进这一组里，用户按撤销时整个会话一起消失。
    bool importing_{false};
};

} // namespace cadgeom::api
