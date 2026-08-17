/**
 * @file MainFrame.h
 * @brief 宿主主窗口：菜单、工具栏、模型树、日志、状态栏，外加那圈帧循环。
 *
 * 分工是这样的 ——
 *
 *   * **帧循环归主窗口**。一个 16ms 的 WM_TIMER 每帧调一次 ICadEngine::Tick()，
 *     然后让每个视口各画一帧。Tick 必须只调一次：脏几何解析一次、上传一次，剩下
 *     的才是每个视口自己那份相机与显示状态（docs/architecture.md §4.1）。
 *   * **快捷键归 MFC**。引擎自己也认一套字母键，但那是给 SurfaceKind::Glfw 那种
 *     「引擎拥有窗口」的场合准备的。这里窗口是宿主的，键就该由 ACCELERATORS 表
 *     领走，菜单上才写得出提示。没被菜单认领的键（Esc、Enter）才落到视口窗口手里
 *     转发给引擎 —— 那几个恰好是工具自己要的。
 *   * **面板归宿主**。引擎自绘的 HUD 只画 ASCII 笔画字，中文面板得自己做；数据
 *     从 ICadEngine2 拿：GetStatusText / FormatLength / GetMeasurement。
 *
 * 和 qt_viewer 比，最省事的一处是菜单项的启停与勾选：Qt 那边每帧手动同步一遍
 * QAction，MFC 有 ON_UPDATE_COMMAND_UI —— 框架在空闲时和菜单弹出前替你问一遍，
 * 菜单和工具栏共用同一个回答。
 */
#ifndef CADGEOM_MFC_VIEWER_MAINFRAME_H
#define CADGEOM_MFC_VIEWER_MAINFRAME_H

#include <afxext.h>
#include <afxwin.h>

#include <cadgeom/CadGeom.h>

#include "Panes.h"

#include <vector>

class CViewportWnd;

/// @brief 一个能把引擎的能力挨个点一遍的宿主窗口。
class CMainFrame : public CFrameWnd {
public:
    /// @param engine 引擎；窗口不持有它，**它必须活得比窗口久**。CWinApp 先建引擎
    ///               后建窗口，退出时窗口先没、引擎后放，正好满足。
    explicit CMainFrame(cadgeom::ICadEngine& engine);
    ~CMainFrame() override;

    /// @brief 自检用：画满 `frames` 帧之后存一张图、关窗口。
    /// @param path   截图路径；空字符串表示只跑帧数不存图。
    /// @param frames 帧数，<= 0 表示不启用。
    /// @note 和 glfw demo 的 `--headless --screenshot` 是同一个用意 —— 一次渲染
    ///       改动能不能在**嵌入宿主窗口**的那条路径上跑通，总得有个不靠手的验法。
    void SetAutoShot(const CString& path, int frames);

    /// @brief 把内置的示例图纸画进场景，然后框住它。
    /// @note 窗口**不会**自己调这个 —— 起来就是一张空图纸，画什么由用户决定。菜单
    ///       里的「载入示例图纸」和命令行的 `--sample` 是它仅有的两个入口。
    void LoadSampleScene();

    // -- 视口窗口回调 -------------------------------------------------------

    void OnViewportReady(CViewportWnd* view);
    void OnViewportFailed(CViewportWnd* view, const CString& reason);
    void OnViewportClosing(CViewportWnd* view);
    void SetActiveViewport(CViewportWnd* view);
    void OnEngineConsumedInput();

    // -- 模型树回调 ---------------------------------------------------------

    void OnTreeSelectionChanged(HTREEITEM item);
    void OnTreeItemChecked(HTREEITEM item);

protected:
    BOOL OnCreateClient(LPCREATESTRUCT cs, CCreateContext* context) override;

    afx_msg int OnCreate(LPCREATESTRUCT cs);
    afx_msg void OnDestroy();
    afx_msg void OnTimer(UINT_PTR id);
    afx_msg LRESULT OnLogMessage(WPARAM wparam, LPARAM lparam);
    afx_msg LRESULT OnViewportFailedMessage(WPARAM wparam, LPARAM lparam);

