# CadGeom — CAD 三维引擎架构方案

> 状态：设计稿 v1（2026-08-16）
> 目标：Vulkan 渲染的可嵌入 CAD 三维引擎，以动态库 + 稳定接口形式对外提供。

---

## 0. 已确定的四个架构决策

| 决策点 | 选择 | 影响 |
|---|---|---|
| 几何内核 | 自研轻量内核 + `IGeometryKernel` 抽象层 | 无重依赖、编译快；后期可换 OCCT 后端 |
| 对外接口 | C++ 纯虚接口类（COM 风格） | 用起来自然；**必须遵守 ABI 纪律**（见 §2.2） |
| 窗口/表面 | 抽象 `ISurface`：GLFW 后端 + 原生句柄后端 | 既能独立跑 demo，又能嵌入 Qt/MFC |
| 依赖管理 | git submodule + 手动安装 Vulkan SDK | 离线可控、版本确定 |

## 0.1 我替你定的默认值（有异议随时改）

| 项 | 默认 | 理由 |
|---|---|---|
| 坐标系 | **Z-up，右手系** | CAD 行业惯例（SolidWorks/AutoCAD/Fusion 均 Z-up）。glTF 是 Y-up，在 IO 层做轴转换 |
| 数值精度 | 内核 `double`，GPU `float` | 大坐标模型必须；上传前做 camera-relative 平移消抖动 |
| Vulkan 版本 | **1.3**，启用 dynamic rendering + sync2 | 省掉 RenderPass/Framebuffer 大量样板代码 |
| 目标平台 | Windows 优先，代码不写死平台 | ISurface 抽象已经隔离掉平台差异 |
| 相机 | 默认**正交**，可切透视 | CAD 制图以正交为主 |
| 拾取 | **CPU 射线 + BVH**（GPU ID buffer 作为可选加速后端） | 拾取要区分点/边/面并支持吸附，GPU ID 给不了这些语义 |
| 线条渲染 | **屏幕空间 quad 扩展**（instanced） | `wideLines` feature 不保证支持、宽度不可靠，且无法做虚线/线型 |
| UI | 引擎侧只有笔画字体画的 HUD；真正的面板归宿主 | 核心库保持无 UI 依赖。M6 没有引入 ImGui —— 理由见 §7.1 |

---

## 1. 分层结构

```
┌──────────────────────────────────────────────────────────────┐
│  宿主应用 (examples/glfw_viewer, 未来的 Qt 客户端)             │
└───────────────────────────┬──────────────────────────────────┘
                            │ include/cadgeom/*.h  ← 唯一对外契约
┌───────────────────────────┴──────────────────────────────────┐
│  api/        接口实现 (EngineImpl / SceneImpl / ViewportImpl) │
├──────────────────────────────────────────────────────────────┤
│  interact/   Camera · Tool 状态机 · Gizmo · Picker · Snap      │
├─────────────────┬───────────────────┬────────────────────────┤
│  scene/         │  io/              │  render/               │
│  Entity 存储    │  glTF / OBJ       │  Vulkan RHI            │
│  BVH · 选择集   │  Importer 注册表  │  Pass 组织 · ISurface  │
│  CommandStack   │                   │                        │
├─────────────────┴───────────────────┴────────────────────────┤
│  geom/       IGeometryKernel · 参数化曲线 · Extrude · 网格化  │
├──────────────────────────────────────────────────────────────┤
│  core/       数学(double) · 内存 · 日志 · 事件 · ID · Result  │
└──────────────────────────────────────────────────────────────┘
```

**依赖方向严格向下**，禁止反向：`geom/` 不知道 `render/` 存在，`render/` 不知道 `interact/` 存在。
这条规则保证几何内核可以被单元测试独立验证（不需要起 Vulkan）。

## 1.1 目录布局

