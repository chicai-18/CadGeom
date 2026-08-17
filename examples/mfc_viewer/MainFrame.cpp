/**
 * @file MainFrame.cpp
 * @brief CMainFrame 的实现 —— 菜单挂命令，命令调引擎。
 */
#include "MainFrame.h"

#include "Dialogs.h"
#include "HostCommands.h"
#include "LogSink.h"
#include "SampleScene.h"
#include "Utf8.h"
#include "ViewportWnd.h"
#include "resource.h"

#include <cadgeom/CadGeomRAII.h>

#include <afxdlgs.h>

#include <cmath>
#include <vector>

using namespace cadgeom;

namespace {

constexpr double kPi = 3.14159265358979323846;

/// 帧定时器。16ms ≈ 60Hz；真正的节奏还是由交换链的 vsync 定。
constexpr UINT_PTR kFrameTimer = 1;

/// @brief 「视口建不起来」是在 WM_SIZE 的栈上发现的，弹窗要等回到消息循环再弹。
#define WM_CADGEOM_VIEWPORT_FAILED (WM_APP + 3)

/// 工具表：下标 = 命令 id 减 ID_TOOL_FIRST，所以顺序必须和 resource.h 里那一段、
/// 和菜单里那一串一致。
const ToolId kToolTable[] = {ToolId::Select, ToolId::Point,  ToolId::Line,      ToolId::Circle,
                             ToolId::Rectangle, ToolId::Polyline, ToolId::Extrude, ToolId::Move,
                             ToolId::Rotate, ToolId::Scale,  ToolId::Measure};

const StandardView kViewTable[] = {StandardView::Front, StandardView::Back,  StandardView::Right,
                                   StandardView::Left,  StandardView::Top,   StandardView::Bottom,
                                   StandardView::Isometric};

const uint32_t kSnapTable[] = {Snap_Endpoint,     Snap_Midpoint,      Snap_Center, Snap_Quadrant,
                               Snap_Intersection, Snap_Perpendicular, Snap_Grid};

const uint32_t kPickTable[] = {PickFilter_Vertex, PickFilter_Edge, PickFilter_Face};

/// @brief 形状类型的中文名，给模型树和信息框用。
CString ShapeTypeName(ShapeType type) {
    switch (type) {
        case ShapeType::None: return _T("组");
        case ShapeType::Point: return _T("点");
        case ShapeType::Line: return _T("直线");
        case ShapeType::Circle: return _T("圆");
        case ShapeType::Arc: return _T("圆弧");
        case ShapeType::Rectangle: return _T("矩形");
        case ShapeType::Polyline: return _T("多段线");
        case ShapeType::Mesh: return _T("网格");
        case ShapeType::Solid: return _T("实体");
    }
    return CString();
}

/// @brief 把一个「96 DPI 下的像素数」折算成当前显示器上的像素数。
///
/// 进程是 per-monitor v2 DPI 感知的，所以窗口和控件的坐标都是**物理**像素：分割条
/// 的宽度、状态栏窗格的宽度这类写死的数，不折算的话在 150% 缩放的屏幕上就都短了
/// 三分之一 —— 文字还是按 DPI 放大的，于是一格里的字被截掉。
int ScaledPixels(CWnd* wnd, int pixels96) {
    CClientDC dc(wnd);
    return MulDiv(pixels96, dc.GetDeviceCaps(LOGPIXELSX), 96);
}

CString FormatVec(const Vec3d& v, int decimals = 3) {
    CString out;
    out.Format(_T("(%.*f, %.*f, %.*f)"), decimals, v.x, decimals, v.y, decimals, v.z);
    return out;
}

/// @brief 帧循环暂停器。
///
/// 两件事都要它：进度回调里会抽消息队列，帧循环不能在这中间插进来画半个场景；而
/// 文件对话框是**系统**的模态框，它自己那圈消息循环照样会派发我们的 WM_TIMER ——
/// 自检模式下的「画满就关窗」会在对话框还开着的时候关掉主窗口，于是谁也退不出去。
class CFrameLoopPause {
public:
    explicit CFrameLoopPause(CWnd* frame) : frame_(frame) {
        if (frame_ && frame_->GetSafeHwnd()) {
            frame_->KillTimer(kFrameTimer);
        }
    }
    ~CFrameLoopPause() {
        if (frame_ && frame_->GetSafeHwnd()) {
            frame_->SetTimer(kFrameTimer, 16, nullptr);
        }
    }

    CFrameLoopPause(const CFrameLoopPause&) = delete;
    CFrameLoopPause& operator=(const CFrameLoopPause&) = delete;

private:
    CWnd* frame_;
};

} // namespace

// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_MESSAGE(WM_CADGEOM_LOG, &CMainFrame::OnLogMessage)
    ON_MESSAGE(WM_CADGEOM_VIEWPORT_FAILED, &CMainFrame::OnViewportFailedMessage)

    // -- 文件 ---------------------------------------------------------------
    ON_COMMAND(ID_FILE_NEW, &CMainFrame::OnFileNew)
    ON_COMMAND(ID_FILE_SAMPLE, &CMainFrame::OnFileSample)
    ON_COMMAND(ID_FILE_OPEN, &CMainFrame::OnFileOpen)
    ON_COMMAND(ID_FILE_IMPORT, &CMainFrame::OnFileImport)
    ON_COMMAND(ID_FILE_EXPORT, &CMainFrame::OnFileExport)
    ON_COMMAND(ID_FILE_EXPORT_SELECTION, &CMainFrame::OnFileExportSelection)
    ON_UPDATE_COMMAND_UI(ID_FILE_EXPORT_SELECTION, &CMainFrame::OnUpdateHasSelection)
    ON_COMMAND(ID_FILE_SCREENSHOT, &CMainFrame::OnFileScreenshot)
    ON_UPDATE_COMMAND_UI(ID_FILE_SCREENSHOT, &CMainFrame::OnUpdateHasViewport)

    // -- 编辑 ---------------------------------------------------------------
    ON_COMMAND(ID_EDIT_UNDO, &CMainFrame::OnEditUndo)
    ON_UPDATE_COMMAND_UI(ID_EDIT_UNDO, &CMainFrame::OnUpdateUndo)
    ON_COMMAND(ID_EDIT_REDO, &CMainFrame::OnEditRedo)
    ON_UPDATE_COMMAND_UI(ID_EDIT_REDO, &CMainFrame::OnUpdateRedo)
    ON_COMMAND(ID_EDIT_CLEAR, &CMainFrame::OnEditDelete)
    ON_UPDATE_COMMAND_UI(ID_EDIT_CLEAR, &CMainFrame::OnUpdateHasSelection)
    ON_COMMAND(ID_EDIT_SELECT_ALL, &CMainFrame::OnEditSelectAll)
    ON_COMMAND(ID_EDIT_DESELECT, &CMainFrame::OnEditDeselect)
    ON_COMMAND(ID_EDIT_RENAME, &CMainFrame::OnEditRename)
    ON_UPDATE_COMMAND_UI(ID_EDIT_RENAME, &CMainFrame::OnUpdateSingleSelection)
    ON_COMMAND(ID_EDIT_PARAMS, &CMainFrame::OnEditParams)
    ON_UPDATE_COMMAND_UI(ID_EDIT_PARAMS, &CMainFrame::OnUpdateSingleSelection)
    ON_COMMAND(ID_EDIT_COLOR, &CMainFrame::OnEditColor)
    ON_UPDATE_COMMAND_UI(ID_EDIT_COLOR, &CMainFrame::OnUpdateHasSelection)
    ON_COMMAND(ID_EDIT_STYLE, &CMainFrame::OnEditStyle)
    ON_UPDATE_COMMAND_UI(ID_EDIT_STYLE, &CMainFrame::OnUpdateHasSelection)
    ON_COMMAND(ID_EDIT_TRANSFORM, &CMainFrame::OnEditTransform)
    ON_UPDATE_COMMAND_UI(ID_EDIT_TRANSFORM, &CMainFrame::OnUpdateSingleSelection)
    ON_COMMAND(ID_EDIT_VISIBLE, &CMainFrame::OnEditVisible)
    ON_UPDATE_COMMAND_UI(ID_EDIT_VISIBLE, &CMainFrame::OnUpdateHasSelection)
    ON_COMMAND(ID_EDIT_GROUP, &CMainFrame::OnEditGroup)
    ON_UPDATE_COMMAND_UI(ID_EDIT_GROUP, &CMainFrame::OnUpdateHasSelection)
    ON_COMMAND(ID_EDIT_UNDO_CAPACITY, &CMainFrame::OnEditUndoCapacity)
    ON_COMMAND(ID_EDIT_CLEAR_HISTORY, &CMainFrame::OnEditClearHistory)
    ON_COMMAND_RANGE(ID_LINESTYLE_FIRST, ID_LINESTYLE_LAST, &CMainFrame::OnSetLineStyle)
    ON_UPDATE_COMMAND_UI_RANGE(ID_LINESTYLE_FIRST, ID_LINESTYLE_LAST,
                               &CMainFrame::OnUpdateHasSelection)

    // -- 创建 ---------------------------------------------------------------
    ON_COMMAND_RANGE(ID_TOOL_FIRST, ID_TOOL_LAST, &CMainFrame::OnActivateTool)
    ON_UPDATE_COMMAND_UI_RANGE(ID_TOOL_FIRST, ID_TOOL_LAST, &CMainFrame::OnUpdateTool)
    ON_COMMAND(ID_CREATE_ARC, &CMainFrame::OnCreateArc)
    ON_COMMAND_RANGE(ID_WORKPLANE_XY, ID_WORKPLANE_ZX, &CMainFrame::OnWorkPlaneAxis)
    ON_COMMAND(ID_WORKPLANE_FROM_PICK, &CMainFrame::OnWorkPlaneFromPick)
    ON_UPDATE_COMMAND_UI(ID_WORKPLANE_FROM_PICK, &CMainFrame::OnUpdateHasViewport)

    // -- 视图 ---------------------------------------------------------------
    ON_COMMAND_RANGE(ID_VIEW_STD_FIRST, ID_VIEW_STD_LAST, &CMainFrame::OnStandardView)
    ON_UPDATE_COMMAND_UI_RANGE(ID_VIEW_STD_FIRST, ID_VIEW_STD_LAST,
                               &CMainFrame::OnUpdateStandardView)
    ON_COMMAND(ID_VIEW_FIT, &CMainFrame::OnViewFit)
    ON_UPDATE_COMMAND_UI(ID_VIEW_FIT, &CMainFrame::OnUpdateHasViewport)
    ON_COMMAND(ID_VIEW_FIT_SELECTION, &CMainFrame::OnViewFitSelection)
    ON_UPDATE_COMMAND_UI(ID_VIEW_FIT_SELECTION, &CMainFrame::OnUpdateHasSelection)
    ON_COMMAND(ID_VIEW_PERSPECTIVE, &CMainFrame::OnViewPerspective)
    ON_UPDATE_COMMAND_UI(ID_VIEW_PERSPECTIVE, &CMainFrame::OnUpdatePerspective)
    ON_COMMAND_RANGE(ID_RENDER_FIRST, ID_RENDER_LAST, &CMainFrame::OnSetRenderMode)
    ON_UPDATE_COMMAND_UI_RANGE(ID_RENDER_FIRST, ID_RENDER_LAST, &CMainFrame::OnUpdateRenderMode)
    ON_COMMAND(ID_RENDER_CYCLE, &CMainFrame::OnCycleRenderMode)
    ON_UPDATE_COMMAND_UI(ID_RENDER_CYCLE, &CMainFrame::OnUpdateHasViewport)
    ON_COMMAND(ID_VIEW_GRID, &CMainFrame::OnViewGrid)
    ON_UPDATE_COMMAND_UI(ID_VIEW_GRID, &CMainFrame::OnUpdateGrid)
    ON_COMMAND(ID_VIEW_HUD, &CMainFrame::OnViewHud)
    ON_UPDATE_COMMAND_UI(ID_VIEW_HUD, &CMainFrame::OnUpdateHud)
    ON_COMMAND(ID_VIEW_BACKGROUND, &CMainFrame::OnViewBackground)
    ON_UPDATE_COMMAND_UI(ID_VIEW_BACKGROUND, &CMainFrame::OnUpdateHasViewport)
    ON_COMMAND(ID_VIEW_CAMERA, &CMainFrame::OnViewCamera)
    ON_UPDATE_COMMAND_UI(ID_VIEW_CAMERA, &CMainFrame::OnUpdateHasViewport)
    ON_COMMAND_RANGE(ID_PICK_FIRST, ID_PICK_LAST, &CMainFrame::OnPickFilter)
    ON_UPDATE_COMMAND_UI_RANGE(ID_PICK_FIRST, ID_PICK_LAST, &CMainFrame::OnUpdatePickFilter)
    ON_COMMAND(ID_VIEW_NEW_VIEWPORT, &CMainFrame::OnViewNewViewport)
    ON_COMMAND(ID_VIEW_CLOSE_EXTRA, &CMainFrame::OnViewCloseExtra)
    ON_UPDATE_COMMAND_UI(ID_VIEW_CLOSE_EXTRA, &CMainFrame::OnUpdateCloseExtra)
    ON_COMMAND(ID_VIEW_TREE_PANE, &CMainFrame::OnViewTreePane)
    ON_UPDATE_COMMAND_UI(ID_VIEW_TREE_PANE, &CMainFrame::OnUpdateTreePane)
    ON_COMMAND(ID_VIEW_LOG_PANE, &CMainFrame::OnViewLogPane)
    ON_UPDATE_COMMAND_UI(ID_VIEW_LOG_PANE, &CMainFrame::OnUpdateLogPane)
    // 工具栏和状态栏的显示 / 隐藏，框架自带的那两个处理函数就够了。
    ON_COMMAND_EX(ID_VIEW_TOOLBAR, &CMainFrame::OnBarCheck)
    ON_UPDATE_COMMAND_UI(ID_VIEW_TOOLBAR, &CMainFrame::OnUpdateControlBarMenu)
    ON_COMMAND_EX(ID_VIEW_STATUS_BAR, &CMainFrame::OnBarCheck)
    ON_UPDATE_COMMAND_UI(ID_VIEW_STATUS_BAR, &CMainFrame::OnUpdateControlBarMenu)

    // -- 捕捉 ---------------------------------------------------------------
    ON_COMMAND_RANGE(ID_SNAP_FIRST, ID_SNAP_LAST, &CMainFrame::OnSnapType)
    ON_UPDATE_COMMAND_UI_RANGE(ID_SNAP_FIRST, ID_SNAP_LAST, &CMainFrame::OnUpdateSnapType)
    ON_COMMAND(ID_SNAP_TOLERANCE, &CMainFrame::OnSnapTolerance)
    ON_COMMAND(ID_SNAP_CONTINUOUS, &CMainFrame::OnSnapContinuous)
    ON_UPDATE_COMMAND_UI(ID_SNAP_CONTINUOUS, &CMainFrame::OnUpdateContinuous)
    ON_COMMAND(ID_TESS_PARAMS, &CMainFrame::OnTessParams)

    // -- 测量 ---------------------------------------------------------------
    ON_COMMAND(ID_MEASURE_LAST_RESULT, &CMainFrame::OnMeasureLast)
    ON_COMMAND(ID_ENTITY_INFO, &CMainFrame::OnEntityInfo)
    ON_UPDATE_COMMAND_UI(ID_ENTITY_INFO, &CMainFrame::OnUpdateSingleSelection)
    ON_COMMAND(ID_UNIT_SETTINGS, &CMainFrame::OnUnitSettings)

    // -- 帮助 ---------------------------------------------------------------
    ON_COMMAND(ID_HELP_SHORTCUTS, &CMainFrame::OnHelpShortcuts)
    ON_COMMAND(ID_APP_ABOUT, &CMainFrame::OnAppAbout)
    ON_COMMAND_RANGE(ID_LOG_FIRST, ID_LOG_LAST, &CMainFrame::OnLogLevel)
    ON_UPDATE_COMMAND_UI_RANGE(ID_LOG_FIRST, ID_LOG_LAST, &CMainFrame::OnUpdateLogLevel)
END_MESSAGE_MAP()

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

CMainFrame::CMainFrame(ICadEngine& engine) : engine_(engine) {}

CMainFrame::~CMainFrame() = default;

int CMainFrame::OnCreate(LPCREATESTRUCT cs) {
    // 基类的 OnCreate 里会调 OnCreateClient()，分割条和三块面板就是在那儿建的。
    if (CFrameWnd::OnCreate(cs) == -1) {
        return -1;
    }

    // M6 之后的新能力都从扩展槽进来。拿到 null 说明这是个老库 —— 那不是崩溃，是
    // 一句「这个版本没有这件东西」，界面上把相关的项禁掉就是了。
    ext_ = static_cast<ICadEngine2*>(engine_.GetExtension(ExtensionId_Engine2));

    if (!CreateToolBarAndStatus()) {
        return -1;
    }
    RecalcLayout();

    // 现在才敢让日志槽叫醒我们：在此之前 m_hWnd 还没准备好收消息。
    LogSink::Instance().SetWakeWindow(m_hWnd);

    viewArea_.AddViewport(/*secondary=*/false);
    // 起来就是一张空图纸。示例图纸从「文件 → 载入示例图纸」或命令行的 --sample
    // 进来 —— 宿主替用户往场景里塞几何，是替他做了一个不该由程序做的决定。

    lastTick_ = ::GetTickCount64();
    SetTimer(kFrameTimer, 16, nullptr);
    return 0;
}

