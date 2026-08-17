/**
 * @file MainWindow.h
 * @brief 宿主主窗口：菜单、工具栏、模型树、日志、状态栏，外加那圈帧循环。
 *
 * 分工是这样的 ——
 *
 *   * **帧循环归主窗口**。一个 QTimer 每帧调一次 ICadEngine::Tick()，然后让每个
 *     视口各画一帧。Tick 必须只调一次：脏几何解析一次、上传一次，剩下的才是每个
 *     视口自己那份相机与显示状态（docs/architecture.md §4.1）。
 *   * **快捷键归 Qt**。引擎自己也认一套字母键，但那是给 SurfaceKind::Glfw 那种
 *     「引擎拥有窗口」的场合准备的。这里窗口是 Qt 的，键就该由 QAction 领走，菜
 *     单上才写得出提示。没被菜单认领的键（Esc、Enter）才落到控件手里转发给引擎
 *     —— 那几个恰好是工具自己要的。
 *   * **面板归宿主**。引擎自绘的 HUD 只画 ASCII 笔画字，中文面板得自己做；数据
 *     从 ICadEngine2 拿：GetStatusText / FormatLength / GetMeasurement。
 */
#ifndef CADGEOM_QT_VIEWER_MAINWINDOW_H
#define CADGEOM_QT_VIEWER_MAINWINDOW_H

#include <cadgeom/CadGeom.h>

#include <QElapsedTimer>
#include <QList>
#include <QMainWindow>
#include <QVector>

class CadGeomWidget;

class QAction;
class QActionGroup;
class QLabel;
class QPlainTextEdit;
class QSplitter;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

/// @brief 一个能把引擎的能力挨个点一遍的宿主窗口。
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /// @param engine 引擎；窗口不持有它，**它必须活得比窗口久**。main() 里两个都
    ///               在栈上，引擎先声明、后析构，正好满足。
    explicit MainWindow(cadgeom::ICadEngine& engine, QWidget* parent = nullptr);
    ~MainWindow() override;

    /// @brief 自检用：画满 `frames` 帧之后存一张图、关窗口。
    /// @param path   截图路径；空字符串表示只跑帧数不存图。
    /// @param frames 帧数，<= 0 表示不启用。
    /// @note 和 glfw demo 的 `--headless --screenshot` 是同一个用意 —— 一次渲染
    ///       改动能不能在**嵌入宿主窗口**的那条路径上跑通，总得有个不靠手的验法。
    void setAutoShot(const QString& path, int frames);

    /// @brief 把内置的示例图纸画进场景，然后框住它。
    /// @note 窗口**不会**自己调这个 —— 起来就是一张空图纸，画什么由用户决定。菜单
    ///       里的「载入示例图纸」和命令行的 `--sample` 是它仅有的两个入口。
    void loadSampleScene();

