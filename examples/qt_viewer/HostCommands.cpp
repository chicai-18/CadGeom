/**
 * @file HostCommands.cpp
 * @brief HostCommands.h 的实现。
 */
#include "HostCommands.h"

#include <utility>

using namespace cadgeom;

// ---------------------------------------------------------------------------
// RenameCommand
// ---------------------------------------------------------------------------

RenameCommand::RenameCommand(EntityId target, std::string newName)
    : target_(target), newName_(std::move(newName)) {}

CgResult RenameCommand::Execute(IScene* scene) {
    IEntity* e = scene ? scene->GetEntity(target_) : nullptr;
    if (!e) {
        return CgResult::InvalidHandle;
    }
    previousName_ = e->GetName();
    e->SetName(newName_.c_str());
    return CgResult::Ok;
}

CgResult RenameCommand::Undo(IScene* scene) {
    IEntity* e = scene ? scene->GetEntity(target_) : nullptr;
    if (!e) {
        return CgResult::InvalidHandle;
    }
    e->SetName(previousName_.c_str());
    return CgResult::Ok;
}

// ---------------------------------------------------------------------------
// StyleCommand
// ---------------------------------------------------------------------------

StyleCommand::StyleCommand(std::vector<EntityId> targets, Editor editor, std::string name)
    : targets_(std::move(targets)), editor_(std::move(editor)), name_(std::move(name)) {}

CgResult StyleCommand::Execute(IScene* scene) {
    if (!scene || targets_.empty() || !editor_) {
        return CgResult::InvalidArgument;
    }
    previous_.clear();
    previous_.reserve(targets_.size());

    for (const EntityId id : targets_) {
        IEntity* e = scene->GetEntity(id);
        if (!e) {
            // 撤销时会按同样的顺序走一遍，占位保证下标对得上。
            previous_.push_back(EntityStyle{});
            continue;
        }
        EntityStyle style{};
        e->GetStyle(style);
        previous_.push_back(style);
        editor_(style);
        e->SetStyle(style);
    }
    return CgResult::Ok;
}

CgResult StyleCommand::Undo(IScene* scene) {
    if (!scene) {
        return CgResult::InvalidArgument;
    }
    for (size_t i = 0; i < targets_.size() && i < previous_.size(); ++i) {
        if (IEntity* e = scene->GetEntity(targets_[i])) {
            e->SetStyle(previous_[i]);
        }
    }
    return CgResult::Ok;
}

// ---------------------------------------------------------------------------
// TransformCommand
// ---------------------------------------------------------------------------

TransformCommand::TransformCommand(EntityId target, const Transform& next)
    : target_(target), next_(next) {}

CgResult TransformCommand::Execute(IScene* scene) {
    IEntity* e = scene ? scene->GetEntity(target_) : nullptr;
    if (!e) {
        return CgResult::InvalidHandle;
    }
    e->GetLocalTransform(previous_);
    e->SetLocalTransform(next_);
    return CgResult::Ok;
}

CgResult TransformCommand::Undo(IScene* scene) {
    IEntity* e = scene ? scene->GetEntity(target_) : nullptr;
    if (!e) {
        return CgResult::InvalidHandle;
    }
    e->SetLocalTransform(previous_);
    return CgResult::Ok;
}

// ---------------------------------------------------------------------------
// GroupCommand
// ---------------------------------------------------------------------------

GroupCommand::GroupCommand(std::vector<EntityId> members, std::string groupName)
    : members_(std::move(members)), groupName_(std::move(groupName)) {}

CgResult GroupCommand::Execute(IScene* scene) {
    if (!scene || members_.empty()) {
        return CgResult::InvalidArgument;
    }

    group_ = scene->CreateGroup(groupName_.c_str(), kInvalidEntity);
    if (!IsValid(group_)) {
        return CgResult::Unknown;
    }

    previousParents_.clear();
    previousParents_.reserve(members_.size());
    for (const EntityId id : members_) {
        IEntity* e = scene->GetEntity(id);
        previousParents_.push_back(e ? e->GetParent() : kInvalidEntity);
        if (e) {
            // SetParent 保世界变换不变，所以入组之后模型一动不动 —— 这是分组该有
            // 的样子。
            scene->SetParent(id, group_);
        }
    }
    return CgResult::Ok;
}

CgResult GroupCommand::Undo(IScene* scene) {
    if (!scene) {
        return CgResult::InvalidArgument;
    }
    // 先把成员挪出去再删组：DestroyEntity 是连子孙一起删的，顺序反了就把整组模型
    // 一起撤没了。
    for (size_t i = 0; i < members_.size() && i < previousParents_.size(); ++i) {
        if (scene->Exists(members_[i])) {
            scene->SetParent(members_[i], previousParents_[i]);
        }
    }
    if (IsValid(group_) && scene->Exists(group_)) {
        scene->DestroyEntity(group_);
    }
    group_ = kInvalidEntity;
    return CgResult::Ok;
}
