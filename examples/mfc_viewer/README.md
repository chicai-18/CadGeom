# mfc_viewer — 把引擎嵌进一个 MFC 宿主

三个示例的分工：

| 示例 | 说的是哪件事 |
|---|---|
| `glfw_viewer` | 引擎自带窗口，宿主什么都不用管（`SurfaceKind::Glfw`） |
| `qt_viewer` | 宿主已经有一整套界面，引擎只负责那块 3D 区域（`SurfaceKind::NativeWin32`） |
| **`mfc_viewer`** | **同一件事，换 MFC 说一遍** |

后两个是同一条路径（`docs/architecture.md` §4.4），菜单也覆盖同样一批接口。之所以
写两份，是因为「嵌进宿主」的难点从来不在引擎这一侧，而在**宿主框架自己的规矩**
里 —— Qt 要靠四个 widget attribute 才肯把一块区域让出来，MFC 只要注册窗口类时不
给背景刷；Qt 的坐标是逻辑像素，MFC 这边是物理像素；Qt 有隐式鼠标抓取，Win32 得
自己 `SetCapture`。把两份摆在一起，哪些是引擎的要求、哪些是框架的脾气，一目了然。

它链接的东西只有两样：`cadgeom` 和 MFC。Vulkan、GLFW、tinygltf 全在共享库里面，一个
都不会漏进宿主的编译单元。

## 构建

三个前提，缺一个 CMake 就整块跳过这个示例（主库、测试和另外两个示例照常构建）：

* **MSVC** —— MFC 是微软的库；
* **Visual Studio 生成器** —— `CMAKE_MFC_FLAG` 只有它认，Ninja 下 CMake 不会把
  `atlmfc` 的 include/lib 路径喂给编译器；
* **VS 的「MFC 组件」** —— 默认的「使用 C++ 的桌面开发」工作负载**不带**它，要在
  Visual Studio Installer 里勾上「适用于最新 v143 生成工具的 C++ MFC」。

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
build/bin/Debug/mfc_viewer.exe
```

配置阶段会打印 `MFC example : <atlmfc 路径>`，跳过时打印跳过的原因。

起来是一张**空图纸** —— 宿主替用户往场景里塞几何，是替他做了一个不该由程序做的决定。
要那张安装板就走「文件 → 载入示例图纸」（`Ctrl+Shift+N`），命令行上是 `--sample`。

自检用的几个开关，和 `glfw_viewer` 的 `--headless --screenshot`、`qt_viewer` 的同名开关
是同一个用意 —— 「嵌在宿主窗口里的那条渲染路径还通不通」总得有个不靠手的验法。截图要
`--sample`，不然截出来的是一张只有网格的空图：

```sh
build/bin/Debug/mfc_viewer.exe --sample --screenshot shot.png --frames 60
build/bin/Debug/mfc_viewer.exe --sample --frames 300
```

退出时会检查 `CadGeom_GetLiveObjectCount()`。没回到 0 就是有人漏了一次 `Release()`，
退出码变成 2。

发布包里只有 `mfc_viewer.exe`：MFC 的运行时（`mfc140u.dll`）走的是 VC++ 可再发行组件
包，不像 Qt 那样能 `windeployqt` 一份到旁边。装了 Visual Studio 或者 VC++ Redist 的
机器上它已经在系统目录里了。

## 真正需要动脑子的接入点

### 1. 一个不被别人画的 HWND

MFC 这边比 Qt 省事得多：`CWnd` 本来就是一个真实的 HWND，本来也没有 backing store 盖
在上面。要做的只有「别让别人往这块地方画」：

```cpp
// 注册窗口类时不给背景刷 —— 有刷子的话 DefWindowProc 会在每次 WM_ERASEBKGND 里把
// 客户区涂一遍，刚呈现的那帧就被盖成一块灰的，表现为闪烁。
AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_CROSS), nullptr, nullptr);