```
CadGeom/
├─ CMakeLists.txt
├─ cmake/                     CadGeomShaders.cmake, 导出宏, 安装规则
├─ include/cadgeom/           ★ 公共头，唯一对外契约
│   ├─ CadGeom.h              总入口（只包含这一个就够）
│   ├─ Types.h                POD 结构：Vec3/Mat4/Color/CgSpan/CgResult
│   ├─ IEngine.h  IScene.h  IEntity.h  IViewport.h
│   ├─ IGeometryBuilder.h     点/线/圆/矩形/拉伸的创建接口
│   ├─ ITool.h  ICamera.h  ISelection.h  ICommandStack.h
│   ├─ IImportExport.h
│   └─ IEngineExt.h           ★ ICadEngine2：扩展槽里的 M6 能力（§7.1）
├─ src/
│   ├─ core/                  Math.h(double) · Log · Error · File · Png · Units(显示单位)
│   ├─ geom/
│   │   ├─ Kernel.h/.cpp      IGeometryKernel 实现（SimpleKernel）
│   │   ├─ Curve.h/.cpp       Line/Circle/Rectangle/Polyline 参数化定义
│   │   ├─ Profile.h/.cpp     闭合轮廓 + 平面三角化
│   │   ├─ Extrude.h/.cpp     轮廓 → Solid（含拓扑：face/edge/vertex）
│   │   └─ Tessellate.h/.cpp  精确几何 → MeshData / PolyLineData
│   ├─ scene/
│   │   ├─ EntityStore.h/.cpp SoA 存储 + 脏标记
│   │   ├─ Transform.h/.cpp   层级变换传播
│   │   ├─ Bvh.h/.cpp         拾取 + 视锥剔除
│   │   ├─ Selection.h/.cpp
│   │   └─ CommandStack.h/.cpp
│   ├─ render/
│   │   ├─ vk/                Context · Swapchain · Allocator(VMA) · DeleteQueue
│   │   │                     Buffer · Image · Pipeline · DescriptorAllocator
│   │   ├─ Surface.h/.cpp     ISurface: GlfwSurface / NativeSurface(HWND)
│   │   ├─ Renderer.h/.cpp    帧循环、frames-in-flight、上传队列
│   │   ├─ GpuScene.h/.cpp    Entity → RenderProxy，大 buffer 子分配
│   │   └─ pass/              Grid · Mesh · Edge · Line · Point · Overlay
│   ├─ interact/
│   │   ├─ OrbitCamera.h/.cpp
│   │   ├─ WorkPlane.h/.cpp   ★ 2D 图元创建的落点平面
│   │   ├─ Picker.h/.cpp      射线 → BVH → 点/边/面优先级
│   │   ├─ Snap.h/.cpp        端点/中点/圆心/交点/网格
│   │   ├─ Gizmo.h/.cpp       平移箭头 + 旋转圆环 + 缩放标尺
│   │   ├─ TextStroke.h/.cpp  ★ 笔画字体：文本 → 折线，叠加层的文字靠它
│   │   └─ tools/             Select/Point/Line/Circle/Rect/Extrude/Move/Rotate/Scale/Measure
│   ├─ io/
│   │   ├─ Registry.h/.cpp    格式 → Importer/Exporter 工厂
│   │   ├─ SceneAccess.h      ★ ISceneSource / ISceneSink，io/ 看场景的两面
│   │   ├─ Axis.h             Z-up ↔ Y-up，全引擎只此一处
│   │   ├─ ObjIO.cpp          tinyobjloader + 自写 writer
│   │   ├─ GltfIO.cpp         tinygltf（.gltf / .glb）+ extras 参数化编解码
│   │   └─ *Common.h/Tiny*Impl.cpp  第三方库的开关与唯一实现单元
│   └─ api/                   *Impl.cpp + 工厂导出函数
├─ shaders/                   GLSL → SPIR-V → 嵌入 DLL
├─ external/                  submodules
├─ examples/glfw_viewer/      独立可跑的 CAD 查看/建模 demo
├─ tests/                     几何内核 + IO + 拾取的单元测试（不依赖 Vulkan）
└─ docs/
```

---

## 2. 对外接口设计（COM 风格）

### 2.1 形态

```cpp
// include/cadgeom/IEngine.h
namespace cadgeom {

class CADGEOM_API ICadEngine {
public:
    virtual void        Release() = 0;              // 不要 delete，DLL 内部释放
    virtual IScene*     GetScene() = 0;
    virtual IViewport*  CreateViewport(const ViewportDesc& desc) = 0;
    virtual IToolManager* GetToolManager() = 0;
    virtual IIoRegistry*  GetIoRegistry() = 0;
    virtual void        Tick(double deltaSeconds) = 0;
protected:
    virtual ~ICadEngine() = default;                // 禁止外部 delete
};

extern "C" CADGEOM_API ICadEngine* CadGeom_CreateEngine(const EngineDesc&);
extern "C" CADGEOM_API uint32_t    CadGeom_GetApiVersion();   // 宿主启动时校验
}
```

### 2.2 ABI 纪律（**这几条必须写进 CLAUDE.md，违反即埋雷**）

纯虚接口能跨 DLL 工作，靠的是 MSVC/Itanium ABI 对「单继承、无虚基类的纯虚类」vtable 布局的事实稳定性。要维持它：

1. **接口中不出现任何 STL 类型**。不传 `std::string` / `std::vector` / `std::shared_ptr`。
   - 字符串 → `const char*`（UTF-8）
   - 数组入参 → `CgSpan<const T>{ ptr, count }`（POD，仅指针+长度）
   - 数组出参 → 两段式：先查数量再填缓冲，或返回 `IArray*` 接口
2. **所有对象由 DLL 分配、`Release()` 释放**。跨 DLL 边界 `new`/`delete` 会踩不同堆。
3. **接口只追加不修改**。已发布接口的虚函数顺序/签名冻结；要扩展就新增 `IScene2 : IScene`，或加独立接口 + `QueryInterface` 风格的 `void* GetExtension(InterfaceId)`。
4. **不允许异常跨边界**。所有可能失败的调用返回 `CgResult`（枚举），细节走 `GetLastErrorMessage()`。
5. **接口类不带数据成员、不带非虚函数、单继承**。
6. **不在接口里用内联函数**（内联会把宿主编译期的实现固化进宿主）。
7. **提供 `CadGeom_GetApiVersion()`**，宿主启动时比对头文件里的 `CADGEOM_API_VERSION`，不匹配直接报错，比诡异崩溃强。

> 便利性补偿：额外提供 `include/cadgeom/CadGeomRAII.h`（header-only，非必须），把裸接口包成 `EnginePtr`/`ScopedEntity` 之类的 RAII 句柄，宿主可选用。这层完全在宿主侧编译，不影响 ABI。

### 2.3 典型调用序列

```cpp
auto* engine = CadGeom_CreateEngine({ .enableValidation = true });

SurfaceDesc sd{ SurfaceKind::NativeWin32, hwnd, width, height };
auto* vp = engine->CreateViewport({ sd, ProjectionMode::Orthographic });

// 交互式创建圆：切工具，喂鼠标事件，工具内部完成状态机
engine->GetToolManager()->Activate(ToolId::Circle);
vp->OnMouseEvent({ MouseButton::Left, MouseAction::Down, x, y, mods });
...
// 拉伸选中的圆
engine->GetToolManager()->Activate(ToolId::Extrude);

engine->GetIoRegistry()->Export("out.glb", ExportOptions{ Format::Gltf });
engine->Release();
```

---

## 3. 几何内核（`geom/`）

### 3.1 核心思想：参数化定义与网格并存