BOOL CMainFrame::OnCreateClient(LPCREATESTRUCT /*cs*/, CCreateContext* /*context*/) {
    // 没有文档 / 视图那一套，客户区就是两层静态分割条：
    //
    //     +----------+---------------------------+
    //     |          |        视口区             |
    //     | 模型树   +---------------------------+
    //     |          |        引擎日志           |
    //     +----------+---------------------------+
    //
    // CreateStatic 默认用 AFX_IDW_PANE_FIRST 做 id，而 CFrameWnd 正是按这个 id
    // 找「占满客户区的那个子窗口」，所以布局不用自己算。
    if (!splitMain_.CreateStatic(this, 1, 2)) {
        return FALSE;
    }
    if (!treePane_.CreateIn(&splitMain_, splitMain_.IdFromRowCol(0, 0))) {
        return FALSE;
    }
    if (!splitRight_.CreateStatic(&splitMain_, 2, 1, WS_CHILD | WS_VISIBLE,
                                  splitMain_.IdFromRowCol(0, 1))) {
        return FALSE;
    }
    if (!viewArea_.CreateIn(&splitRight_, splitRight_.IdFromRowCol(0, 0))) {
        return FALSE;
    }
    if (!logPane_.CreateIn(&splitRight_, splitRight_.IdFromRowCol(1, 0))) {
        return FALSE;
    }

    treeWidth_ = ScaledPixels(this, treeWidth_);
    logHeight_ = ScaledPixels(this, logHeight_);
    splitMain_.SetColumnInfo(0, treeWidth_, ScaledPixels(this, 80));
    splitMain_.SetColumnInfo(1, ScaledPixels(this, 1000), ScaledPixels(this, 240));
    splitRight_.SetRowInfo(0, ScaledPixels(this, 640), ScaledPixels(this, 120));
    splitRight_.SetRowInfo(1, logHeight_, ScaledPixels(this, 48));
    splitMain_.RecalcLayout();
    splitRight_.RecalcLayout();

    treePane_.SetOwner(this);
    viewArea_.Attach(&engine_, this);
    return TRUE;
}

BOOL CMainFrame::CreateToolBarAndStatus() {
    // 文字工具栏：没有位图资源，一份示例不值得为十几个图标去画一张位图，而文字
    // 反倒说得更清楚。按钮上因此只剩文字（TBSTYLE_LIST 让文字排在图像位置右边，
    // 而图像根本不存在）。
    if (!toolBar_.CreateEx(this, TBSTYLE_FLAT | TBSTYLE_LIST,
                           WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_TOOLTIPS | CBRS_FLYBY)) {
        return FALSE;
    }

    static const UINT kButtons[] = {
        ID_EDIT_UNDO,      ID_EDIT_REDO,    ID_SEPARATOR,
        ID_TOOL_SELECT,    ID_TOOL_POINT,   ID_TOOL_LINE,     ID_TOOL_CIRCLE,
        ID_TOOL_RECTANGLE, ID_TOOL_POLYLINE, ID_TOOL_EXTRUDE, ID_TOOL_MOVE,
        ID_TOOL_ROTATE,    ID_TOOL_SCALE,   ID_TOOL_MEASURE,  ID_SEPARATOR,
        ID_VIEW_PERSPECTIVE, ID_VIEW_GRID};
    if (!toolBar_.SetButtons(kButtons, _countof(kButtons))) {
        return FALSE;
    }

    static const TCHAR* const kLabels[] = {
        _T("撤销"), _T("重做"), nullptr,
        _T("选择"), _T("点"),   _T("直线"),   _T("圆"),
        _T("矩形"), _T("多段线"), _T("拉伸"), _T("移动"),
        _T("旋转"), _T("缩放"), _T("测量"),   nullptr,
        _T("透视"), _T("网格")};
    for (int i = 0; i < static_cast<int>(_countof(kLabels)); ++i) {
        if (kLabels[i]) {
            toolBar_.SetButtonText(i, kLabels[i]);
        }
    }
    // 图像尺寸给 1×1 而不是 0×0：0 是「删掉图像」的正规写法，但 CToolBar::SetSizes
    // 里有一句 ASSERT(sizeImage.cx > 0)，Debug 构建下会当场断言。1 个像素的图像位
    // 谁也看不见，而按钮尺寸的记账仍然由 CToolBar 自己算，布局不会错位。
    toolBar_.SetSizes(CSize(58, 26), CSize(1, 1));

    // 状态栏四格：提示语占大头，右边三格是常驻信息。
    static const UINT kIndicators[] = {ID_INDICATOR_PROMPT, ID_INDICATOR_SELECTION,
                                       ID_INDICATOR_MEASURE, ID_INDICATOR_DEVICE};
    if (!statusBar_.Create(this) || !statusBar_.SetIndicators(kIndicators, _countof(kIndicators))) {
        return FALSE;
    }
    statusBar_.SetPaneInfo(0, ID_INDICATOR_PROMPT, SBPS_STRETCH | SBPS_NOBORDERS, 0);
    statusBar_.SetPaneInfo(1, ID_INDICATOR_SELECTION, SBPS_NORMAL, ScaledPixels(this, 220));
    statusBar_.SetPaneInfo(2, ID_INDICATOR_MEASURE, SBPS_NORMAL, ScaledPixels(this, 160));
    statusBar_.SetPaneInfo(3, ID_INDICATOR_DEVICE, SBPS_NORMAL, ScaledPixels(this, 380));
    return TRUE;
}

void CMainFrame::OnDestroy() {
    KillTimer(kFrameTimer);
    // 窗口要没了，别再往一个死掉的 HWND 上投消息。
    LogSink::Instance().SetWakeWindow(nullptr);
    CFrameWnd::OnDestroy();
}

void CMainFrame::SetAutoShot(const CString& path, int frames) {
    autoShotPath_ = path;
    autoShotFrames_ = frames;
    frameCount_ = 0;
}

// ---------------------------------------------------------------------------
// 视口窗口回调
// ---------------------------------------------------------------------------

void CMainFrame::OnViewportReady(CViewportWnd* view) {
    IViewport* vp = view ? view->Viewport() : nullptr;
    if (!vp) {
        return;
    }
    if (view->IsSecondary()) {
        // 正视图 + 隐藏线：一眼看出视口之间只共享设备和几何，显示状态各自独立。
        vp->GetCamera()->SetStandardView(StandardView::Front);
        vp->SetRenderMode(RenderMode::HiddenLine);
    } else {
        vp->GetCamera()->SetStandardView(StandardView::Isometric);
    }
    vp->GetCamera()->ZoomToFit(nullptr, 1.25);

    if (!active_) {
        active_ = view;
    }
    CString text;
    text.Format(_T("视口就绪，设备：%s"), static_cast<LPCTSTR>(FromUtf8(engine_.GetDeviceName())));
    AppendLog(LogLevel::Info, text);
}

void CMainFrame::OnViewportFailed(CViewportWnd* /*view*/, const CString& reason) {
    // 这是在 WM_SIZE 的栈上发现的，别在那儿弹模态框：投一条消息给自己，回到消息
    // 循环再说。
    viewportFailure_ = reason;
    AppendLog(LogLevel::Error, reason);
    PostMessage(WM_CADGEOM_VIEWPORT_FAILED);
}

LRESULT CMainFrame::OnViewportFailedMessage(WPARAM /*wparam*/, LPARAM /*lparam*/) {
    if (viewportFailure_.IsEmpty()) {
        return 0;
    }
    KillTimer(kFrameTimer);
    CString text;
    text.Format(_T("引擎没能在这个窗口上建起视口：\n\n%s\n\n")
                _T("没有 Vulkan SDK 的构建里渲染器整块是不编的，此时 CreateViewport 会明说")
                _T("而不是给一个坏指针。"),
                static_cast<LPCTSTR>(viewportFailure_));
    viewportFailure_.Empty();
    MessageBox(text, _T("无法创建视口"), MB_OK | MB_ICONERROR);
    return 0;
}

void CMainFrame::OnViewportClosing(CViewportWnd* view) {
    if (active_ == view) {
        active_ = nullptr;
    }
}

void CMainFrame::SetActiveViewport(CViewportWnd* view) {
    if (view) {
        active_ = view;
    }
}

void CMainFrame::OnEngineConsumedInput() {
    SyncStatusBar();
}

// ---------------------------------------------------------------------------
// 每帧
// ---------------------------------------------------------------------------

void CMainFrame::OnTimer(UINT_PTR id) {
    if (id == kFrameTimer) {
        OnFrame();
        return;
    }
    CFrameWnd::OnTimer(id);
}

void CMainFrame::OnFrame() {
    const ULONGLONG now = ::GetTickCount64();
    const double delta = static_cast<double>(now - lastTick_) / 1000.0;
    lastTick_ = now;

    // Tick 每帧**只调一次**：脏几何解析一次、上传一次，之后每个视口各画各的。
    engine_.Tick(delta);
    viewArea_.RenderAll();

    DrainLog();
    RefreshTree();
    SyncTreeSelection();
    SyncStatusBar();

    // 有模态框开着时不数帧、更不关窗：关掉主窗口而模态框的消息循环还在，程序就
    // 卡在那儿谁也退不出去。主窗口被禁用，正是「它上面压着一个模态框」的意思。
    if (autoShotFrames_ > 0 && IsWindowEnabled() && ++frameCount_ >= autoShotFrames_) {
        autoShotFrames_ = 0;
        if (!autoShotPath_.IsEmpty()) {
            if (IViewport* vp = ActiveViewport()) {
                vp->SaveScreenshot(ToUtf8(autoShotPath_));
            }
        }
        PostMessage(WM_CLOSE);
    }
}