private:
    // -- 搭界面 -------------------------------------------------------------
    void createActions();
    void createMenus();
    void createToolBar();
    void createDocks();
    void createStatusBar();
    /// @brief 新开一个视口控件并塞进分割条。
    CadGeomWidget* addViewport(bool secondary);

    // -- 文件 ---------------------------------------------------------------
    void onNewScene();
    /// @param merge true 并入当前场景，false 先清空（那样的导入不可撤销，头文件说过）。
    void onImport(bool merge);
    void onExport(bool selectionOnly);
    void onScreenshot();

    // -- 编辑 ---------------------------------------------------------------
    void onUndo();
    void onRedo();
    void onDeleteSelection();
    void onSelectAll();
    void onDeselect();
    void onRename();
    void onEditParams();
    void onSetColor();
    void onSetStyle();
    void onSetLineStyle(cadgeom::LineStyle style);
    void onSetTransform();
    void onToggleSelectionVisible();
    void onGroupSelection();
    void onClearHistory();
    void onUndoCapacity();

    // -- 创建 ---------------------------------------------------------------
    /// @brief 让工具管理器先拿到一个上下文，再切工具。
    ///
    /// IToolManager::Activate 只在**已经有上下文**时才调 ITool::OnActivate()，而
    /// 上下文是视口在派发事件时顺手塞进去的（ViewportImpl::ActiveTool）。从菜单
    /// 切工具走的不是事件，所以先补一个「鼠标没动」的移动事件把上下文喂进去，
    /// 新工具才会被 Reset() 并写出自己的提示语。
    void primeToolContext();
    void onActivateTool(cadgeom::ToolId id);
    void onCreateArc();
    /// @param axis 0 = XY、1 = YZ、2 = ZX。
    void onSetWorkPlaneAxis(int axis);
    void onSetWorkPlaneFromPick();

    // -- 视图 ---------------------------------------------------------------
    void onStandardView(cadgeom::StandardView view);
    void onFit(bool selectionOnly);
    void onToggleProjection();
    void onSetRenderMode(cadgeom::RenderMode mode);
    void onCycleRenderMode();
    void onToggleGrid();
    void onToggleHud();
    void onBackgroundColor();
    void onCameraParams();
    void onPickFilterChanged();
    void onNewViewport();
    void onCloseExtraViewports();

    // -- 捕捉 / 精度 --------------------------------------------------------
    void onSnapMaskChanged();
    void onSnapTolerance();
    void onContinuousMode(bool enabled);
    void onTessParams();

    // -- 测量 ---------------------------------------------------------------
    void onShowMeasurement();
    void onEntityInfo();
    void onUnitSettings();

    // -- 帮助 ---------------------------------------------------------------
    void onAbout();

    // -- 每帧 ---------------------------------------------------------------
    void onFrame();
    void syncActions();
    void syncStatusBar();
    /// @brief 场景版本变了才重建模型树；每帧重建一棵树太蠢了。
    void refreshTree();
    void syncTreeSelection();
    void onTreeSelectionChanged();
    void onTreeItemChanged(QTreeWidgetItem* item, int column);
    void appendLog(int level, const QString& text);

    // -- 小工具 -------------------------------------------------------------
    cadgeom::IViewport* activeViewport() const;
    cadgeom::ICamera* activeCamera() const;
    /// @return 当前选中的实体，按选择集的顺序。
    QVector<cadgeom::EntityId> selectedEntities() const;
    /// @brief 报一次引擎的错误，顺带把详情写进日志。
    void reportError(const QString& what);
    /// @brief 把模型单位的长度格式化成显示字符串（"12.50 mm"）。
    QString formatLength(double modelUnits) const;
    /// @brief 文件对话框的过滤器，从 IIoRegistry 注册表现问现答。
    QString ioFilter(bool forImport) const;

    cadgeom::ICadEngine& engine_;
    /// M6 的扩展接口。取不到（老库）就是 null，界面上相关的项会禁用。
    cadgeom::ICadEngine2* ext_{nullptr};

    QList<CadGeomWidget*> views_;
    CadGeomWidget* active_{nullptr};
    QSplitter* viewArea_{nullptr};
    QTimer* frameTimer_{nullptr};
    QElapsedTimer clock_;

    QTreeWidget* tree_{nullptr};
    QPlainTextEdit* log_{nullptr};

    QLabel* statusPrompt_{nullptr};
    QLabel* statusSelection_{nullptr};
    QLabel* statusMeasure_{nullptr};
    QLabel* statusDevice_{nullptr};

    // -- 动作 ---------------------------------------------------------------
    QAction* actUndo_{nullptr};
    QAction* actRedo_{nullptr};
    QAction* actDelete_{nullptr};
    QAction* actRename_{nullptr};
    QAction* actEditParams_{nullptr};
    QAction* actColor_{nullptr};
    QAction* actStyle_{nullptr};
    QAction* actTransform_{nullptr};
    QAction* actToggleVisible_{nullptr};
    QAction* actGroup_{nullptr};
    QAction* actExportSelection_{nullptr};
    QAction* actFitSelection_{nullptr};
    QAction* actEntityInfo_{nullptr};
    QAction* actWorkPlaneFromPick_{nullptr};
    QAction* actCloseExtraViewports_{nullptr};

    /// 两个停靠面板的开关；用 QDockWidget 自带的那个动作，勾选状态不用自己维护。
    QAction* actTreeDock_{nullptr};
    QAction* actLogDock_{nullptr};

    QAction* actGrid_{nullptr};
    QAction* actHud_{nullptr};
    QAction* actPerspective_{nullptr};
    QAction* actContinuous_{nullptr};

    QActionGroup* toolGroup_{nullptr};
    QVector<QAction*> toolActions_;  ///< 与 kToolTable 一一对应
    QActionGroup* renderModeGroup_{nullptr};
    QVector<QAction*> renderModeActions_;
    QVector<QAction*> snapActions_;
    QVector<QAction*> pickFilterActions_;
    QVector<QAction*> logLevelActions_;

    uint64_t lastRevision_{0};
    bool haveRevision_{false};
    uint64_t lastSelectionSignature_{0};
    /// 模型树往引擎写选择集时立起来，挡住「引擎变了 → 再写回树」的回环。
    bool syncingTree_{false};

    QString autoShotPath_;
    int autoShotFrames_{0};
    int frameCount_{0};
};

#endif // CADGEOM_QT_VIEWER_MAINWINDOW_H