CAD 引擎和普通渲染引擎的根本区别：**改半径要重新生成几何，而不是编辑三角形**。

```cpp
struct Shape {
    ShapeId     id;
    ShapeType   type;        // Point/Line/Circle/Rectangle/Polyline/Solid
    Transform   xform;       // double 精度
    ParamData   params;      // ★ 参数化定义（圆心+半径+法向 / 矩形宽高 / 拉伸源+方向+距离）
    Topology    topo;        // Solid 的 face/edge/vertex 索引（为拾取与后期布尔留口）
    MeshData    mesh;        // ★ 缓存的三角网格（脏了就重新 Tessellate）
    PolyLineData wire;       // 缓存的线框
};
```
- `params` 是 SSOT（single source of truth），`mesh`/`wire` 是派生缓存。
- 改任意参数 → 打脏标记 → 下一帧重新 `Tessellate` → 通知 `GpuScene` 重传。

### 3.2 内核接口

```cpp
class IGeometryKernel {
public:
    virtual ShapeId MakePoint(const Vec3d&) = 0;
    virtual ShapeId MakeLine(const Vec3d& a, const Vec3d& b) = 0;
    virtual ShapeId MakeCircle(const Plane&, double radius) = 0;
    virtual ShapeId MakeRectangle(const Plane&, double w, double h) = 0;
    virtual ShapeId MakePolyline(CgSpan<const Vec3d>, bool closed) = 0;

    virtual ShapeId Extrude(ShapeId profile, const Vec3d& dir, double dist,
                            const ExtrudeOptions&) = 0;   // 可选拔模/双向

    virtual bool    UpdateParams(ShapeId, const ParamData&) = 0;   // 参数化编辑
    virtual void    Tessellate(ShapeId, const TessParams&, MeshData&, PolyLineData&) = 0;
    virtual Aabb    GetBounds(ShapeId) const = 0;
    virtual void    Destroy(ShapeId) = 0;
};
```
第一版实现 `SimpleKernel`；将来接 OCCT 时新增 `OcctKernel` 实现同一接口，`EngineDesc::kernelType` 切换。

### 3.3 拉伸算法（v1）

```
闭合轮廓 Profile (圆 / 矩形 / 闭合多段线)
   │  1. 按弦高容差离散成平面多边形（圆按 tol 自适应分段数）
   │  2. 平面三角化：凸多边形扇形剖分；一般多边形用 earcut（v1.1）
   ├─→ 底面三角形（法线 = -dir）
   ├─→ 顶面三角形（顶点 += dir*dist，法线 = +dir，绕序翻转）
   └─→ 侧面：相邻两点 + 各自偏移 → 两个三角形；法线按边方向叉乘
   3. 生成 Topology：
        faces  = { bottom, top, side[0..n-1] }
        edges  = { 底环、顶环、竖直棱 }
        verts  = { 底环、顶环 }
      （圆柱侧面标记为 smooth face，法线做平滑；矩形侧面为 sharp face）
```
拓扑信息不是可选项——拾取要能选中「某个面」、Gizmo 要能拖动「某条边」、后期布尔运算和 STEP 导出全依赖它。

M4 的落地细节：

- **三角化直接上耳切法**，没走「v1 先扇形剖分」那一步。扇形只对凸轮廓成立，而一块带缺口的板子是最普通不过的零件；耳切法多出来的那几十行换掉的是一个会被立刻撞上的限制。
- **实体自带一份轮廓定义的拷贝**（`geom::ShapeDef::profile`）。`ExtrudeParams::profile` 里的 `ShapeId` 只是出处：轮廓那个实体随时可能被删掉，而「实体拥有自己的形状」意味着形状会跟着一起走。带一份拷贝，实体才是自足的 —— 轮廓没了照样能改高度重新扫掠。
- **轮廓每次细分都重新离散**，不缓存点表。同一个圆当轮廓拉伸和当曲线画出来，分段数必须一致，否则圆柱底面会和它自己的轮廓线错开一圈。
- **圆柱侧面是一个面**，不是 n 个四边形：法线逐顶点平均，拓扑里只记一个 `planar = false` 的面，竖直棱一根都不产出 —— 圆柱身上那些线是细分的痕迹，不是零件上的边。硬角轮廓反过来，每段侧面各是一个平面面，棱是真的。面拾取因此报的是面下标：点侧面点到的是「侧面」，不是六十四分之一个侧面。
- **拔模走逐顶点斜接偏移**。收过头（有一条边掉了头）会被认出来并报错，但凹轮廓在缺口处自交认不出来 —— 那需要一整套直骨架或多边形裁剪。
- **拉出来的实体与轮廓同父、同局部变换**。扫掠发生在轮廓的对象空间里，`IGeometryBuilder::Extrude` 收的世界方向在那里折算一次，实体因此正好落在轮廓上；轮廓本身留在场景里，它就是贴在实体底面上的那张草图。
- **拉伸高度是把鼠标射线投到扫掠轴上求出来的**，所以正对着那根轴的视角（顶视图里拉一个躺在 XY 平面上的圆）解不出来。这不是引擎的毛病，是那个视角下这件事本身没有答案，任何 CAD 里都一样。

---

## 4. 渲染层（`render/`）

### 4.1 Vulkan 三层封装

| 层 | 内容 | 说明 |
|---|---|---|
| `vk/Context` | Instance / PhysicalDevice / Device / Queue / Swapchain / 验证层 | 只做一次初始化 |
| `vk/*` RHI | `Buffer` `Image` `Pipeline` `DescriptorAllocator` `DeleteQueue` `UploadContext` | RAII 封装，屏蔽 Vulkan 样板 |
| `Renderer` + `pass/` | 帧循环、pass 编排、draw list 构建 | 业务层，不直接碰 raw Vulkan handle |