LRESULT CMainFrame::OnLogMessage(WPARAM /*wparam*/, LPARAM /*lparam*/) {
    DrainLog();
    return 0;
}

void CMainFrame::DrainLog() {
    LogRecord record;
    while (LogSink::Instance().Pop(record)) {
        logPane_.Append(record.level, record.text);
    }
}

void CMainFrame::AppendLog(LogLevel level, const CString& text) {
    // 走同一个队列，日志面板里的先后顺序才和引擎自己那些消息对得上。
    LogSink::Instance().Push(level, text);
}

void CMainFrame::SyncStatusBar() {
    IViewport* vp = ActiveViewport();

    // 工具写给状态栏的提示语。引擎自绘的 HUD 显示的是同一份文本的 ASCII 版本 ——
    // 宿主拿到的是原始字符串，想怎么排版都行。
    CString prompt;
    if (ext_) {
        prompt = FromUtf8(ext_->GetStatusText(vp));
    }
    if (prompt.IsEmpty()) {
        prompt = _T("就绪");
    }
    if (!flashText_.IsEmpty()) {
        if (::GetTickCount64() < flashUntil_) {
            prompt = flashText_;
        } else {
            flashText_.Empty();
        }
    }

    const ISelection* selection = engine_.GetScene()->GetSelection();
    CString selectionText;
    selectionText.Format(_T("选中 %u / %u"), selection->GetCount(),
                         engine_.GetScene()->GetEntityCount());
    if (selection->GetCount() == 1) {
        if (const IEntity* e = engine_.GetScene()->GetEntity(selection->GetAt(0))) {
            selectionText += _T("：");
            selectionText += FromUtf8(e->GetName());
        }
    }

    CString measureText = _T("测量 —");
    Vec3d from{};
    Vec3d to{};
    double distance = 0.0;
    if (ext_ && ext_->GetMeasurement(from, to, distance)) {
        measureText.Format(_T("测量 %s"), static_cast<LPCTSTR>(FormatLength(distance)));
    }

    CString deviceText = FromUtf8(engine_.GetDeviceName());
    if (ext_ && vp) {
        UnitSettings settings{};
        ext_->GetUnitSettings(settings);
        CString extra;
        extra.Format(_T(" · %u× MSAA · %s"), ext_->GetSampleCount(vp),
                     static_cast<LPCTSTR>(LengthUnitName(settings.displayUnit)));
        deviceText += extra;
    }

    // 只在变了的时候写：SetPaneText 每次都会重画那一格。
    if (prompt != statusPrompt_) {
        statusPrompt_ = prompt;
        statusBar_.SetPaneText(0, prompt);
    }
    if (selectionText != statusSelection_) {
        statusSelection_ = selectionText;
        statusBar_.SetPaneText(1, selectionText);
    }
    if (measureText != statusMeasure_) {
        statusMeasure_ = measureText;
        statusBar_.SetPaneText(2, measureText);
    }
    if (deviceText != statusDevice_) {
        statusDevice_ = deviceText;
        statusBar_.SetPaneText(3, deviceText);
    }
}

void CMainFrame::FlashMessage(const CString& text) {
    flashText_ = text;
    flashUntil_ = ::GetTickCount64() + 3000;
    SyncStatusBar();
}

// ---------------------------------------------------------------------------
// 模型树
// ---------------------------------------------------------------------------

namespace {

/// @brief 递归建一个树节点。EntityId 存在 item data 里，回头点树时要拿它。
/// @note SetItemData 收的是 DWORD_PTR，x64 上正好装得下 64 位的 EntityId。
void AddTreeItem(CTreeCtrl& tree, const IScene& scene, EntityId id, HTREEITEM parent) {
    const IEntity* entity = scene.GetEntity(id);
    if (!entity) {
        return;
    }
    // Win32 的树控件只有一列，名字和类型只好写在一起。
    CString label;
    label.Format(_T("%s  [%s]"), static_cast<LPCTSTR>(FromUtf8(entity->GetName())),
                 static_cast<LPCTSTR>(ShapeTypeName(entity->GetShapeType())));

    HTREEITEM item = tree.InsertItem(label, parent, TVI_LAST);
    tree.SetItemData(item, static_cast<DWORD_PTR>(id.value));
    tree.SetCheck(item, entity->IsVisible() ? TRUE : FALSE);

    for (uint32_t i = 0; i < entity->GetChildCount(); ++i) {
        AddTreeItem(tree, scene, entity->GetChild(i), item);
    }
    tree.Expand(item, TVE_EXPAND);
}

/// @brief 按 EntityId 找树节点。树不大，线性找就够了。
HTREEITEM FindTreeItem(CTreeCtrl& tree, HTREEITEM start, EntityId id) {
    for (HTREEITEM item = start; item != nullptr; item = tree.GetNextSiblingItem(item)) {
        if (tree.GetItemData(item) == static_cast<DWORD_PTR>(id.value)) {
            return item;
        }
        if (HTREEITEM child = tree.GetChildItem(item)) {
            if (HTREEITEM found = FindTreeItem(tree, child, id)) {
                return found;
            }
        }
    }
    return nullptr;
}

} // namespace

void CMainFrame::RefreshTree() {
    const uint64_t revision = engine_.GetScene()->GetRevision();
    if (haveRevision_ && revision == lastRevision_) {
        return;
    }
    lastRevision_ = revision;
    haveRevision_ = true;

    const IScene& scene = *engine_.GetScene();
    CTreeCtrl& tree = treePane_.Tree();

    syncingTree_ = true;
    tree.SetRedraw(FALSE);
    tree.DeleteAllItems();
    for (uint32_t i = 0; i < scene.GetRootCount(); ++i) {
        AddTreeItem(tree, scene, scene.GetRootAt(i), TVI_ROOT);
    }
    tree.SetRedraw(TRUE);
    syncingTree_ = false;

    // 树重建之后选中状态也没了，下一句 SyncTreeSelection 会补上。
    lastSelectionSignature_ = 0;
}

void CMainFrame::SyncTreeSelection() {
    const ISelection* selection = engine_.GetScene()->GetSelection();
    uint64_t signature = 1469598103934665603ull ^ selection->GetCount();
    for (uint32_t i = 0; i < selection->GetCount(); ++i) {
        signature = (signature ^ selection->GetAt(i).value) * 1099511628211ull;
    }
    if (signature == lastSelectionSignature_) {
        return;
    }
    lastSelectionSignature_ = signature;
    if (syncingTree_) {
        return;
    }

    CTreeCtrl& tree = treePane_.Tree();
    syncingTree_ = true;
    if (selection->GetCount() == 0) {
        tree.SelectItem(nullptr);
    } else {
        // Win32 的树控件没有多选，所以这里只跟到第一个 —— 多选照旧在视口里按住
        // Ctrl 点，状态栏那格会报「选中 N / M」。
        if (HTREEITEM item = FindTreeItem(tree, tree.GetRootItem(), selection->GetAt(0))) {
            tree.SelectItem(item);
            tree.EnsureVisible(item);
        }
    }
    syncingTree_ = false;
}

void CMainFrame::OnTreeSelectionChanged(HTREEITEM item) {
    if (syncingTree_ || !item) {
        return;
    }
    const EntityId id{static_cast<uint64_t>(treePane_.Tree().GetItemData(item))};
    syncingTree_ = true;
    if (IsValid(id)) {
        engine_.GetScene()->GetSelection()->Set(CgSpan<const EntityId>{&id, 1});
    }
    syncingTree_ = false;
}

void CMainFrame::OnTreeItemChecked(HTREEITEM item) {
    if (syncingTree_ || !item) {
        return;
    }
    CTreeCtrl& tree = treePane_.Tree();
    const EntityId id{static_cast<uint64_t>(tree.GetItemData(item))};
    const bool visible = tree.GetCheck(item) != FALSE;

    IEntity* entity = engine_.GetScene()->GetEntity(id);
    if (!entity || entity->IsVisible() == visible) {
        return;
    }
    // 走命令，不直接 SetVisible —— 那样撤销栈里就少了一步（HostCommands.h）。
    engine_.GetScene()->GetCommandStack()->Push(new StyleCommand(
        std::vector<EntityId>{id}, [visible](EntityStyle& style) { style.visible = visible; },
        visible ? "Show" : "Hide"));
}

// ---------------------------------------------------------------------------
// 文件
// ---------------------------------------------------------------------------