BOOL CViewportWnd::OnEraseBkgnd(CDC*) { return TRUE; }   // 再挡一道
```

对照 `qt_viewer`：那边要 `WA_NativeWindow` + `WA_PaintOnScreen` + `WA_NoSystemBackground`
+ `WA_OpaquePaintEvent`，外加一个返回 null 的 `paintEngine()`，才换来同样的效果。

视口在**第一次拿到非零尺寸**时建（`EnsureViewport()`）：交换链要一个有面积的窗口，而
`WM_CREATE` 那会儿子窗口的矩形还是空的。

### 2. DPI：让引擎和宿主说同一套坐标

进程在 `InitInstance` 里声明成 per-monitor v2 感知：

```cpp
SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
```

不声明的话，Windows 会在高 DPI 屏上把整个窗口按比例拉伸：交换链按逻辑像素建出来再被
系统放大，画面糊一层；更糟的是鼠标坐标和引擎那边 `GetClientRect` 量出来的从此是两套
数，拾取会整体偏掉。声明之后客户区坐标就是物理像素，视口窗口里**一次折算都不用做**
（`qt_viewer` 那边处处要乘一个 `devicePixelRatio`）。

代价是分割条宽度、状态栏窗格宽度这些写死的像素数要自己按 DPI 折算 —— `MainFrame.cpp`
里的 `ScaledPixels()` 就干这个。

### 3. Win32 没有隐式鼠标抓取

Qt 在按下按钮时自动把鼠标抓给那个控件，Win32 不会：

```cpp
if (buttonsDown_++ == 0) { SetCapture(); }
```

少了这一句，环绕视角拖出窗口边界就收不到移动和抬起事件，视角卡在半路。还要留意
`WM_MOUSEWHEEL` 的坐标是**屏幕**坐标，得先 `ScreenToClient`。

### 4. 快捷键归谁

引擎自己也认一套字母键（`V/L/C/…` 切工具、`1`~`7` 切视图），但那是给「引擎拥有窗口」
的场合准备的。这里窗口是宿主的，键就该由 `ACCELERATORS` 表领走 —— 菜单上才写得出提示、
才禁得掉、才和工具栏共用一个勾选状态。没被菜单认领的键（`Esc`、`Enter`）才落到视口窗口
手里转发给引擎，而那几个恰好正是工具自己要的（取消手势、结束多段线）。

还有一处不那么显眼：`IToolManager::Activate` 只在**已经有上下文**时才调
`ITool::OnActivate()`，而上下文是视口在派发事件时顺手塞进去的。从菜单切工具走的不是
事件，所以 `CMainFrame::PrimeToolContext()` 先补一个「鼠标没动」的移动事件，新工具才会
被 `Reset()` 并写出自己的提示语。

### 5. UTF-8 ↔ UTF-16 只有一处

引擎的字符串全是 UTF-8 `const char*`（ABI 规则第 1 条），这个宿主是 Unicode 构建的 MFC，
`CString` 是 `CStringW`。换算集中在 `Utf8.h` 的两个函数里，散落在调用点上迟早会漏掉一
个 —— 表现为菜单里的中文变成乱码，或者带中文路径的文件打不开。

**不要**用 `CT2A` / `CW2A` 那一类宏：它们按 ANSI 代码页转换，在简体中文机器上把 UTF-8
当 GBK 处理，看着能用，换台英文机器就全错。

### 6. 日志是跨线程来的

引擎的日志回调「可能来自产生消息的那个线程」，而 Win32 控件只能在建它的那个线程里碰。
`LogSink` 因此是「加锁的队列 + `PostMessage` 叫醒主窗口」——`PostMessage` 是少数几个明确
允许跨线程调用的 API 之一。用队列而不是「投递一个 `new` 出来的字符串」，是因为窗口还没
建起来时也会有日志（引擎创建阶段那几条），那些得先攒着。

`qt_viewer` 那边用的是跨线程排队的信号槽 —— 一个道理的两种写法。

### 7. 界面状态：ON_UPDATE_COMMAND_UI

这是 MFC 比 Qt 省事的一处。`qt_viewer` 每帧手动同步一遍 `QAction` 的启停与勾选；MFC 的
框架在空闲时和菜单弹出前替你问一遍，菜单项和工具栏按钮共用同一个回答：

```cpp
void CMainFrame::OnUpdateHasSelection(CCmdUI* cmdUI) {
    cmdUI->Enable(engine_.GetScene()->GetSelection()->GetCount() > 0);
}
```

一个坑：同一个命令 id 在菜单和工具栏上各有一份，`CCmdUI::SetText` 会把两边都改掉 ——
给工具栏按钮塞上「撤销 Extrude\tCtrl+Z」会把那颗按钮撑变形。看 `m_pMenu` 非空再改文字。

## 界面覆盖到的引擎能力

| 菜单 | 用到的接口 |
|---|---|
| 文件 | `IIoRegistry`（枚举格式建过滤器、`Import`/`Export`、`IoProgressCallback` 接自绘进度框）、`IViewport::SaveScreenshot`、`IScene::Clear` |
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

它也证明了边界的另一个方向：对象在**宿主**里 `new`，引擎执行它、持有它，最后调它的
`Release()`（也在宿主里）—— 谁分配谁释放，一次跨堆都没有（§2.2 第 2 条）。

`HostCommands.*` 和 `SampleScene.*` 与 `qt_viewer` 里的那两份**逐字相同**。那不是疏忽：
它们只跟引擎打交道，一个 MFC 或 Qt 的符号都没有 —— 「宿主侧的引擎代码」和「宿主用什么
UI 框架」本来就是两件事，换框架时这两个文件原样搬过来就行。

## 和 qt_viewer 明确不一样的几处

| | qt_viewer | mfc_viewer |
|---|---|---|
| 面板 | `QDockWidget`，能拖出去浮动 | `CSplitterWnd`，只能拉宽窄或收起来 |
| 模型树 | 多选（`ExtendedSelection`） | 单选 —— Win32 的树控件没有多选。多选照旧在视口里按住 Ctrl 点，状态栏那格会报「选中 N / M」 |
| 树的列 | 名称 / 类型两列 | 一列，写成 `名称 [类型]` |
| 对话框 | 按形状类型一行行拼出来 | 模板在 `.rc` 里写死，用不到的行藏起来再把下面的往上收（`CShapeParamsDialog::LayoutRows`） |
| 工具栏 | `QAction` 自带文字 | 文字工具栏，没有位图资源 |
| 进度框 | `QProgressDialog` + `processEvents` | 非模态对话框 + 禁用主窗口 + 自己抽消息队列 |

## 两个会咬人的小地方

* **资源脚本必须声明 UTF-8**。`.rc` 里有中文，而 `rc.exe` 默认按系统的 ANSI 代码页读；
  在简体中文机器上，一个汉字就能把后面的字符吃掉。文件头的 `#pragma code_page(65001)`
  和源码那边的 `/utf-8` 是同一件事的两个开关。
* **`CToolBar::SetSizes` 不接受 0×0 的图像尺寸**。`TB_SETBITMAPSIZE(0,0)` 才是「这条工具
  栏没有图像」的正规写法，但 MFC 那层包装里有一句 `ASSERT(sizeImage.cx > 0)`，Debug 构建
  下会当场断言。给 1×1：一个像素谁也看不见，而按钮尺寸的记账仍旧由 `CToolBar` 自己算。

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