关键配置：
- **Vulkan 1.3 dynamic rendering**，不写 RenderPass/Framebuffer
- **VMA** 管所有显存分配
- **frames-in-flight = 2**，每帧独立 command pool / uniform ring buffer / descriptor pool
- **DeleteQueue**：资源销毁延迟 N 帧，避免 GPU 还在用
- 离屏渲染到 `VK_FORMAT_R16G16B16A16_SFLOAT` color + `D32_SFLOAT` depth，最后 blit 到 swapchain（方便后期加 MSAA/后处理/截图）

### 4.2 Pass 列表

| Pass | 作用 | 技术要点 |
|---|---|---|
| `GridPass` | 无限地面网格 | 全屏三角形，fragment shader 里解析计算网格线 + 距离淡出，零几何 |
| `MeshPass` | 实体着色 | Blinn-Phong / 简化 PBR，正反面双光照，支持 per-entity 颜色与半透明 |
| `EdgePass` | 实体轮廓与特征边 | 从 `Topology.edges` 直接取线段，和 `LinePass` 共用那对着色器与同一个实例缓冲：一条边和一条曲线在 GPU 上是同一种东西。不同的是不写深度、取 `EntityStyle::edgeColor`，而且 `RenderMode::Shaded` 下整批不画。CAD 的「黑边」是可读性核心 |
| `LinePass` | 独立线/圆/矩形线框 | **屏幕空间 quad 扩展**：instanced draw，每段线 1 个 instance，VS 在 NDC 空间按 `lineWidth` 撑开四边形；支持虚线（沿弧长累计 + `discard`） |
| `PointPass` | 点图元 | instanced billboard quad，FS 里画圆并 `discard` 外部像素 |
| `OverlayPass` | Gizmo / 高亮 / 橡皮筋预览 / 屏幕文字 | 独立 depth 策略（Gizmo 常关深度测试保证永远可见）。M6 的文字也在这里：笔画字体拆成线段，走的还是 `LinePass` 那对着色器 |

防 z-fighting 的偏移**加在实体表面上，不加在边上**：`MeshPass` 把面往后推一格深度单位（`depthBiasConstantFactor`），单位是深度缓冲自己的最小可分辨量，所以一米的零件和一毫米的零件都合适。反过来给边一个固定的 NDC 偏移的话，在小零件上背面的边会从正面透出来。

**为什么不用 `VK_POLYGON_MODE_LINE` / `vkCmdSetLineWidth`：** `wideLines` 是可选 feature，很多驱动只支持宽度 1.0；且线宽是像素级不随投影变化、无法做虚线和端点样式。屏幕空间扩展一次解决全部问题，代价只是 VS 里几行数学。

### 4.3 GPU 数据流

```
Scene 脏实体 → GpuScene::Sync()
   ├─ MeshData  → 上传到 大 VertexBuffer / IndexBuffer 的子分配区间
   ├─ Transform → 每帧写入 per-object SSBO（数组下标 = drawIndex）
   └─ 生成 RenderProxy { bufferRange, materialId, flags }
每帧：视锥剔除(BVH) → 按 pipeline 分桶 → 每桶一次 draw（后期升级 DrawIndexedIndirect）
```
- **管线是有限固定集合**（约 8~10 条），不做通用材质系统。CAD 场景够用，复杂度骤降。
- 大坐标处理：SSBO 里存 `worldPos - cameraOrigin`（double 减完再转 float），彻底消除远离原点时的抖动。

### 4.4 ISurface 抽象

```cpp
class ISurface {
public:
    virtual VkSurfaceKHR CreateVkSurface(VkInstance) = 0;
    virtual void         GetExtent(uint32_t& w, uint32_t& h) const = 0;
    virtual bool         IsMinimized() const = 0;
    virtual void         Release() = 0;
};
// 两个实现：
//   GlfwSurface   —— 引擎内建窗口，examples 与调试用，自带消息循环
//   NativeSurface —— VkWin32SurfaceCreateInfoKHR{ hwnd }，宿主管消息循环与 resize 通知
```
宿主嵌入模式下，输入事件由宿主转发给 `IViewport::OnMouseEvent/OnKeyEvent`，引擎不碰系统消息。

### 4.5 Shader 构建

```
shaders/*.vert|.frag  --glslc-->  *.spv  --bin2c(cmake script)-->  *.spv.h (C 数组)
                                                                        │
                                                            编译进 DLL，运行时零外部文件依赖
```
`cmake/CadGeomShaders.cmake` 提供 `cadgeom_add_shaders(target ...)`，增量构建、改 GLSL 自动重编。

---

## 5. 场景与命令（`scene/`）

```
Scene
 ├─ EntityStore     SoA: { id, parentId, localXform, worldXform, aabb, shapeId, style, flags }
 ├─ GeometryStore   ShapeId → Shape（内核持有）
 ├─ Bvh             拾取 + 视锥剔除，脏时增量重建
 ├─ Selection       selected: set<EntityId>, hovered: EntityId
 └─ CommandStack    undo/redo
```
- **Entity 是 ID 不是继承树**。组件按需附加，避免 `class Circle : public Shape : public Object` 这种深继承在 CAD 里必然崩溃的结构。
- 层级变换：`world = parent.world * local`，脏标记向下传播，每帧统一 flush。
- **所有场景修改必须走 Command**：

```cpp
class ICommand {
    virtual void Execute(Scene&) = 0;
    virtual void Undo(Scene&) = 0;
    virtual const char* GetName() const = 0;
};
// CreateShapeCommand / DeleteCommand / TransformCommand / ModifyParamCommand / ExtrudeCommand
```
Undo/Redo 在 CAD 里不是加分项是必需品，一开始就走 Command 模式，比后期补便宜一个数量级。

