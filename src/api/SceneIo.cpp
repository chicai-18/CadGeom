#include "api/SceneIo.h"

#include <cadgeom/ISelection.h>

#include "api/Commands.h"
#include "api/SceneImpl.h"
#include "core/Error.h"
#include "core/Log.h"

#include <utility>

namespace cadgeom::api {

SceneIoBridge::SceneIoBridge(SceneImpl& scene) : scene_(scene) {}

SceneIoBridge::~SceneIoBridge() {
    if (importing_) {
        // 只可能是某个处理器中途返回了却没收尾。丢掉这一组，别把撤销栈留在开着的
        // 状态上跟着引擎一起下葬。
        CG_WARN("an import was still open at shutdown; discarding it");
        End(/*commit=*/false);
    }
}

// ---------------------------------------------------------------------------
// io::ISceneSource
// ---------------------------------------------------------------------------

uint32_t SceneIoBridge::GetEntityCount() const {
    return scene_.GetEntityCount();
}

bool SceneIoBridge::GetEntity(uint32_t index, io::SceneEntityView& out) {
    const EntityId id = scene_.GetEntityAt(index);
    const EntityImpl* entity = scene_.FindEntity(id);
    if (!entity) {
        return false;
    }

    out = io::SceneEntityView{};
    out.id = id;
    out.parent = entity->GetParent();
    out.name = entity->GetName();
    entity->GetLocalTransform(out.local);
    entity->GetWorldTransform(out.world);
    entity->GetStyle(out.style);
    out.visible = entity->IsVisible();
    out.selected = scene_.GetSelection()->Contains(id);

    const ShapeId shape = entity->GetShape();
    if (IsValid(shape)) {
        // Resolve 按需细分。导出要的正是这个结果 —— 没有它，一个刚建出来还没画过
        // 一帧的场景导出去会是空的。
        out.shape = scene_.Kernel().Resolve(shape);
        if (out.shape) {
            out.def = &out.shape->def;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// io::ISceneSink
// ---------------------------------------------------------------------------

CgResult SceneIoBridge::Begin(const char* utf8Label, bool replaceScene) {
    if (importing_) {
        return core::SetError(CgResult::InvalidState,
                              "Import: another import is already in progress");
    }

    if (replaceScene) {
        // 清空连撤销栈一起清（IScene::Clear 的语义），所以这一次导入本身撤不回来。
        // 这是 mergeIntoScene = false 明码标价的代价，io/SceneAccess.h 里写着。
        scene_.Clear();
    }
    scene_.GetCommandStack()->BeginGroup(utf8Label);
    importing_ = true;
    return CgResult::Ok;
}

EntityId SceneIoBridge::AddGroup(const char* utf8Name, EntityId parent, const Transform& local) {
    if (!importing_) {
        core::SetError(CgResult::InvalidState, "AddGroup: no import is in progress");
        return kInvalidEntity;
    }

    auto* command = new CreateGroupCommand(scene_, utf8Name, parent, local);
    // Push 执行并在失败时释放，所以只有成功那条路上读 `command` 才是安全的。
    if (CgFailed(scene_.GetCommandStack()->Push(command))) {
        return kInvalidEntity;
    }
    return command->Entity();
}

EntityId SceneIoBridge::AddShape(const char* utf8Name, EntityId parent, const Transform& local,
                                 const EntityStyle& style, geom::ShapeDef def) {
    if (!importing_) {
        core::SetError(CgResult::InvalidState, "AddShape: no import is in progress");
        return kInvalidEntity;
    }

    auto* command = new CreateShapeCommand(scene_, std::move(def), utf8Name, parent, local, style);
    if (CgFailed(scene_.GetCommandStack()->Push(command))) {
        return kInvalidEntity;
    }
    return command->Entity();
}

CgResult SceneIoBridge::End(bool commit) {
    if (!importing_) {
        return core::SetError(CgResult::InvalidState, "End: no import is in progress");
    }
    importing_ = false;

    CommandStackImpl& commands = scene_.Commands();
    if (commit) {
        // 整个文件是一步撤销。一个模型进来是一次操作，不是三百次。
        commands.EndGroup();
    } else {
        // 读到一半失败：建出来的全部撤销、全部丢掉。半个模型留在场景里是最糟糕的
        // 失败方式 —— 用户看不出哪些是文件里的、哪些是自己画的。
        commands.AbortGroup();
    }
    return CgResult::Ok;
}

} // namespace cadgeom::api