    // -- 文件 ---------------------------------------------------------------
    afx_msg void OnFileNew();
    afx_msg void OnFileSample();
    afx_msg void OnFileOpen();
    afx_msg void OnFileImport();
    afx_msg void OnFileExport();
    afx_msg void OnFileExportSelection();
    afx_msg void OnFileScreenshot();

    // -- 编辑 ---------------------------------------------------------------
    afx_msg void OnEditUndo();
    afx_msg void OnEditRedo();
    afx_msg void OnEditDelete();
    afx_msg void OnEditSelectAll();
    afx_msg void OnEditDeselect();
    afx_msg void OnEditRename();
    afx_msg void OnEditParams();
    afx_msg void OnEditColor();
    afx_msg void OnEditStyle();
    afx_msg void OnEditTransform();
    afx_msg void OnEditVisible();
    afx_msg void OnEditGroup();
    afx_msg void OnEditUndoCapacity();
    afx_msg void OnEditClearHistory();
    afx_msg void OnSetLineStyle(UINT id);

    // -- 创建 ---------------------------------------------------------------
    afx_msg void OnActivateTool(UINT id);
    afx_msg void OnCreateArc();
    afx_msg void OnWorkPlaneAxis(UINT id);
    afx_msg void OnWorkPlaneFromPick();

    // -- 视图 ---------------------------------------------------------------
    afx_msg void OnStandardView(UINT id);
    afx_msg void OnViewFit();
    afx_msg void OnViewFitSelection();
    afx_msg void OnViewPerspective();
    afx_msg void OnSetRenderMode(UINT id);
    afx_msg void OnCycleRenderMode();
    afx_msg void OnViewGrid();
    afx_msg void OnViewHud();
    afx_msg void OnViewBackground();
    afx_msg void OnViewCamera();
    afx_msg void OnPickFilter(UINT id);
    afx_msg void OnViewNewViewport();
    afx_msg void OnViewCloseExtra();
    afx_msg void OnViewTreePane();
    afx_msg void OnViewLogPane();

    // -- 捕捉 / 精度 --------------------------------------------------------
    afx_msg void OnSnapType(UINT id);
    afx_msg void OnSnapTolerance();
    afx_msg void OnSnapContinuous();
    afx_msg void OnTessParams();

    // -- 测量 ---------------------------------------------------------------
    afx_msg void OnMeasureLast();
    afx_msg void OnEntityInfo();
    afx_msg void OnUnitSettings();

    // -- 帮助 ---------------------------------------------------------------
    afx_msg void OnHelpShortcuts();
    afx_msg void OnAppAbout();
    afx_msg void OnLogLevel(UINT id);

    // -- 界面状态（ON_UPDATE_COMMAND_UI）------------------------------------
    afx_msg void OnUpdateUndo(CCmdUI* cmdUI);
    afx_msg void OnUpdateRedo(CCmdUI* cmdUI);
    afx_msg void OnUpdateHasSelection(CCmdUI* cmdUI);
    afx_msg void OnUpdateSingleSelection(CCmdUI* cmdUI);
    afx_msg void OnUpdateHasViewport(CCmdUI* cmdUI);
    afx_msg void OnUpdateTool(CCmdUI* cmdUI);
    afx_msg void OnUpdateStandardView(CCmdUI* cmdUI);
    afx_msg void OnUpdateRenderMode(CCmdUI* cmdUI);
    afx_msg void OnUpdateSnapType(CCmdUI* cmdUI);
    afx_msg void OnUpdatePickFilter(CCmdUI* cmdUI);
    afx_msg void OnUpdateLogLevel(CCmdUI* cmdUI);
    afx_msg void OnUpdateGrid(CCmdUI* cmdUI);
    afx_msg void OnUpdateHud(CCmdUI* cmdUI);
    afx_msg void OnUpdatePerspective(CCmdUI* cmdUI);
    afx_msg void OnUpdateContinuous(CCmdUI* cmdUI);
    afx_msg void OnUpdateCloseExtra(CCmdUI* cmdUI);
    afx_msg void OnUpdateTreePane(CCmdUI* cmdUI);
    afx_msg void OnUpdateLogPane(CCmdUI* cmdUI);