---

## 6. 交互层（`interact/`）

### 6.1 工作平面（Work Plane）——交互式创建的关键抽象

2D 图元（圆/矩形）需要一个落点平面，否则鼠标射线在 3D 空间无解。

```cpp
struct WorkPlane { Vec3d origin, normal, uAxis, vAxis; };
// 来源：预设 XY/YZ/ZX  |  用户拾取的某个已有平面  |  过某点垂直于视线
Vec3d p = workPlane.RayIntersect(camera.ScreenToRay(mouseX, mouseY));
```
所有 2D 创建工具都在当前工作平面上工作。**这个抽象漏掉的话，交互式创建会做成一团乱麻。**

### 6.2 Tool 状态机

```cpp
class ITool {
public:
    virtual void OnActivate(ToolContext&) {}
    virtual ToolResult OnMouseDown(const MouseEvent&, ToolContext&) = 0;
    virtual ToolResult OnMouseMove(const MouseEvent&, ToolContext&) = 0;
    virtual ToolResult OnMouseUp  (const MouseEvent&, ToolContext&) = 0;
    virtual ToolResult OnKey      (const KeyEvent&,   ToolContext&) = 0;
    virtual void BuildPreview(IOverlayBuilder&) = 0;      // 橡皮筋预览（不进场景）
    virtual void OnCancel() = 0;                          // Esc
};
```

| 工具 | 状态机 |
|---|---|
| `PointTool` | 点击 → 落点 |
| `LineTool` | 点1 → 拖拽预览 → 点2 →（连续模式可续画） |
| `CircleTool` | 圆心 → 拖拽预览半径 → 确定 |
| `RectangleTool` | 角点1 → 拖拽预览 → 角点2 |
| `ExtrudeTool` | 选中闭合轮廓 → 沿法线拖拽预览高度 → 确定 → 一步可撤销的拉伸（`CreateShapeCommand`，撤销菜单里叫「Extrude」） |
| `MoveTool` / `RotateTool` | 选中 → Gizmo 拖拽 → `TransformCommand` |

预览几何走 `OverlayPass`，不进场景、不进 undo 栈；只有 commit 时才产出 Command。

### 6.3 拾取与吸附

```
鼠标 → 射线(double) → BVH 遍历 → 候选
优先级：顶点/端点(6px) > 边/曲线(6px) > 面(精确三角形求交)
吸附类型：端点 · 中点 · 圆心 · 象限点 · 交点 · 垂足 · 网格
```
面拾取返回 `{ entityId, faceIndex, hitPoint, normal }`——`faceIndex` 让「点某个面 → 设为工作平面 → 在上面画圆 → 拉伸」这条 CAD 核心工作流成立。

M3 的落地细节：

- **优先级是严格的，不比距离。** 一个面总是比它自己的轮廓边先被射线碰到，按距离排就永远选不中边；容差已经保证了「顶点/边就在附近」这件事成立。
- **像素容差换成世界容差写成 `base + slope * t`。** 正交下 `slope = 0`，透视下 `base = 0`，窄阶段因此不必知道自己在哪种投影里。`IScene::Raycast` 没有相机可问，用的是场景包围盒对角线的一个比例。
- **没选中不是错误**，不写错误槽——鼠标划过空白是最常见的情况。
- 承载平面（圆/圆弧/矩形）会作为命中法线返回，所以 `SetWorkPlaneFromPick` 在 M4 的实体做出来之前就能用：点一个圆，就能在它的平面上继续画。
- ~~**垂足吸附（`Snap_Perpendicular`）没有做**~~（M6 补上了）：垂足是相对「上一个点」说的，而 `IToolContext::SnapAt` 的签名已经冻结，没有地方传那个参考点。M6 的解法见下面的落地细节 —— 比「加一个参数」简单。

### 6.4 相机

`OrbitCamera`：中键拖拽旋转 / Shift+中键平移 / 滚轮以光标位置为中心缩放 / 双击框选后 Zoom-to-fit。正交与透视可切，正交为默认。

### 6.5 Gizmo

平移：三根轴向箭头 + 三个平面方块；旋转：三个圆环。拖拽时把鼠标射线投影到轴向直线（或平面）求参数增量，实时更新 + `OverlayPass` 高亮当前轴。松开鼠标才 push Command（拖拽过程只改 transform 不进 undo 栈）。

---

## 7. 导入导出（`io/`）

```cpp
class IImporter { virtual CgResult Import(const char* path, IScene*, const ImportOptions&) = 0; };
class IExporter { virtual CgResult Export(const char* path, const IScene*, const ExportOptions&) = 0; };
class IIoRegistry {
    virtual void Register(const char* ext, IImporter*, IExporter*) = 0;   // ★ 扩展点
    virtual CgResult Import(const char* path, ImportOptions);             // 按扩展名分派
};
```

| 格式 | 导入 | 导出 |
|---|---|---|
| OBJ | tinyobjloader | 自写 writer（格式简单，含 .mtl） |
| glTF 2.0 | tinygltf（.gltf + .glb） | tinygltf |

**glTF 的参数化保真技巧：** 把 CadGeom 的参数化定义（圆半径、拉伸方向与距离、工作平面）序列化进 glTF 节点的 `extras` 字段。
- 自家导出→导入：**参数化信息完整往返**，导入后仍可改半径、改拉伸高度。
- 被 Blender/三方软件读：`extras` 被忽略，就是一个普通 mesh，完全兼容。

这一手让 glTF 事实上成为 CadGeom 的原生格式，省掉自研文件格式。轴向在此层做 Z-up ↔ Y-up 转换。

M5 的落地细节：

