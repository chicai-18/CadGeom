# qt_viewer — 把引擎嵌进一个真实的宿主 UI

`glfw_viewer` 演示的是「引擎自带窗口，宿主什么都不用管」（`SurfaceKind::Glfw`）。
这个示例演示的是另一半，也是嵌进 Qt / MFC / WPF 时真正会走的那条路：
**宿主已经有一整套界面了，引擎只负责那块 3D 区域**
（`SurfaceKind::NativeWin32`，`docs/architecture.md` §4.4）。

它链接的东西只有两样：`cadgeom` 和 `Qt5/6::Widgets`。Vulkan、GLFW、tinygltf 全在共享
库里面，一个都不会漏进宿主的编译单元。

## 构建

Qt 不是 vendored 依赖，得告诉 CMake 它在哪儿：

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_PREFIX_PATH=D:/Qt/5.15.2/msvc2019_64
cmake --build build --config Debug --parallel
build/bin/Debug/qt_viewer.exe
```

Qt5 和 Qt6 都行（`find_package(QT NAMES Qt6 Qt5 ...)`）。找不到 Qt 的话这个示例整块
跳过，主库、测试和 `glfw_viewer` 照常构建 —— 一个可选示例不该挡住别人的路。

Windows 上构建完会跑一次 `windeployqt`，Qt 的运行时直接落在可执行文件旁边，双击就能
跑；不想要就 `-DCADGEOM_QT_DEPLOY=OFF`。

起来是一张**空图纸** —— 宿主替用户往场景里塞几何，是替他做了一个不该由程序做的决定。
要那张安装板就走「文件 → 载入示例图纸」（`Ctrl+Shift+N`），命令行上是 `--sample`。

自检用的几个开关，和 `glfw_viewer` 的 `--headless --screenshot` 是同一个用意 ——
「嵌在宿主窗口里的那条渲染路径还通不通」总得有个不靠手的验法。截图要 `--sample`，
不然截出来的是一张只有网格的空图：

```sh
build/bin/Debug/qt_viewer.exe --sample --screenshot shot.png --frames 60
build/bin/Debug/qt_viewer.exe --sample --frames 300
```

退出时会检查 `CadGeom_GetLiveObjectCount()`。没回到 0 就是有人漏了一次 `Release()`，
退出码变成 2。

## 三个真正需要动脑子的接入点

### 1. 让 Qt 交出一个 HWND，然后别再往上面画

```cpp
setAttribute(Qt::WA_NativeWindow);      // 换来一个真实的 HWND
setAttribute(Qt::WA_PaintOnScreen);     // 这块地方归别人画
setAttribute(Qt::WA_NoSystemBackground);
setAttribute(Qt::WA_OpaquePaintEvent);
QPaintEngine* paintEngine() const override { return nullptr; }
```

少了后两句，Qt 的 backing store 会盖在交换链画出来的画面上，表现为闪烁或者一块纯灰。
`winId()` 就是给 `SurfaceDesc::nativeWindow` 的那个句柄。视口在**第一次 `showEvent`**
里建 —— 在那之前没有窗口可言。

### 2. 高 DPI：引擎认的是物理像素

引擎那边的 `NativeSurface::GetExtent` 是拿 `GetClientRect` 量的，量出来是物理像素；
Qt 的 `width()/height()` 和鼠标坐标是逻辑像素。两者差一个 `devicePixelRatio`，不折算
的话拾取会整体偏到左上角去。`CadGeomWidget` 在转发前统一乘过。

### 3. 快捷键归谁

引擎自己也认一套字母键（`V/L/C/…` 切工具、`1`~`7` 切视图），但那是给「引擎拥有窗口」
的场合准备的。这里窗口是 Qt 的，键就该由 `QAction` 领走 —— 菜单上才写得出提示、才禁得
掉、才和工具栏共用一个勾选状态。没被菜单认领的键（`Esc`、`Enter`）才落到控件手里转发
给引擎，而那几个恰好正是工具自己要的（取消手势、结束多段线）。

还有一处不那么显眼：`IToolManager::Activate` 只在**已经有上下文**时才调
`ITool::OnActivate()`，而上下文是视口在派发事件时顺手塞进去的。从菜单切工具走的不是
事件，所以 `MainWindow::primeToolContext()` 先补一个「鼠标没动」的移动事件，新工具才会
被 `Reset()` 并写出自己的提示语。

## 界面覆盖到的引擎能力

| 菜单 | 用到的接口 |
|---|---|
| 文件 | `IIoRegistry`（枚举格式建过滤器、`Import`/`Export`、`IoProgressCallback` 接 `QProgressDialog`）、`IViewport::SaveScreenshot`、`IScene::Clear` |
| 编辑 | `ICommandStack`（撤销/重做/`PeekUndoName`/分组/`SetCapacity`/清空）、`ISelection`、`IScene::DestroyEntities`/`SetParent`、`IGeometryBuilder::GetParams`/`SetParams`、整份 `EntityStyle`、`IEntity::SetLocalTransform`、宿主自己的 `ICommand` |
| 创建 | 全部 11 个 `ToolId`、`IGeometryBuilder::MakeArc`、`SetWorkPlane`、`IViewport::Pick` + `SetWorkPlaneFromPick` |
| 视图 | 7 个 `StandardView`、`ZoomToFit`、投影切换、4 种 `RenderMode`、网格、HUD、背景色、相机参数（fov / 视高 / 近远面）、`SetPickFilter`、多视口 |
| 捕捉 | `SetSnapMask` 的 7 个 `SnapType`、`SetSnapTolerance`、`SetContinuousMode`、`SetTessParams` |
| 测量 | `ToolId::Measure`、`ICadEngine2::GetMeasurement`/`FormatLength`、`IEntity` 的包围盒与变换、`UnitSettings` |
| 面板 | 模型树走 `GetRootAt`/`GetChild`/`GetRevision`，日志走 `EngineDesc::logCallback` |

### 宿主侧的 `ICommand`

`IEntity::SetName` / `SetStyle` / `SetVisible` / `SetLocalTransform` 是直接写的，不进撤销
栈 —— 它们改的是显示属性和位姿，不是几何定义，引擎把「要不要可撤销」的决定权留给了宿
主。一个真正的 CAD 应用当然要，所以 `HostCommands.h` 里补了四个：改名、批量改样式、
数值变换、编组。

这不是可有可无的讲究：示例图纸最初用 `SetStyle` 直接写，`Ctrl+Z` 再 `Ctrl+Y` 之后线还
在，颜色和线型没了 —— 实体以默认样式回来了。改走 `StyleCommand` 之后，撤销再重做的画面
与初始画面逐像素相同。

顺带它也证明了边界的另一个方向：对象在**宿主**里 `new`，引擎执行它、持有它，最后调它
的 `Release()`（也在宿主里）—— 谁分配谁释放，一次跨堆都没有（§2.2 第 2 条）。

## 故意没做的两件事

- **注册宿主自己的 `ITool`**：`IToolManager::RegisterTool` 是开放的，但那是「怎么扩展
  引擎」的题目，不是「怎么把引擎的能力摆进菜单」的题目。
- **注册宿主自己的 `IImporter` / `IExporter`**：同上。

两件都在头文件里写得很清楚，照着 `HostCommands.h` 的写法就能补。

## 已知的边角

- 多段线的「编辑参数」是不通的，对话框会说明原因：`ShapeParams` 是个冻结的 POD union，
  装不下变长的点表。文件没有这个限制 —— glTF 的 `extras` 里带着点表。
- 网格（导入的三角形）同样没有参数可编辑，它的三角形就是定义本身。
- 拉伸要一个不正对扫掠轴的视角。顶视图里沿 Z 拉伸没有解 —— 先转到等轴测（`7`）。
