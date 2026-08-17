/**
 * @file HostCommands.h
 * @brief 宿主侧实现的 ICommand —— 引擎没有替我们做成可撤销的那几件事。
 *
 * 引擎的规矩是「所有场景修改走 ICommand」，但 IEntity::SetName / SetStyle /
 * SetVisible 是直接写的：它们改的是显示属性，不是几何，引擎把要不要进撤销栈的
 * 决定权留给了宿主。一个真正的 CAD 应用当然要，所以这里补上。
 *
 * 顺带证明了边界的另一个方向：对象在**宿主**里 new，引擎执行它、持有它，最后调
 * 它的 Release()（也在宿主里）—— 谁分配谁释放，一次跨堆都没有
 * （docs/architecture.md §2.2 第 2 条）。
 */
#ifndef CADGEOM_QT_VIEWER_HOSTCOMMANDS_H
#define CADGEOM_QT_VIEWER_HOSTCOMMANDS_H

#include <cadgeom/CadGeom.h>

#include <functional>
#include <string>
#include <vector>

/// @brief 改名，可撤销。
class RenameCommand final : public cadgeom::ICommand {
public:
    RenameCommand(cadgeom::EntityId target, std::string newName);

    void Release() override { delete this; }
    cadgeom::CgResult Execute(cadgeom::IScene* scene) override;
    cadgeom::CgResult Undo(cadgeom::IScene* scene) override;
    const char* GetName() const override { return "Rename"; }

private:
    ~RenameCommand() override = default;

    cadgeom::EntityId target_;
    std::string newName_;
    std::string previousName_;
};

/// @brief 批量改样式，可撤销。
/// @note 改什么由一个函数说了算 —— 颜色、线宽、线型、可见性都是 EntityStyle 的
///       字段，为每一个写一个命令类没有意义。撤销时整份旧样式原样写回。
class StyleCommand final : public cadgeom::ICommand {
public:
    using Editor = std::function<void(cadgeom::EntityStyle&)>;

    /// @param targets 受影响的实体；空集合会让 Execute 返回 InvalidArgument，
    ///                命令因此不会进栈。
    /// @param editor  就地修改一份样式副本。
    /// @param name    菜单上「撤销 XXX」里的那个 XXX，必须是 UTF-8。
    StyleCommand(std::vector<cadgeom::EntityId> targets, Editor editor, std::string name);

    void Release() override { delete this; }
    cadgeom::CgResult Execute(cadgeom::IScene* scene) override;
    cadgeom::CgResult Undo(cadgeom::IScene* scene) override;
    const char* GetName() const override { return name_.c_str(); }

private:
    ~StyleCommand() override = default;

    std::vector<cadgeom::EntityId> targets_;
    Editor editor_;
    std::string name_;
    /// 与 targets_ 一一对应的旧样式，每次 Execute 重新采集（重做也要有得还原）。
    std::vector<cadgeom::EntityStyle> previous_;
};

/// @brief 数值改一个实体的局部变换，可撤销。
/// @note `IEntity::SetLocalTransform` 同样是直接写的。Move / Rotate / Scale 三个
///       Gizmo 工具走的是它们自己的 ICommand，宿主要提供「输入精确数值」这条路，
///       就得自己写一个 —— 引擎不会替宿主决定它的对话框长什么样。
class TransformCommand final : public cadgeom::ICommand {
public:
    TransformCommand(cadgeom::EntityId target, const cadgeom::Transform& next);

    void Release() override { delete this; }
    cadgeom::CgResult Execute(cadgeom::IScene* scene) override;
    cadgeom::CgResult Undo(cadgeom::IScene* scene) override;
    const char* GetName() const override { return "Transform"; }

private:
    ~TransformCommand() override = default;

    cadgeom::EntityId target_;
    cadgeom::Transform next_{};
    cadgeom::Transform previous_{};
};

/// @brief 建一个组节点，把选中的实体挂进去，可撤销。
/// @note IScene::CreateGroup 自己不入栈（它是建节点，不是改几何），SetParent 入栈
///       —— 但内层命令在外层命令执行期间只执行不记录（CLAUDE.md「嵌套 Push」那
///       条），所以整件事必须由这一个命令负责撤干净：先把成员放回原来的父节点，
///       再删掉空组。
class GroupCommand final : public cadgeom::ICommand {
public:
    GroupCommand(std::vector<cadgeom::EntityId> members, std::string groupName);

    void Release() override { delete this; }
    cadgeom::CgResult Execute(cadgeom::IScene* scene) override;
    cadgeom::CgResult Undo(cadgeom::IScene* scene) override;
    const char* GetName() const override { return "Group"; }

    /// @return 最近一次 Execute 建出来的组；失败或尚未执行时是 kInvalidEntity。
    cadgeom::EntityId group() const { return group_; }

private:
    ~GroupCommand() override = default;

    std::vector<cadgeom::EntityId> members_;
    std::vector<cadgeom::EntityId> previousParents_;
    std::string groupName_;
    cadgeom::EntityId group_{cadgeom::kInvalidEntity};
};

#endif // CADGEOM_QT_VIEWER_HOSTCOMMANDS_H