- **`io/` 也把依赖箭头倒过来。** 实体表和几何内核在 `api/`，在 `io/` 之上，所以 `io/` 声明
  `ISceneSource` / `ISceneSink`，由 `api::SceneIoBridge` 实现并把数据递下去 —— 和 `interact/`
  的 `IPickTargetSource` 是同一手。它需要自己的门，是因为公开接口确实给不了这两件事：`IScene`
  没有读网格的入口，`IGeometryBuilder` 也没有 `MakeMesh`。那不是疏漏 —— 宿主拿到的是一个参数化
  的 CAD 场景，底下的三角形是内部缓存。
- **换轴挂在根节点上，不烤进顶点里。** 导出时整棵场景挂在一个绕 X 转 -90° 的根节点下面。顶点
  和 `extras` 因此都留在对象空间，两者永远对得上；导入时把这次旋转叠回顶层节点，我们自己写的
  文件里那两次旋转正好抵消，往返一遍一个多余的角度都不剩。烤进顶点的话，`extras` 里的对象空间
  参数就和顶点各说各话了。
- **参数在，就不读那堆三角形。** 带 `extras.cadgeom.shape` 的节点是从参数重建的，同一个节点上的
  网格被有意丢掉。信网格等于让缓存反过来定义真相 —— 而这正是 §3.1 唯一禁止的方向。
- **导入的网格自己就是定义。** `ShapeType::Mesh` 的三角形存在 `geom::ShapeDef::mesh` 里，不是
  缓存：没有任何参数能重新生成它们，改细分容差自然也不会。`Tessellate` 对它只做两件事 —— 抄进
  缓存，然后把特征边认出来。
- **特征边是从三角形汤里认回来的。** 按位置焊接，保留两面夹角超过 30° 的棱和只有一个面用到的
  边界边，再按连通性串成链：导进来的立方体得到 12 条单段棱和 8 个角点，圆柱得到两条闭合的端面
  圈、一个角点都没有。30° 这个阈值远高于细分曲面上相邻面之间的 ~12°，否则画出来的是细分的痕迹，
  不是零件的边。CAD 的可读性靠黑边，而三角形汤里没有「边」这个概念。
- **一次导入是一步撤销，要么就一步都不留。** `ISceneSink::Begin` 开一个命令组，`End(false)` 走
  `CommandStackImpl::AbortGroup` 整批撤销并丢弃 —— 读到一半的文件不该能从「重做」菜单里捞回来。
  例外是 `mergeIntoScene = false`：清空场景连撤销栈一起清，那次导入撤不回来，头文件里写着。
- **OBJ 是有损的，而且损在明处。** 没有层级、没有变换、没有参数化定义，顶点写的是世界坐标。曲线
  走 `l`、点图元走 `p`（绝大多数导出器直接把它们丢了），读回来是折线和点 —— 一个圆出去是折线，
  回来还是折线。要保住参数化就用 glTF。
- **精度的两条线。** `extras` 里的参数是 double，往返分毫不差；而 glTF 的 POSITION 按规范只能是
  float32，所以**没有**参数化定义的实体（导入的网格）往返一次是 float 精度。OBJ 写的是十位有效
  数字的文本，读回来用 `TINYOBJLOADER_USE_DOUBLE`，不经过 float。
- **路径是 UTF-8，所以谁都不许自己开文件。** 两个第三方库自带的文件入口收的都是 narrow 路径
  （Windows 上就是当前代码页），中文目录下直接打不开。字节由 `core/File.h` 读进来交给它们解析，
  写出来的字节也由它落盘。

---

## 7.1 打磨（M6）的落地细节

M6 补的是「能拿来干活」和「能跑起来」之间的差距：那些一个人真拿它画图时，第一个小时里
就会撞上的东西。

- **新能力从扩展槽进来，不往冻结的接口上加虚函数。** `include/cadgeom/IEngineExt.h` 里的
  `ICadEngine2` 由 `ICadEngine::GetExtension(ExtensionId_Engine2)` 交出去（§2.2 第 3 条
  留的那个口子）。单位系统、吸附参考点、状态栏文本、HUD 开关、实际采样数、测量结果 ——
  六件事分别属于引擎、工具和视口，往三个已发布的接口上各挂一批虚函数是在给自己埋雷。
  它是引擎的一个成员，宿主拿到的是借来的指针，**没有 `Release`**：那正是扩展对象和普通
  接口对象的区别。不认识的 id 返回 null，宿主据此降级而不是崩。

- **垂足吸附的参考点变成了引擎状态，而不是一个新参数。** M3 卡住的地方是
  `IToolContext::SnapAt(x, y, out)` 已经冻结，传不进「上一个点」。加 `SnapAt2` 是一条路，
  但那意味着每个工具都要判断自己能不能用新版本。真正的答案是：**参考点根本不属于那次调用**，
  它属于「当前正在画的这一笔」，所以它住在 `interact::ToolSettings` 里 ——
  内置工具落下第一个点时设它，宿主写的工具用 `ICadEngine2::SetSnapReference` 设，
  而 `SnapAt` 的签名一个字都没动。垂足因此对内置工具和宿主工具同时生效。
  几何那一半在 `geom::CollectPerpendicularPoints`：垂足夹在曲线的实际范围内（线段夹在两端
  之间、圆弧夹在扫描角之内，落在端点上的不算 —— 那是端点吸附的活儿），因为延长线上的垂足
  在图纸上不存在。

- **屏幕上的字是笔画字体，走 `LinePass`，没有字形图集。** `IOverlayBuilder::AddText`
  从 M2 起就是空的，因为「画字」听起来意味着一张纹理、一个采样器、一条带纹理的管线和一套
  描述符 —— 为了状态栏上的一行提示。但线段撑成屏幕空间四边形这件事引擎早就做完了，而
  **笔画字体正好是一堆线段**（`interact/TextStroke.cpp`，4×6 的格子，ASCII，小写按大写画）。
  这也不是将就：CAD 图纸上的字历来是笔画字体（AutoCAD 的 `txt.shx` 就是），放多大都不糊，
  而且和图线用同一支笔。代价是只有 ASCII —— 所以 HUD 上的字是英文的，而注释是中文的。

