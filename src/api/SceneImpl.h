#pragma once

#include <cadgeom/IScene.h>

#include "api/CommandStackImpl.h"
#include "api/EntityImpl.h"
#include "api/GeometryBuilderImpl.h"
#include "api/SelectionImpl.h"
#include "core/ObjectTracker.h"
#include "geom/Kernel.h"
#include "interact/Picker.h"
#include "interact/Snap.h"
#include "scene/Bvh.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace cadgeom::api {

/// 场景同时是拾取和吸附的数据源：BVH 只索引 id 和包围盒，实体表和几何内核都在
/// 这里，所以由这一层实现那两个接口把数据递下去（interact/Picker.h 的说明）。
///
/// @warning `IScene` 必须留在基类列表的第一位。宿主拿到的是 `IScene*`，它指向
///          的是主基类子对象，vtable 布局和单继承的 IScene 一模一样 —— ABI 纪律
///          管的是接口本身单继承（§2.2 第 5 条），实现类挂几个内部接口不影响
///          宿主，前提是别把 IScene 从第一位挪走。
class SceneImpl final : public IScene,
                        public interact::IPickTargetSource,
                        public interact::ISnapSource {
public:
    SceneImpl();
    ~SceneImpl() override;

    // -- IScene -------------------------------------------------------------

    IGeometryBuilder* GetGeometryBuilder() override;

    EntityId CreateGroup(const char* utf8Name, EntityId parent) override;
    CgResult DestroyEntity(EntityId entity) override;
    CgResult DestroyEntities(CgSpan<const EntityId> entities) override;
    void Clear() override;

    bool Exists(EntityId entity) const override;
    IEntity* GetEntity(EntityId entity) override;
    const IEntity* GetEntity(EntityId entity) const override;

    uint32_t GetEntityCount() const override;
    EntityId GetEntityAt(uint32_t index) const override;
    uint32_t GetRootCount() const override;
    EntityId GetRootAt(uint32_t index) const override;

    CgResult SetParent(EntityId entity, EntityId parent) override;

    bool GetBounds(Aabb& out) const override;
    bool Raycast(const Ray& ray, uint32_t pickFilter, PickResult& out) const override;

    ISelection* GetSelection() override;
    const ISelection* GetSelection() const override;
    ICommandStack* GetCommandStack() override;

    uint64_t GetRevision() const override;

    // -- interact::IPickTargetSource ----------------------------------------

    bool GetPickTarget(EntityId entity, interact::PickTarget& out) const override;

    // -- interact::ISnapSource ----------------------------------------------

    void CollectSnapCandidates(const Vec3d& center, double radius,
                               const interact::SnapQuery& query,
                               std::vector<interact::SnapCandidate>& out) const override;

    // -- Engine-side access -------------------------------------------------

    EntityImpl* FindEntity(EntityId id);
    const EntityImpl* FindEntity(EntityId id) const;

    /// @brief 带显式容差的拾取。IScene::Raycast 没有相机可问，只能用一个由场景
    ///        尺寸推出来的容差；视口知道每个像素有多大，走的是这条路。
    bool RaycastWithTolerance(const Ray& ray, uint32_t pickFilter,
                              const interact::PickTolerance& tolerance, PickResult& out) const;

    /// @brief 实体自己和它每一级祖先都可见时才为 true。
    bool IsEffectivelyVisible(EntityId entity) const;

    /// The geometry kernel every shape in this scene lives in.
    geom::IGeometryKernel& Kernel() { return *kernel_; }
    const geom::IGeometryKernel& Kernel() const { return *kernel_; }

    /// @brief 引擎自己人看到的撤销栈，带着 `ICommandStack` 里没有的那几件事
    ///        （`AbortGroup`）。宿主拿到的还是那个公开接口。
    CommandStackImpl& Commands() { return commands_; }

    /// Bump whenever anything a renderer or a host would observe changes.
    void BumpRevision() {
        ++revision_;
        ++geometryRevision_;
    }

    /// @brief 只影响「怎么画」而不影响「画什么」的改动 —— 选中、悬停、子元素。
    ///
    /// 分成两个计数器是因为悬停高亮跟着鼠标每一次移动变。它必须让渲染快照重建
    /// （颜色变了），但顶点一个都没动：混用一个计数器的话，鼠标划过几何就会
    /// 触发一次全量显存重传和一次 BVH 重建。
    void BumpDisplayRevision() { ++revision_; }

    /// @brief 只在几何、层级或可见性变化时前进。BVH 和 GPU 缓冲跟的是它。
    uint64_t GetGeometryRevision() const { return geometryRevision_; }

    void MarkTransformDirty(EntityId entity);

    /// Adds an entity without going through the undo stack — the primitive the
    /// engine's own commands are built from.
    ///
    /// `preferredId` is how undo puts an entity back under the id it had:
    /// anything else holding that id (a later command, a host's bookkeeping)
    /// would otherwise be left pointing at nothing. Pass kInvalidEntity to
    /// allocate a fresh one.
    EntityId CreateEntityInternal(const char* utf8Name, EntityId parent, EntityId preferredId);

    /// Destroys an entity and its descendants without touching the undo stack.
    /// Any shape still attached goes back to the kernel with it.
    CgResult DestroyEntityInternal(EntityId entity);

    /// For handing `this` to ICommand callbacks without a cast at every site.
    IScene* AsInterface() { return this; }

private:
    void DestroyRecursive(EntityId id);
    bool WouldCreateCycle(EntityId entity, EntityId newParent) const;

    /// 让 BVH 追上当前的 revision。const 是因为拾取本身是查询：重建的是一个缓存，
    /// 不是场景的状态。
    void RefreshBvh() const;

    /// 细分是惰性缓存，不改变场景「是什么」，所以 const 查询里也可以要求它。
    /// unique_ptr 的 operator* 本来就不传递 const，这个包装只是把这件事说明白。
    geom::IGeometryKernel& LazyKernel() const { return *kernel_; }

    core::ObjectTracker tracker_;

    /// Declared first so it outlives every entity below it: an entity hands its
    /// shape back on destruction, and doing that to a dead kernel would be the
    /// last thing this scene ever did.
    std::unique_ptr<geom::IGeometryKernel> kernel_;

    std::unordered_map<uint64_t, std::unique_ptr<EntityImpl>> entities_;
    /// Insertion order for stable index-based enumeration across the boundary.
    std::vector<EntityId> order_;
    std::vector<EntityId> roots_;

    /// Monotonic and never recycled: a stale id must fail a lookup, not
    /// silently resolve to a different entity.
    uint64_t nextEntityId_{1};
    uint64_t revision_{1};
    uint64_t geometryRevision_{1};

    SelectionImpl selection_;
    CommandStackImpl commands_;
    GeometryBuilderImpl builder_;

    /// 拾取和吸附的空间索引，脏了整棵重建。跟着 revision 走：任何会挪动几何的
    /// 改动都会把它推进一格，这也正是索引失效的时刻。
    mutable scene::Bvh bvh_;
    mutable uint64_t bvhRevision_{0};
    mutable bool bvhValid_{false};
};

} // namespace cadgeom::api