    DECLARE_MESSAGE_MAP()

private:
    // -- 搭界面 -------------------------------------------------------------
    BOOL CreateToolBarAndStatus();

    // -- 每帧 ---------------------------------------------------------------
    void OnFrame();
    void SyncStatusBar();
    /// @brief 场景版本变了才重建模型树；每帧重建一棵树太蠢了。
    void RefreshTree();
    void SyncTreeSelection();
    void DrainLog();
    void AppendLog(cadgeom::LogLevel level, const CString& text);

    // -- 小工具 -------------------------------------------------------------
    cadgeom::IViewport* ActiveViewport() const;
    cadgeom::ICamera* ActiveCamera() const;
    /// @return 当前选中的实体，按选择集的顺序。
    std::vector<cadgeom::EntityId> SelectedEntities() const;
    /// @brief 报一次引擎的错误，顺带把详情写进日志。
    void ReportError(const CString& what);
    /// @brief 把模型单位的长度格式化成显示字符串（"12.50 mm"）。
    CString FormatLength(double modelUnits) const;
    /// @brief 文件对话框的过滤器，从 IIoRegistry 注册表现问现答。
    CString IoFilter(bool forImport) const;
    /// @brief 「打开」和「导入」共用的那段。
    void DoImport(bool merge);
    /// @brief 「导出」和「导出选中」共用的那段。
    void DoExport(bool selectionOnly);
    /// @brief 让工具管理器先拿到一个上下文，再切工具。
    ///
    /// IToolManager::Activate 只在**已经有上下文**时才调 ITool::OnActivate()，而
    /// 上下文是视口在派发事件时顺手塞进去的（ViewportImpl::ActiveTool）。从菜单
    /// 切工具走的不是事件，所以先补一个「鼠标没动」的移动事件把上下文喂进去，
    /// 新工具才会被 Reset() 并写出自己的提示语。
    void PrimeToolContext();
    /// @brief 状态栏左边那格的临时提示，几秒后被下一帧的常规提示覆盖。
    void FlashMessage(const CString& text);

    cadgeom::ICadEngine& engine_;
    /// M6 的扩展接口。取不到（老库）就是 null，界面上相关的项会禁用。
    cadgeom::ICadEngine2* ext_{nullptr};

    CSplitterWnd splitMain_;   ///< 左右：模型树 | 右侧
    CSplitterWnd splitRight_;  ///< 上下：视口区 | 日志
    CViewArea viewArea_;
    CTreePane treePane_;
    CLogPane logPane_;
    CToolBar toolBar_;
    CStatusBar statusBar_;

    CViewportWnd* active_{nullptr};

    /// 状态栏四格上一次写进去的字：一样就不重写，免得每帧重画。
    CString statusPrompt_;
    CString statusSelection_;
    CString statusMeasure_;
    CString statusDevice_;

    ULONGLONG lastTick_{0};
    uint64_t lastRevision_{0};
    bool haveRevision_{false};
    uint64_t lastSelectionSignature_{0};
    /// 模型树往引擎写选择集时立起来，挡住「引擎变了 → 再写回树」的回环。
    bool syncingTree_{false};

    /// 面板收起来时记下原来的宽 / 高，再点一次好还原。
    int treeWidth_{280};
    int logHeight_{180};
    bool treeVisible_{true};
    bool logVisible_{true};

    CString flashText_;
    ULONGLONG flashUntil_{0};

    CString autoShotPath_;
    int autoShotFrames_{0};
    int frameCount_{0};
    CString viewportFailure_;
};

#endif // CADGEOM_MFC_VIEWER_MAINFRAME_H