- **HUD 是引擎自己画的，ImGui 没有进来。** §0.1 说 UI 只在 examples 里用、核心库保持无 UI
  依赖；而 `examples/glfw_viewer` 从 M0 起就只链接 `cadgeom` 一个库，连 GLFW 都不链
  —— 窗口归引擎（`SurfaceKind::Glfw`），宿主拿不到 `GLFWwindow*`，也拿不到
  `VkDevice`，ImGui 的两个后端都没有立足之地。硬要给 ImGui 让路，就得在公共接口上开一个
  「收下这批二维顶点」的口子，那是发明一套 UI 协议。所以 M6 走的是另一条：引擎用上面那套
  笔画字体在左下角画状态行和一行摘要（工具名、显示模式、投影、网格间距、MSAA），
  `H` 键开关。宿主要做真正的面板 —— Qt 的、MFC 的、ImGui 的 —— 需要的东西一件不少地在
  `ICadEngine2` 上：`GetStatusText()` 给提示文本，`GetMeasurement()` 给读数，
  `GetUnitSettings()` / `FormatLength()` 给格式化，然后把 `SetHudVisible(false)` 关掉自绘那份。

- **单位系统只管显示，一个顶点都不碰。** 引擎内部只有一种长度：模型单位。`UnitSettings`
  里的 `modelUnit` 说「模型里的 1.0 是多长」，`displayUnit` 说「读出来用什么」，两者互不
  相干。换显示单位不推进 `revision`，因此不会让整个场景重传一遍显存 —— 这和「params 是
  SSOT、mesh 是派生缓存」是同一条纪律的延伸：**读数也是派生的**。英制走国际英寸
  （1 in = 25.4 mm 整），来回换算分毫不差。

- **MSAA 是「一条管线一个采样数」，所以 pass 按采样数分套。** dynamic rendering 下管线绑的
  是附件的*格式*和*采样数*，而 M6 允许每个视口各要一个 `sampleCount`。于是
  `render::RenderSystem` 持有一张「采样数 → `PassSet`」的表，按需建：两个同采样数的视口共用
  一套，从不开 MSAA 的宿主永远只有一套。帧里画进多采样附件，结束时 `VK_RESOLVE_MODE_AVERAGE_BIT`
  resolve 到原来那张单采样的半浮点图上 —— **resolve 之后的每一步（blit 到 swapchain、截图
  读回）因此一个字都不用改**，这正是 M1 就把离屏合成做进去换来的那笔钱。设备支持不到宿主要的
  档位时向下取（8x 要不到就给 4x，而不是给 1x），降了记一条日志，宿主也能从
  `ICadEngine2::GetSampleCount()` 问回来。

- **隐藏线是两遍深度，不是一遍。** `RenderMode::HiddenLine`：第一遍让 `MeshPass` 用一条
  `colorWriteMask = 0` 的管线把实体的深度铺出来（**看不见但挡得住**），第二遍把边和曲线的
  深度判据翻成 `VK_COMPARE_OP_GREATER` —— 深度测试*没通过*的片元恰好就是被遮挡的那些，
  它们画成虚线；第三遍照常画可见的边。表面的 depth bias 一直留着，所以贴在面上的边不会被
  自己所在的那个面判成「被遮挡」。出来的图和一张工程图纸是同一回事：全是线，而背面那些线
  你看得见、也知道它们在背面。

- **多视口本来就成立，M6 只是把它跑起来了。** 设备、管线、`GpuScene` 都在
  `RenderSystem` 里按引擎共享，视口只额外持有一个 swapchain、一组帧资源和一台相机，所以
  「第二个视口的成本是一个 swapchain」这句话从 M1 起就是真的。要补的是使用一侧：一次
  `Tick()` 之后每个视口各画一帧（脏几何只解析一次、只上传一次），以及 demo 里那个
  `--viewports 2` —— 第二个窗口是正视图加隐藏线，和第一个的轴测着色图同时在屏幕上，
  共享的只有几何。

- **`ZoomToFit` 有选择集就框选择集。** `F` 键和中键双击都走 `FitView()`：选中了东西就框它，
  没选中才框整个场景。「按 F 看看这个」问的是选中的那个东西，不是整张图。

- **Scale 绕世界轴缩放，不装作能处理所有情况。** Gizmo 多了一个 `GizmoMode::Scale`：三根轴
  手柄按「现在的参数 / 按下时的参数」求倍率，原点上的等比手柄按屏幕距离之比求倍率（屏幕
  距离在正交和透视下是同一件事，所以这一段不必知道自己在哪种投影里）。增量在世界空间里合成
  再折回局部变换，和 Move/Rotate 走的是同一条路 —— 于是也继承了同一个限制：绕世界轴缩放
  一个已经转过身的实体会产生错切，而 TRS 表示不了错切。那种情况下工具记一条日志并让实体
  留在原地，而不是给一个看着合理其实是错的姿态。倍率有下限，拖过原点不会变成镜像。

- **Measure 是唯一一个什么都不改的工具。** 不建几何、不推命令、不碰选择集 —— 所以它也是
  叠加层文字第一个真正的用户（一个量不出数来的测量工具没有意义）。两个点都走吸附，而第一个
  点落下之后会成为垂足参考点，「量这个角到那条边的垂直距离」因此是点两下的事。结果留在
  `ToolSettings` 里，宿主从 `ICadEngine2::GetMeasurement()` 取；斜距画在图上，三个分量走
  状态栏 —— 一条标注线上挤四个数就没人读得下去了。