void CMainFrame::OnFileNew() {
    IScene* scene = engine_.GetScene();
    if (scene->GetEntityCount() > 0 &&
        MessageBox(_T("清空场景？撤销历史也会一起丢掉。"), _T("新建"),
                   MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    scene->Clear();
    AppendLog(LogLevel::Info, _T("场景已清空"));
}

void CMainFrame::OnFileSample() {
    LoadSampleScene();
}

void CMainFrame::LoadSampleScene() {
    BuildSampleScene(*engine_.GetScene());
    // 视口还没建起来时 ActiveCamera() 是空的，OnViewFit 自己认这一路 —— 那种情况
    // 下由 OnViewportReady 里的 ZoomToFit 收尾。
    OnViewFit();
}

CString CMainFrame::IoFilter(bool forImport) const {
    IIoRegistry* io = engine_.GetIoRegistry();

    CString all;
    CString entries;
    for (uint32_t i = 0; i < io->GetFormatCount(); ++i) {
        const char* ext = io->GetFormatExtension(i);
        if (!ext) {
            continue;
        }
        const bool usable = forImport ? io->CanImport(ext) : io->CanExport(ext);
        if (!usable) {
            continue;
        }
        CString pattern;
        pattern.Format(_T("*.%s"), static_cast<LPCTSTR>(FromUtf8(ext)));
        if (!all.IsEmpty()) {
            all += _T(";");
        }
        all += pattern;

        CString upper = FromUtf8(ext);
        upper.MakeUpper();
        CString entry;
        entry.Format(_T("%s (%s)|%s|"), static_cast<LPCTSTR>(upper), static_cast<LPCTSTR>(pattern),
                     static_cast<LPCTSTR>(pattern));
        entries += entry;
    }

    // CFileDialog 的过滤器是「描述|通配|描述|通配|」，最后要一个多出来的 '|'
    // 当结束符 —— MFC 会把它换成两个 '\0'。
    CString filter;
    if (!all.IsEmpty()) {
        filter.Format(_T("所有支持的格式 (%s)|%s|"), static_cast<LPCTSTR>(all),
                      static_cast<LPCTSTR>(all));
    }
    filter += entries;
    filter += _T("所有文件 (*.*)|*.*||");
    return filter;
}

void CMainFrame::OnFileOpen() {
    DoImport(/*merge=*/false);
}

void CMainFrame::OnFileImport() {
    DoImport(/*merge=*/true);
}

void CMainFrame::DoImport(bool merge) {
    CFrameLoopPause pause(this);

    CFileDialog dialog(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                       IoFilter(true), this);
    const CString title = merge ? _T("导入") : _T("打开");
    dialog.m_ofn.lpstrTitle = title;
    if (dialog.DoModal() != IDOK) {
        return;
    }
    const CString path = dialog.GetPathName();

    ImportOptions options{};
    options.mergeIntoScene = merge;
    options.readParametricExtras = true;

    CgResult result = CgResult::Ok;
    {
        // 进度框活着的时候主窗口是禁用的，所以出了这个作用域再弹别的框。
        CProgressDialog progress(this);
        progress.Show();
        progress.SetDlgItemText(IDC_PROGRESS_TEXT, _T("正在读取…"));
        result = engine_.GetIoRegistry()->Import(ToUtf8(path), options,
                                                 &CProgressDialog::ProgressCallback, &progress);
    }

    if (CgFailed(result)) {
        ReportError(_T("导入失败"));
        return;
    }
    CString text;
    text.Format(_T("已读入 %s，场景里现在有 %u 个对象"), static_cast<LPCTSTR>(path),
                engine_.GetScene()->GetEntityCount());
    AppendLog(LogLevel::Info, text);
    OnViewFit();
}

void CMainFrame::OnFileExport() {
    DoExport(/*selectionOnly=*/false);
}

void CMainFrame::OnFileExportSelection() {
    DoExport(/*selectionOnly=*/true);
}

void CMainFrame::DoExport(bool selectionOnly) {
    if (selectionOnly && engine_.GetScene()->GetSelection()->GetCount() == 0) {
        FlashMessage(_T("没有选中任何对象"));
        return;
    }
    CFrameLoopPause pause(this);

    CFileDialog dialog(FALSE, _T("glb"), _T("scene.glb"), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                       IoFilter(false), this);
    const CString title = selectionOnly ? _T("导出选中") : _T("导出");
    dialog.m_ofn.lpstrTitle = title;
    if (dialog.DoModal() != IDOK) {
        return;
    }
    const CString path = dialog.GetPathName();

    ExportOptions options{};
    options.selectionOnly = selectionOnly;
    // 参数写进 glTF 的 extras：我们自己的读取器能把圆读回成圆，别人看到的还是网格。
    options.writeParametricExtras = true;

    CgResult result = CgResult::Ok;
    {
        CProgressDialog progress(this);
        progress.Show();
        progress.SetDlgItemText(IDC_PROGRESS_TEXT, _T("正在写出…"));
        result = engine_.GetIoRegistry()->Export(ToUtf8(path), options,
                                                 &CProgressDialog::ProgressCallback, &progress);
    }

    if (CgFailed(result)) {
        ReportError(_T("导出失败"));
        return;
    }
    CString text;
    text.Format(_T("已写出 %s"), static_cast<LPCTSTR>(path));
    AppendLog(LogLevel::Info, text);
}

void CMainFrame::OnFileScreenshot() {
    IViewport* vp = ActiveViewport();
    if (!vp) {
        return;
    }
    CFrameLoopPause pause(this);

    CFileDialog dialog(FALSE, _T("png"), _T("cadgeom.png"),
                       OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, _T("PNG 图片 (*.png)|*.png||"),
                       this);
    if (dialog.DoModal() != IDOK) {
        return;
    }
    const CString path = dialog.GetPathName();

    // 截的是「最后一次呈现的那一帧」，所以先老老实实画一帧出来。
    engine_.Tick(0.0);
    vp->Render();
    if (CgFailed(vp->SaveScreenshot(ToUtf8(path)))) {
        ReportError(_T("截图失败"));
        return;
    }
    CString text;
    text.Format(_T("截图已保存到 %s"), static_cast<LPCTSTR>(path));
    AppendLog(LogLevel::Info, text);
}

// ---------------------------------------------------------------------------
// 编辑
// ---------------------------------------------------------------------------

std::vector<EntityId> CMainFrame::SelectedEntities() const {
    const ISelection* selection = engine_.GetScene()->GetSelection();
    std::vector<EntityId> out;
    out.reserve(selection->GetCount());
    for (uint32_t i = 0; i < selection->GetCount(); ++i) {
        out.push_back(selection->GetAt(i));
    }
    return out;
}

void CMainFrame::OnEditUndo() {
    ICommandStack* stack = engine_.GetScene()->GetCommandStack();
    const CString name = FromUtf8(stack->PeekUndoName());
    if (CgFailed(stack->Undo())) {
        ReportError(_T("撤销失败"));
        return;
    }
    FlashMessage(_T("已撤销 ") + name);
}

void CMainFrame::OnEditRedo() {
    ICommandStack* stack = engine_.GetScene()->GetCommandStack();
    const CString name = FromUtf8(stack->PeekRedoName());
    if (CgFailed(stack->Redo())) {
        ReportError(_T("重做失败"));
        return;
    }
    FlashMessage(_T("已重做 ") + name);
}

void CMainFrame::OnEditDelete() {
    IScene* scene = engine_.GetScene();
    const std::vector<EntityId> doomed = SelectedEntities();
    if (doomed.empty()) {
        return;
    }
    // 先清选择集：那些 id 马上就不存在了。
    scene->GetSelection()->Clear();
    if (CgFailed(scene->DestroyEntities(CgSpan<const EntityId>{doomed.data(), doomed.size()}))) {
        ReportError(_T("删除失败"));
    }
}

void CMainFrame::OnEditSelectAll() {
    IScene* scene = engine_.GetScene();
    std::vector<EntityId> all;
    all.reserve(scene->GetEntityCount());
    for (uint32_t i = 0; i < scene->GetEntityCount(); ++i) {
        all.push_back(scene->GetEntityAt(i));
    }
    scene->GetSelection()->Set(CgSpan<const EntityId>{all.data(), all.size()});
}

void CMainFrame::OnEditDeselect() {
    engine_.GetScene()->GetSelection()->Clear();
}

void CMainFrame::OnEditRename() {
    const std::vector<EntityId> targets = SelectedEntities();
    if (targets.size() != 1) {
        FlashMessage(_T("请先选中一个对象"));
        return;
    }
    IEntity* entity = engine_.GetScene()->GetEntity(targets.front());
    if (!entity) {
        return;
    }
    CString name = FromUtf8(entity->GetName());
    if (!CInputDialog::AskText(this, _T("重命名"), _T("名称"), name)) {
        return;
    }
    // 宿主 new 的命令，引擎执行、持有，最后调它的 Release()——分配与释放同侧。
    engine_.GetScene()->GetCommandStack()->Push(
        new RenameCommand(targets.front(), std::string(static_cast<LPCSTR>(ToUtf8(name)))));
}

void CMainFrame::OnEditParams() {
    const std::vector<EntityId> targets = SelectedEntities();
    if (targets.size() != 1) {
        FlashMessage(_T("请先选中一个对象"));
        return;
    }
    IScene* scene = engine_.GetScene();
    IEntity* entity = scene->GetEntity(targets.front());
    if (!entity) {
        return;
    }

    const ShapeType type = entity->GetShapeType();
    if (!CShapeParamsDialog::Supports(type)) {
        MessageBox(CShapeParamsDialog::UnsupportedReason(type), _T("无法编辑参数"),
                   MB_OK | MB_ICONINFORMATION);
        return;
    }

    ShapeParams params{};
    if (!scene->GetGeometryBuilder()->GetParams(targets.front(), params)) {
        ReportError(_T("读不出参数"));
        return;
    }

    CString title;
    title.Format(_T("编辑参数 — %s"), static_cast<LPCTSTR>(ShapeTypeName(type)));
    CShapeParamsDialog dialog(params, ext_, title, this);
    if (dialog.DoModal() != IDOK) {
        return;
    }
    // 改的是参数；网格是派生缓存，引擎自己会打脏标记重新细分。
    if (CgFailed(scene->GetGeometryBuilder()->SetParams(targets.front(), dialog.Params()))) {
        ReportError(_T("参数无效"));
    }
}

void CMainFrame::OnEditColor() {
    const std::vector<EntityId> targets = SelectedEntities();
    if (targets.empty()) {
        FlashMessage(_T("没有选中任何对象"));
        return;
    }
    COLORREF initial = RGB(255, 255, 255);
    if (IEntity* first = engine_.GetScene()->GetEntity(targets.front())) {
        EntityStyle style{};
        first->GetStyle(style);
        initial = ColorToRgb(style.color);
    }
    CColorDialog dialog(initial, CC_FULLOPEN | CC_ANYCOLOR, this);
    if (dialog.DoModal() != IDOK) {
        return;
    }
    const Color linear = ColorFromRgb(dialog.GetColor());
    engine_.GetScene()->GetCommandStack()->Push(new StyleCommand(
        targets, [linear](EntityStyle& style) { style.color = linear; }, "Set colour"));
}

void CMainFrame::OnEditStyle() {
    const std::vector<EntityId> targets = SelectedEntities();
    if (targets.empty()) {
        FlashMessage(_T("没有选中任何对象"));
        return;
    }
    // 表单从第一个选中对象读初值；确定之后整份样式套到所有选中对象上。
    EntityStyle current{};
    if (IEntity* first = engine_.GetScene()->GetEntity(targets.front())) {
        first->GetStyle(current);
    }

    CEntityStyleDialog dialog(current, this);
    if (dialog.DoModal() != IDOK) {
        return;
    }
    const EntityStyle next = dialog.Style();
    engine_.GetScene()->GetCommandStack()->Push(
        new StyleCommand(targets, [next](EntityStyle& style) { style = next; }, "Set style"));
}

void CMainFrame::OnEditTransform() {
    const std::vector<EntityId> targets = SelectedEntities();
    if (targets.size() != 1) {
        FlashMessage(_T("请先选中一个对象"));
        return;
    }
    IEntity* entity = engine_.GetScene()->GetEntity(targets.front());
    if (!entity) {
        return;
    }
    cadgeom::Transform local{};
    entity->GetLocalTransform(local);

    CTransformDialog dialog(local, ext_, this);
    if (dialog.DoModal() == IDOK) {
        engine_.GetScene()->GetCommandStack()->Push(
            new TransformCommand(targets.front(), dialog.Transform()));
    }
}

void CMainFrame::OnEditVisible() {
    const std::vector<EntityId> targets = SelectedEntities();
    if (targets.empty()) {
        return;
    }
    // 只要还有一个看得见，这一下就是「藏起来」。
    bool anyVisible = false;
    for (const EntityId id : targets) {
        if (IEntity* e = engine_.GetScene()->GetEntity(id)) {
            anyVisible = anyVisible || e->IsVisible();
        }
    }
    const bool target = !anyVisible;
    engine_.GetScene()->GetCommandStack()->Push(
        new StyleCommand(targets, [target](EntityStyle& s) { s.visible = target; },
                         target ? "Show" : "Hide"));
}

void CMainFrame::OnEditGroup() {
    const std::vector<EntityId> targets = SelectedEntities();
    if (targets.empty()) {
        FlashMessage(_T("没有选中任何对象"));
        return;
    }
    CString name = _T("Group");
    if (!CInputDialog::AskText(this, _T("编组"), _T("组名"), name) || name.IsEmpty()) {
        return;
    }
    engine_.GetScene()->GetCommandStack()->Push(
        new GroupCommand(targets, std::string(static_cast<LPCSTR>(ToUtf8(name)))));
}

void CMainFrame::OnEditUndoCapacity() {
    int capacity = 0;
    if (CInputDialog::AskInt(this, _T("撤销栈容量"), _T("最多保留多少步（0 表示不限）"), capacity,
                             0, 100000)) {
        // 超出容量时最老的那些先掉出去。
        engine_.GetScene()->GetCommandStack()->SetCapacity(static_cast<uint32_t>(capacity));
    }
}

void CMainFrame::OnEditClearHistory() {
    if (MessageBox(_T("撤销和重做都会清空，场景本身不动。"), _T("清空撤销历史"),
                   MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    engine_.GetScene()->GetCommandStack()->Clear();
}

void CMainFrame::OnSetLineStyle(UINT id) {
    const std::vector<EntityId> targets = SelectedEntities();
    if (targets.empty()) {
        FlashMessage(_T("没有选中任何对象"));
        return;
    }
    const LineStyle style = static_cast<LineStyle>(id - ID_LINESTYLE_FIRST);
    engine_.GetScene()->GetCommandStack()->Push(
        new StyleCommand(targets, [style](EntityStyle& s) { s.lineStyle = style; },
                         "Set line style"));
}

// ---------------------------------------------------------------------------
// 创建
// ---------------------------------------------------------------------------

void CMainFrame::PrimeToolContext() {
    IViewport* vp = ActiveViewport();
    if (!vp || !active_) {
        return;
    }
    double x = 0.0;
    double y = 0.0;
    active_->LastCursorPixel(x, y);
    MouseEvent e{};
    e.button = MouseButton::None;
    e.action = MouseAction::Move;
    e.x = x;
    e.y = y;
    vp->OnMouseEvent(e);
}

void CMainFrame::OnActivateTool(UINT id) {
    const int index = static_cast<int>(id - ID_TOOL_FIRST);
    if (index < 0 || index >= static_cast<int>(_countof(kToolTable))) {
        return;
    }
    PrimeToolContext();
    if (CgFailed(engine_.GetToolManager()->Activate(kToolTable[index]))) {
        ReportError(_T("切换工具失败"));
        return;
    }
    // 把焦点还给视口，否则接下来的 Esc / Enter 会被别的控件吃掉。
    if (active_) {
        active_->SetFocus();
    }
}

void CMainFrame::OnCreateArc() {
    IViewport* vp = ActiveViewport();
    if (!vp) {
        return;
    }
    WorkPlane plane{};
    vp->GetWorkPlane(plane);

    ShapeParams params{};
    params.type = ShapeType::Arc;
    params.arc.plane = Plane{plane.origin, plane.normal};
    params.arc.radius = 25.0;
    params.arc.startAngle = 0.0;
    params.arc.sweepAngle = kPi * 0.5;

    CShapeParamsDialog dialog(params, ext_, _T("按参数创建圆弧"), this);
    if (dialog.DoModal() != IDOK) {
        return;
    }
    const ShapeParams edited = dialog.Params();
    const EntityId created = engine_.GetScene()->GetGeometryBuilder()->MakeArc(
        edited.arc.plane, edited.arc.radius, edited.arc.startAngle, edited.arc.sweepAngle);
    if (!IsValid(created)) {
        ReportError(_T("创建圆弧失败"));
    }
}

void CMainFrame::OnWorkPlaneAxis(UINT id) {
    IViewport* vp = ActiveViewport();
    if (!vp) {
        return;
    }
    WorkPlane plane{};
    plane.origin = Vec3d{0.0, 0.0, 0.0};
    switch (id) {
        case ID_WORKPLANE_YZ:
            plane.normal = Vec3d{1, 0, 0};
            plane.uAxis = Vec3d{0, 1, 0};
            plane.vAxis = Vec3d{0, 0, 1};
            break;
        case ID_WORKPLANE_ZX:
            plane.normal = Vec3d{0, 1, 0};
            plane.uAxis = Vec3d{0, 0, 1};
            plane.vAxis = Vec3d{1, 0, 0};
            break;
        default:  // XY
            plane.normal = Vec3d{0, 0, 1};
            plane.uAxis = Vec3d{1, 0, 0};
            plane.vAxis = Vec3d{0, 1, 0};
            break;
    }
    vp->SetWorkPlane(plane);
    FlashMessage(_T("工作平面已重设"));
}

void CMainFrame::OnWorkPlaneFromPick() {
    IViewport* vp = ActiveViewport();
    if (!vp || !active_) {
        return;
    }
    double x = 0.0;
    double y = 0.0;
    active_->LastCursorPixel(x, y);

    PickResult pick{};
    if (!vp->Pick(x, y, PickFilter_All, pick)) {
        FlashMessage(_T("光标下没有东西可拾取"));
        return;
    }
    if (CgFailed(vp->SetWorkPlaneFromPick(pick))) {
        ReportError(_T("这个命中点定不出平面"));
        return;
    }
    FlashMessage(_T("工作平面已落在 ") + FormatVec(pick.point) + _T(" 上"));
}

// ---------------------------------------------------------------------------
// 视图
// ---------------------------------------------------------------------------

IViewport* CMainFrame::ActiveViewport() const {
    if (active_) {
        return active_->Viewport();
    }
    // 还没有人碰过任何一个视口 —— 就用第一个。
    if (CViewportWnd* first = viewArea_.At(0)) {
        return first->Viewport();
    }
    return nullptr;
}

ICamera* CMainFrame::ActiveCamera() const {
    IViewport* vp = ActiveViewport();
    return vp ? vp->GetCamera() : nullptr;
}

void CMainFrame::OnStandardView(UINT id) {
    const int index = static_cast<int>(id - ID_VIEW_STD_FIRST);
    if (index < 0 || index >= static_cast<int>(_countof(kViewTable))) {
        return;
    }
    if (ICamera* camera = ActiveCamera()) {
        camera->SetStandardView(kViewTable[index]);
    }
}

void CMainFrame::OnViewFit() {
    ICamera* camera = ActiveCamera();
    if (!camera) {
        return;
    }
    Aabb bounds{};
    // F 的习惯：有选中就框选中的，没有就框整个场景。
    if (engine_.GetScene()->GetSelection()->GetBounds(bounds)) {
        camera->ZoomToFit(&bounds, 1.25);
        return;
    }
    camera->ZoomToFit(nullptr, 1.25);
}

void CMainFrame::OnViewFitSelection() {
    ICamera* camera = ActiveCamera();
    if (!camera) {
        return;
    }
    Aabb bounds{};
    if (!engine_.GetScene()->GetSelection()->GetBounds(bounds)) {
        FlashMessage(_T("没有选中任何对象"));
        return;
    }
    camera->ZoomToFit(&bounds, 1.25);
}

void CMainFrame::OnViewPerspective() {
    if (ICamera* camera = ActiveCamera()) {
        camera->SetProjection(camera->GetProjection() == ProjectionMode::Orthographic
                                  ? ProjectionMode::Perspective
                                  : ProjectionMode::Orthographic);
    }
}

void CMainFrame::OnSetRenderMode(UINT id) {
    if (IViewport* vp = ActiveViewport()) {
        vp->SetRenderMode(static_cast<RenderMode>(id - ID_RENDER_FIRST));
    }
}

void CMainFrame::OnCycleRenderMode() {
    IViewport* vp = ActiveViewport();
    if (!vp) {
        return;
    }
    const int next = (static_cast<int>(vp->GetRenderMode()) + 1) % 4;
    vp->SetRenderMode(static_cast<RenderMode>(next));
}

void CMainFrame::OnViewGrid() {
    if (IViewport* vp = ActiveViewport()) {
        vp->SetGridVisible(!vp->IsGridVisible());
    }
}

void CMainFrame::OnViewHud() {
    IViewport* vp = ActiveViewport();
    if (!ext_ || !vp) {
        return;
    }
    ext_->SetHudVisible(vp, !ext_->IsHudVisible(vp));
}

void CMainFrame::OnViewBackground() {
    IViewport* vp = ActiveViewport();
    if (!vp) {
        return;
    }
    CColorDialog dialog(RGB(20, 22, 26), CC_FULLOPEN | CC_ANYCOLOR, this);
    if (dialog.DoModal() == IDOK) {
        vp->SetBackgroundColor(ColorFromRgb(dialog.GetColor()));
    }
}

void CMainFrame::OnViewCamera() {
    ICamera* camera = ActiveCamera();
    if (!camera) {
        return;
    }
    CCameraDialog dialog(*camera, ext_, this);
    if (dialog.DoModal() == IDOK) {
        dialog.ApplyTo(*camera);
    }
}

void CMainFrame::OnPickFilter(UINT id) {
    IViewport* vp = ActiveViewport();
    if (!vp) {
        return;
    }
    const int index = static_cast<int>(id - ID_PICK_FIRST);
    if (index < 0 || index >= static_cast<int>(_countof(kPickTable))) {
        return;
    }
    // 菜单项是开关，勾选状态从视口自己那儿读，所以这里是「异或一位」。
    vp->SetPickFilter(vp->GetPickFilter() ^ kPickTable[index]);
}

void CMainFrame::OnViewNewViewport() {
    if (viewArea_.Count() >= 4) {
        FlashMessage(_T("视口开得够多了"));
        return;
    }
    viewArea_.AddViewport(/*secondary=*/true);
}

void CMainFrame::OnViewCloseExtra() {
    viewArea_.CloseExtraViewports();
    if (!active_) {
        active_ = viewArea_.At(0);
    }
}

void CMainFrame::OnViewTreePane() {
    treeVisible_ = !treeVisible_;
    if (!treeVisible_) {
        int current = 0;
        int minimum = 0;
        splitMain_.GetColumnInfo(0, current, minimum);
        if (current > 0) {
            treeWidth_ = current;
        }
        splitMain_.SetColumnInfo(0, 0, 0);
    } else {
        splitMain_.SetColumnInfo(0, treeWidth_, ScaledPixels(this, 80));
    }
    splitMain_.RecalcLayout();
}

void CMainFrame::OnViewLogPane() {
    logVisible_ = !logVisible_;
    if (!logVisible_) {
        int current = 0;
        int minimum = 0;
        splitRight_.GetRowInfo(1, current, minimum);
        if (current > 0) {
            logHeight_ = current;
        }
        splitRight_.SetRowInfo(1, 0, 0);
    } else {
        splitRight_.SetRowInfo(1, logHeight_, ScaledPixels(this, 48));
    }
    splitRight_.RecalcLayout();
}

// ---------------------------------------------------------------------------
// 捕捉 / 精度
// ---------------------------------------------------------------------------

void CMainFrame::OnSnapType(UINT id) {
    const int index = static_cast<int>(id - ID_SNAP_FIRST);
    if (index < 0 || index >= static_cast<int>(_countof(kSnapTable))) {
        return;
    }
    IToolManager* tools = engine_.GetToolManager();
    tools->SetSnapMask(tools->GetSnapMask() ^ kSnapTable[index]);
}

void CMainFrame::OnSnapTolerance() {
    double pixels = 8.0;
    if (CInputDialog::AskDouble(this, _T("捕捉半径"), _T("像素"), pixels, 1.0, 64.0)) {
        engine_.GetToolManager()->SetSnapTolerance(pixels);
    }
}

void CMainFrame::OnSnapContinuous() {
    IToolManager* tools = engine_.GetToolManager();
    tools->SetContinuousMode(!tools->IsContinuousMode());
}

void CMainFrame::OnTessParams() {
    IGeometryBuilder* builder = engine_.GetScene()->GetGeometryBuilder();
    TessParams params{};
    builder->GetTessParams(params);

    CTessParamsDialog dialog(params, ext_, this);
    if (dialog.DoModal() == IDOK) {
        builder->SetTessParams(dialog.Params());
    }
}

// ---------------------------------------------------------------------------
// 测量
// ---------------------------------------------------------------------------

CString CMainFrame::FormatLength(double modelUnits) const {
    if (!ext_) {
        CString out;
        out.Format(_T("%.3f"), modelUnits);
        return out;
    }
    char buffer[64] = {};
    ext_->FormatLength(modelUnits, buffer, sizeof(buffer));
    return FromUtf8(buffer);
}

void CMainFrame::OnMeasureLast() {
    Vec3d from{};
    Vec3d to{};
    double distance = 0.0;
    if (!ext_ || !ext_->GetMeasurement(from, to, distance)) {
        MessageBox(_T("还没量过任何东西。按 D 切到测量工具，点两个点。"), _T("测量"),
                   MB_OK | MB_ICONINFORMATION);
        return;
    }
    const Vec3d delta{to.x - from.x, to.y - from.y, to.z - from.z};
    CString text;
    text.Format(_T("起点：%s\n终点：%s\n\n距离：%s\nΔX：%s\nΔY：%s\nΔZ：%s"),
                static_cast<LPCTSTR>(FormatVec(from)), static_cast<LPCTSTR>(FormatVec(to)),
                static_cast<LPCTSTR>(FormatLength(distance)),
                static_cast<LPCTSTR>(FormatLength(delta.x)),
                static_cast<LPCTSTR>(FormatLength(delta.y)),
                static_cast<LPCTSTR>(FormatLength(delta.z)));
    MessageBox(text, _T("最近一次测量"), MB_OK | MB_ICONINFORMATION);
}

void CMainFrame::OnEntityInfo() {
    const std::vector<EntityId> targets = SelectedEntities();
    if (targets.size() != 1) {
        FlashMessage(_T("请先选中一个对象"));
        return;
    }
    IEntity* entity = engine_.GetScene()->GetEntity(targets.front());
    if (!entity) {
        return;
    }

    CString text;
    text.Format(_T("名称：%s\n类型：%s\nEntityId：%llu\nShapeId：%llu\n可见：%s"),
                static_cast<LPCTSTR>(FromUtf8(entity->GetName())),
                static_cast<LPCTSTR>(ShapeTypeName(entity->GetShapeType())),
                static_cast<unsigned long long>(entity->GetId().value),
                static_cast<unsigned long long>(entity->GetShape().value),
                entity->IsVisible() ? _T("是") : _T("否"));

    Aabb bounds{};
    if (entity->GetWorldBounds(bounds)) {
        CString box;
        box.Format(_T("\n\n包围盒：\n  min %s\n  max %s\n  尺寸 %s × %s × %s"),
                   static_cast<LPCTSTR>(FormatVec(bounds.min)),
                   static_cast<LPCTSTR>(FormatVec(bounds.max)),
                   static_cast<LPCTSTR>(FormatLength(bounds.max.x - bounds.min.x)),
                   static_cast<LPCTSTR>(FormatLength(bounds.max.y - bounds.min.y)),
                   static_cast<LPCTSTR>(FormatLength(bounds.max.z - bounds.min.z)));
        text += box;
    }

    cadgeom::Transform local{};
    entity->GetLocalTransform(local);
    CString transform;
    transform.Format(_T("\n\n局部变换：\n  平移 %s\n  缩放 %s"),
                     static_cast<LPCTSTR>(FormatVec(local.translation)),
                     static_cast<LPCTSTR>(FormatVec(local.scale)));
    text += transform;

    MessageBox(text, _T("对象信息"), MB_OK | MB_ICONINFORMATION);
}

void CMainFrame::OnUnitSettings() {
    if (!ext_) {
        return;
    }
    UnitSettings settings{};
    ext_->GetUnitSettings(settings);

    CUnitSettingsDialog dialog(settings, this);
    if (dialog.DoModal() == IDOK) {
        ext_->SetUnitSettings(dialog.Settings());
    }
}

// ---------------------------------------------------------------------------
// 帮助
// ---------------------------------------------------------------------------

void CMainFrame::OnHelpShortcuts() {
    CFrameLoopPause pause(this);
    ShowShortcutsDialog(this);
}

void CMainFrame::OnAppAbout() {
    CFrameLoopPause pause(this);
    ShowAboutDialog(this, engine_, ext_, ActiveViewport());
}

void CMainFrame::OnLogLevel(UINT id) {
    engine_.SetLogLevel(static_cast<LogLevel>(id - ID_LOG_FIRST));
}

// ---------------------------------------------------------------------------
// 界面状态
//
// MFC 的 ON_UPDATE_COMMAND_UI：菜单弹出前和空闲时框架各问一遍，菜单项和工具栏
// 按钮共用同一个回答。qt_viewer 那边是每帧手动同步一遍 QAction。
// ---------------------------------------------------------------------------

void CMainFrame::OnUpdateUndo(CCmdUI* cmdUI) {
    ICommandStack* stack = engine_.GetScene()->GetCommandStack();
    cmdUI->Enable(stack->CanUndo());
    // 只给菜单项改文字。同一个 id 在工具栏上也有一个按钮，给它塞上「撤销 Extrude
    // \tCtrl+Z」会把那颗按钮撑变形 —— m_pMenu 非空就是「这次问的是菜单」。
    if (cmdUI->m_pMenu != nullptr) {
        const char* name = stack->PeekUndoName();
        cmdUI->SetText(name ? _T("撤销 ") + FromUtf8(name) + _T("\tCtrl+Z")
                            : CString(_T("撤销\tCtrl+Z")));
    }
}

void CMainFrame::OnUpdateRedo(CCmdUI* cmdUI) {
    ICommandStack* stack = engine_.GetScene()->GetCommandStack();
    cmdUI->Enable(stack->CanRedo());
    if (cmdUI->m_pMenu != nullptr) {
        const char* name = stack->PeekRedoName();
        cmdUI->SetText(name ? _T("重做 ") + FromUtf8(name) + _T("\tCtrl+Y")
                            : CString(_T("重做\tCtrl+Y")));
    }
}

void CMainFrame::OnUpdateHasSelection(CCmdUI* cmdUI) {
    cmdUI->Enable(engine_.GetScene()->GetSelection()->GetCount() > 0);
}

void CMainFrame::OnUpdateSingleSelection(CCmdUI* cmdUI) {
    cmdUI->Enable(engine_.GetScene()->GetSelection()->GetCount() == 1);
}

void CMainFrame::OnUpdateHasViewport(CCmdUI* cmdUI) {
    cmdUI->Enable(ActiveViewport() != nullptr);
}

void CMainFrame::OnUpdateTool(CCmdUI* cmdUI) {
    const int index = static_cast<int>(cmdUI->m_nID - ID_TOOL_FIRST);
    if (index < 0 || index >= static_cast<int>(_countof(kToolTable))) {
        return;
    }
    cmdUI->SetCheck(engine_.GetToolManager()->GetActiveTool() == kToolTable[index] ? 1 : 0);
}

void CMainFrame::OnUpdateStandardView(CCmdUI* cmdUI) {
    cmdUI->Enable(ActiveCamera() != nullptr);
}

void CMainFrame::OnUpdateRenderMode(CCmdUI* cmdUI) {
    IViewport* vp = ActiveViewport();
    cmdUI->Enable(vp != nullptr);
    if (vp) {
        cmdUI->SetCheck(static_cast<UINT>(vp->GetRenderMode()) == cmdUI->m_nID - ID_RENDER_FIRST
                            ? 1
                            : 0);
    }
}

void CMainFrame::OnUpdateSnapType(CCmdUI* cmdUI) {
    const int index = static_cast<int>(cmdUI->m_nID - ID_SNAP_FIRST);
    if (index < 0 || index >= static_cast<int>(_countof(kSnapTable))) {
        return;
    }
    cmdUI->SetCheck((engine_.GetToolManager()->GetSnapMask() & kSnapTable[index]) != 0 ? 1 : 0);
}

void CMainFrame::OnUpdatePickFilter(CCmdUI* cmdUI) {
    IViewport* vp = ActiveViewport();
    cmdUI->Enable(vp != nullptr);
    const int index = static_cast<int>(cmdUI->m_nID - ID_PICK_FIRST);
    if (vp && index >= 0 && index < static_cast<int>(_countof(kPickTable))) {
        cmdUI->SetCheck((vp->GetPickFilter() & kPickTable[index]) != 0 ? 1 : 0);
    }
}

void CMainFrame::OnUpdateLogLevel(CCmdUI* cmdUI) {
    cmdUI->SetCheck(static_cast<UINT>(engine_.GetLogLevel()) == cmdUI->m_nID - ID_LOG_FIRST ? 1
                                                                                            : 0);
}

void CMainFrame::OnUpdateGrid(CCmdUI* cmdUI) {
    IViewport* vp = ActiveViewport();
    cmdUI->Enable(vp != nullptr);
    cmdUI->SetCheck(vp && vp->IsGridVisible() ? 1 : 0);
}

void CMainFrame::OnUpdateHud(CCmdUI* cmdUI) {
    IViewport* vp = ActiveViewport();
    cmdUI->Enable(vp != nullptr && ext_ != nullptr);
    cmdUI->SetCheck(ext_ && vp && ext_->IsHudVisible(vp) ? 1 : 0);
}

void CMainFrame::OnUpdatePerspective(CCmdUI* cmdUI) {
    ICamera* camera = ActiveCamera();
    cmdUI->Enable(camera != nullptr);
    cmdUI->SetCheck(camera && camera->GetProjection() == ProjectionMode::Perspective ? 1 : 0);
}

void CMainFrame::OnUpdateContinuous(CCmdUI* cmdUI) {
    cmdUI->SetCheck(engine_.GetToolManager()->IsContinuousMode() ? 1 : 0);
}

void CMainFrame::OnUpdateCloseExtra(CCmdUI* cmdUI) {
    cmdUI->Enable(viewArea_.Count() > 1);
}

void CMainFrame::OnUpdateTreePane(CCmdUI* cmdUI) {
    cmdUI->SetCheck(treeVisible_ ? 1 : 0);
}

void CMainFrame::OnUpdateLogPane(CCmdUI* cmdUI) {
    cmdUI->SetCheck(logVisible_ ? 1 : 0);
}

// ---------------------------------------------------------------------------
// 杂项
// ---------------------------------------------------------------------------

void CMainFrame::ReportError(const CString& what) {
    const CString detail = FromUtf8(engine_.GetLastErrorMessage());
    AppendLog(LogLevel::Error, what + _T("：") + detail);
    MessageBox(what + _T("\n\n") + detail, _T("CadGeom"), MB_OK | MB_ICONWARNING);
    engine_.ClearLastError();
}