---

## 8. 构建系统

```
CMake ≥ 3.24（当前环境 3.26.3 ✓）
├─ cadgeom            SHARED —— 唯一交付物
├─ cadgeom_tests      几何内核 / IO / 拾取单测（不依赖 Vulkan，CI 友好）
└─ glfw_viewer        EXECUTABLE —— 独立 demo
```

**Submodules（`external/`）**

| 库 | 用途 |
|---|---|
| glfw | GLFW surface 后端 + demo 窗口 |
| glm | 着色器侧数学（float）；内核侧用自研 double 数学 |
| VulkanMemoryAllocator | 显存分配 |
| tinyobjloader | OBJ 导入 |
| tinygltf | glTF 导入导出 |
| Catch2 或 googletest | 单元测试 |
| imgui | **没有接进构建**：M6 的界面走引擎自绘的笔画字体 HUD，原因见 §7.1。子模块留着，宿主要在自己那边用它是宿主的事 |

**外部依赖：Vulkan SDK 手动安装**（`find_package(Vulkan REQUIRED)` 取 loader、headers、`glslc`、验证层）。

**导出宏**

```cmake
set_target_properties(cadgeom PROPERTIES
    CXX_VISIBILITY_PRESET hidden      # Linux/Mac 也只导出该导的
    VISIBILITY_INLINES_HIDDEN ON)
generate_export_header(cadgeom BASE_NAME CADGEOM EXPORT_FILE_NAME include/cadgeom/Export.h)
```

**安装产物**

```
install/
├─ include/cadgeom/*.h
├─ lib/cadgeom.lib          导入库
├─ bin/cadgeom.dll
└─ lib/cmake/CadGeom/       CadGeomConfig.cmake —— 宿主一行 find_package(CadGeom) 接入
```

---

## 9. 实施里程碑

| 阶段 | 交付 | 验收 |
|---|---|---|
| **M0 骨架** ✅ | CMake 工程、submodules、导出宏、`ICadEngine` 空实现、`cadgeom.dll` + demo 链接通过 | demo 能 create/release engine，无泄漏 |
| **M1 Vulkan 起飞** ✅ | Context/Swapchain/RHI/Renderer、`GridPass`+`MeshPass`、OrbitCamera、ISurface 三后端（Glfw / Win32 / Headless） | 窗口里出现可旋转的网格地面 + 一个立方体 |
| **M2 几何与线条** ✅ | 内核 + 点/线/圆/矩形、`LinePass`(屏幕空间)、`PointPass`、Tool 状态机、WorkPlane | 鼠标能交互画出点/线/圆/矩形，线宽正确、虚线可用 |
| **M3 选择与操作** ✅ | BVH、Picker（点/边/面优先级）、Selection 高亮、Gizmo、吸附、CommandStack | 能选中、拖动、旋转，Ctrl+Z/Y 正常 |
| **M4 拉伸成体** ✅ | Profile 三角化、Extrude + Topology、`EdgePass`、`ExtrudeTool` | 圆→圆柱、矩形→立方体，交互式拖拽高度，带轮廓黑边 |
| **M5 数据 IO** ✅ | IoRegistry、OBJ 读写、glTF 读写、`extras` 参数化往返、`ShapeType::Mesh` | 导出再导入，参数化信息不丢；Blender 能正常打开 |
| **M6 打磨** ✅ | 垂足吸附、屏幕文字与 HUD、多视口、Zoom-to-fit、单位系统、MSAA、隐藏线、Scale/Measure | 可用性达到「能拿来干活」 |

---

## 10. 风险与对策

| 风险 | 影响 | 对策 |
|---|---|---|
| ~~**Vulkan SDK 未安装**~~（已解决：1.4.357 已装，M1 在 RTX 3070 Ti 上跑通） | M1 直接阻塞 | 开工前装 LunarG Vulkan SDK ≥ 1.3.275，确认 `VULKAN_SDK` 与 `glslc` 可用 |
| C++ 纯虚接口的 ABI 脆弱性 | 宿主换编译器 → 诡异崩溃 | §2.2 七条纪律写进 CLAUDE.md；`CadGeom_GetApiVersion()` 启动校验；文档标注支持的编译器矩阵 |
| Vulkan 样板代码量大，容易淹没进度 | M1 拖长 | 严守 vk RHI 分层，先「能跑」再「优雅」；dynamic rendering + VMA 已砍掉大半样板 |
| ~~任意多边形三角化~~（已解决：M4 直接实现耳切法，凹轮廓可用） | M4 只能支持凸轮廓 | 自写耳切法（`geom/Profile.cpp`），比引入依赖便宜，也不必先交一个只支持凸轮廓的版本 |
| 大坐标精度抖动 | 远离原点时模型抖动 | 内核 double + camera-relative 上传，M1 就做进去，后期返工代价极高 |
| 参数化与网格不同步 | 改半径不生效 / 幽灵几何 | 严格单向：params 为 SSOT，mesh 为派生缓存，只有 `Tessellate` 能写 mesh |

---

## 11. 首个 Sprint 建议（M0 + M1 前半）

1. 装 Vulkan SDK，验证 `find_package(Vulkan)` 与 `glslc`
2. 搭 CMake 三目标工程 + 6 个 submodule + 导出宏 + install 规则
3. 落地 `include/cadgeom/` 全部接口头（**先定契约再写实现**，这是 COM 风格路线的正确顺序）
4. `api/EngineImpl` 空实现打通 create/release，demo 链接跑起来
5. `vk/Context` + `Swapchain` + `GlfwSurface`，清屏成功
6. `GridPass` + `OrbitCamera`，得到第一个可交互的 3D 视口
