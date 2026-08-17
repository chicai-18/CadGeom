/**
 * @file MainWindow.cpp
 * @brief MainWindow 的实现 —— 菜单挂动作，动作调引擎。
 */
#include "MainWindow.h"

#include "CadGeomWidget.h"
#include "Dialogs.h"
#include "HostCommands.h"
#include "LogBridge.h"
#include "SampleScene.h"

#include <cadgeom/CadGeomRAII.h>

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColorDialog>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <cmath>
#include <utility>
#include <vector>

using namespace cadgeom;

namespace {

constexpr double kPi = 3.14159265358979323846;

/// 工具表：一处定义，菜单、工具栏、快捷键和状态同步全从它长出来。
struct ToolEntry {
    ToolId id;
    const char* name;
    const char* shortcut;
    const char* tip;
};

const ToolEntry kToolTable[] = {
    {ToolId::Select, "选择", "V", "点选对象；按住 Ctrl 加选"},
    {ToolId::Point, "点", "X", "在工作平面上落一个点"},
    {ToolId::Line, "直线", "L", "点两下，或者按住拖"},
    {ToolId::Circle, "圆", "C", "先圆心后半径"},
    {ToolId::Rectangle, "矩形", "R", "拖出对角线"},
    {ToolId::Polyline, "多段线", "Y", "连着点，Enter 结束"},
    {ToolId::Extrude, "拉伸", "E", "把闭合轮廓扫成实体；别在正对扫掠轴的视图里拉"},
    {ToolId::Move, "移动", "M", "拖 Gizmo 的轴"},
    {ToolId::Rotate, "旋转", "T", "R 给了矩形，所以是 turn"},
    {ToolId::Scale, "缩放", "S", "沿世界轴缩放"},
    {ToolId::Measure, "测量", "D", "M 给了移动，所以是 dimension"},
};

struct ViewEntry {
    StandardView view;
    const char* name;
    const char* shortcut;
};

const ViewEntry kViewTable[] = {
    {StandardView::Front, "前视图", "1"},   {StandardView::Back, "后视图", "2"},
    {StandardView::Right, "右视图", "3"},   {StandardView::Left, "左视图", "4"},
    {StandardView::Top, "顶视图", "5"},     {StandardView::Bottom, "底视图", "6"},
    {StandardView::Isometric, "等轴测", "7"},
};

struct RenderModeEntry {
    RenderMode mode;
    const char* name;
};

const RenderModeEntry kRenderModeTable[] = {
    {RenderMode::Shaded, "着色"},
    {RenderMode::ShadedWithEdges, "着色 + 特征边"},
    {RenderMode::Wireframe, "线框"},
    {RenderMode::HiddenLine, "隐藏线"},
};

struct SnapEntry {
    uint32_t bit;
    const char* name;
};

const SnapEntry kSnapTable[] = {
    {Snap_Endpoint, "端点"},     {Snap_Midpoint, "中点"},
    {Snap_Center, "圆心"},       {Snap_Quadrant, "象限点"},
    {Snap_Intersection, "交点"}, {Snap_Perpendicular, "垂足"},
    {Snap_Grid, "栅格"},
};

struct PickFilterEntry {
    uint32_t bit;
    const char* name;
};

const PickFilterEntry kPickFilterTable[] = {
    {PickFilter_Vertex, "顶点"},
    {PickFilter_Edge, "边"},
    {PickFilter_Face, "面"},
};

const char* const kLogLevelNames[] = {"Trace", "Debug", "Info", "Warning", "Error", "Fatal", "Off"};

/// @brief 形状类型的中文名，给模型树和信息框用。
QString ShapeTypeName(ShapeType type) {
    switch (type) {
        case ShapeType::None: return QStringLiteral("组");
        case ShapeType::Point: return QStringLiteral("点");
        case ShapeType::Line: return QStringLiteral("直线");
        case ShapeType::Circle: return QStringLiteral("圆");
        case ShapeType::Arc: return QStringLiteral("圆弧");
        case ShapeType::Rectangle: return QStringLiteral("矩形");
        case ShapeType::Polyline: return QStringLiteral("多段线");
        case ShapeType::Mesh: return QStringLiteral("网格");
        case ShapeType::Solid: return QStringLiteral("实体");
    }
    return QString();
}

/// @brief IoProgressCallback：把进度喂给一个 QProgressDialog，取消就返回 false。
bool IoProgress(float fraction, const char* message, void* userData) {
    auto* dialog = static_cast<QProgressDialog*>(userData);
    if (!dialog) {
        return true;
    }
    if (message) {
        dialog->setLabelText(QString::fromUtf8(message));
    }
    dialog->setValue(static_cast<int>(fraction * 100.0f));
    QCoreApplication::processEvents();
    return !dialog->wasCanceled();
}

/// @brief 帧循环暂停器。
///
/// 两件事都要它：进度回调里会 processEvents，帧循环不能在这中间插进来画半个场景；
/// 而文件对话框是**系统**的模态框，Qt 的 activeModalWidget() 看不见它，它自己那个
/// 消息循环照样会派发定时器 —— 自检模式下的「画满就关窗」会在对话框还开着的时候
/// 关掉主窗口，于是谁也退不出去。
class FrameLoopPause {
public:
    explicit FrameLoopPause(QTimer* timer) : timer_(timer) {
        if (timer_) {
            timer_->stop();
        }
    }
    ~FrameLoopPause() {
        if (timer_) {
            timer_->start(16);
        }
    }

    FrameLoopPause(const FrameLoopPause&) = delete;
    FrameLoopPause& operator=(const FrameLoopPause&) = delete;

private:
    QTimer* timer_;
};

QString FormatVec(const Vec3d& v, int decimals = 3) {
    return QStringLiteral("(%1, %2, %3)")
        .arg(v.x, 0, 'f', decimals)
        .arg(v.y, 0, 'f', decimals)
        .arg(v.z, 0, 'f', decimals);
}

} // namespace

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

MainWindow::MainWindow(ICadEngine& engine, QWidget* parent)
    : QMainWindow(parent), engine_(engine) {
    // M6 之后的新能力都从扩展槽进来。拿到 null 说明这是个老库 —— 那不是崩溃，是
    // 一句「这个版本没有这件东西」，界面上把相关的项禁掉就是了。
    ext_ = static_cast<ICadEngine2*>(engine_.GetExtension(ExtensionId_Engine2));

    setWindowTitle(QStringLiteral("CadGeom — Qt 宿主示例"));
    resize(1480, 920);

    viewArea_ = new QSplitter(Qt::Horizontal, this);
    viewArea_->setChildrenCollapsible(false);
    setCentralWidget(viewArea_);

    // 顺序有讲究：动作先有，面板要把自己的开关动作交给菜单，菜单才排得出来。
    createActions();
    createDocks();
    createMenus();
    createToolBar();
    createStatusBar();

    connect(&LogBridge::instance(), &LogBridge::message, this, &MainWindow::appendLog);

    addViewport(/*secondary=*/false);
    BuildSampleScene(*engine_.GetScene());

    frameTimer_ = new QTimer(this);
    connect(frameTimer_, &QTimer::timeout, this, &MainWindow::onFrame);
    clock_.start();
    frameTimer_->start(16);
}

MainWindow::~MainWindow() {
    if (frameTimer_) {
        frameTimer_->stop();
    }
    // 视口必须在引擎之前放掉。控件析构时会 Release 自己的视口，而引擎是 main()
    // 的栈对象，这会儿还活着 —— 但如果留给 ~QObject 去删子控件，那已经是本类成员
    // 析构之后的事了，顺序全靠运气。写明白它。
    qDeleteAll(views_);
    views_.clear();
    active_ = nullptr;
}

void MainWindow::setAutoShot(const QString& path, int frames) {
    autoShotPath_ = path;
    autoShotFrames_ = frames;
    frameCount_ = 0;
}

CadGeomWidget* MainWindow::addViewport(bool secondary) {
    ViewportDesc desc{};
    // 线性光里的「深灰」大约是 0.04，不是 0.2。
    desc.background = Color{0.035f, 0.038f, 0.045f, 1.0f};
    desc.projection = ProjectionMode::Orthographic;
    desc.showGrid = true;
    desc.vsync = true;
    desc.sampleCount = 4;

    auto* view = new CadGeomWidget(engine_, desc);
    views_.append(view);
    if (!active_) {
        active_ = view;
    }

    connect(view, &CadGeomWidget::activated, this, [this, view]() { active_ = view; });
    connect(view, &CadGeomWidget::engineConsumedInput, this, [this]() {
        // 引擎吃掉了一次输入 —— 工具状态多半变了，状态栏立刻跟上，不用等下一帧。
        syncStatusBar();
        syncActions();
    });
    connect(view, &CadGeomWidget::viewportReady, this, [this, view, secondary]() {
        IViewport* vp = view->viewport();
        if (!vp) {
            return;
        }
        if (secondary) {
            // 正视图 + 隐藏线：一眼看出视口之间只共享设备和几何，显示状态各自独立。
            vp->GetCamera()->SetStandardView(StandardView::Front);
            vp->SetRenderMode(RenderMode::HiddenLine);
        } else {
            vp->GetCamera()->SetStandardView(StandardView::Isometric);
        }
        vp->GetCamera()->ZoomToFit(nullptr, 1.25);
        appendLog(static_cast<int>(LogLevel::Info),
                  QStringLiteral("视口就绪，设备：%1").arg(QString::fromUtf8(engine_.GetDeviceName())));
    });
    connect(view, &CadGeomWidget::viewportFailed, this, [this](const QString& reason) {
        // 信号是在 showEvent 里发出来的，别在那个栈上弹模态框。
        QMetaObject::invokeMethod(
            this,
            [this, reason]() {
                if (frameTimer_) {
                    frameTimer_->stop();
                }
                appendLog(static_cast<int>(LogLevel::Error), reason);
                QMessageBox::critical(
                    this, QStringLiteral("无法创建视口"),
                    QStringLiteral("引擎没能在这个窗口上建起视口：\n\n%1\n\n"
                                   "没有 Vulkan SDK 的构建里渲染器整块是不编的，"
                                   "此时 CreateViewport 会明说而不是给一个坏指针。")
                        .arg(reason));
            },
            Qt::QueuedConnection);
    });

    viewArea_->addWidget(view);
    return view;
}

// ---------------------------------------------------------------------------
// 动作
// ---------------------------------------------------------------------------

void MainWindow::createActions() {
    // -- 工具：互斥一组，菜单和工具栏共用同一批 QAction，勾选状态因此天生一致 --
    toolGroup_ = new QActionGroup(this);
    toolGroup_->setExclusive(true);
    for (const ToolEntry& entry : kToolTable) {
        auto* act = new QAction(QString::fromUtf8(entry.name), this);
        act->setCheckable(true);
        act->setShortcut(QKeySequence(QString::fromUtf8(entry.shortcut)));
        act->setStatusTip(QString::fromUtf8(entry.tip));
        const ToolId id = entry.id;
        connect(act, &QAction::triggered, this, [this, id]() { onActivateTool(id); });
        toolGroup_->addAction(act);
        toolActions_.push_back(act);
    }

    // -- 渲染模式 -----------------------------------------------------------
    renderModeGroup_ = new QActionGroup(this);
    renderModeGroup_->setExclusive(true);
    for (const RenderModeEntry& entry : kRenderModeTable) {
        auto* act = new QAction(QString::fromUtf8(entry.name), this);
        act->setCheckable(true);
        const RenderMode mode = entry.mode;
        connect(act, &QAction::triggered, this, [this, mode]() { onSetRenderMode(mode); });
        renderModeGroup_->addAction(act);
        renderModeActions_.push_back(act);
    }

    // -- 捕捉 ---------------------------------------------------------------
    for (const SnapEntry& entry : kSnapTable) {
        auto* act = new QAction(QString::fromUtf8(entry.name), this);
        act->setCheckable(true);
        connect(act, &QAction::triggered, this, &MainWindow::onSnapMaskChanged);
        snapActions_.push_back(act);
    }

    // -- 拾取过滤 -----------------------------------------------------------
    for (const PickFilterEntry& entry : kPickFilterTable) {
        auto* act = new QAction(QString::fromUtf8(entry.name), this);
        act->setCheckable(true);
        connect(act, &QAction::triggered, this, &MainWindow::onPickFilterChanged);
        pickFilterActions_.push_back(act);
    }

    // -- 日志级别 -----------------------------------------------------------
    auto* logGroup = new QActionGroup(this);
    logGroup->setExclusive(true);
    for (int i = 0; i < 7; ++i) {
        auto* act = new QAction(QString::fromUtf8(kLogLevelNames[i]), this);
        act->setCheckable(true);
        const LogLevel level = static_cast<LogLevel>(i);
        connect(act, &QAction::triggered, this, [this, level]() { engine_.SetLogLevel(level); });
        logGroup->addAction(act);
        logLevelActions_.push_back(act);
    }

    // -- 需要按状态启停的那几个 ---------------------------------------------
    actUndo_ = new QAction(QStringLiteral("撤销"), this);
    actUndo_->setShortcut(QKeySequence::Undo);
    connect(actUndo_, &QAction::triggered, this, &MainWindow::onUndo);

    actRedo_ = new QAction(QStringLiteral("重做"), this);
    // 两派习惯都有人用，而它们不冲突。
    actRedo_->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Y")),
                            QKeySequence(QStringLiteral("Ctrl+Shift+Z"))});
    connect(actRedo_, &QAction::triggered, this, &MainWindow::onRedo);

    actDelete_ = new QAction(QStringLiteral("删除"), this);
    actDelete_->setShortcut(QKeySequence::Delete);
    connect(actDelete_, &QAction::triggered, this, &MainWindow::onDeleteSelection);

    actRename_ = new QAction(QStringLiteral("重命名…"), this);
    actRename_->setShortcut(QKeySequence(Qt::Key_F2));
    connect(actRename_, &QAction::triggered, this, &MainWindow::onRename);

    actEditParams_ = new QAction(QStringLiteral("编辑参数…"), this);
    actEditParams_->setShortcut(QKeySequence(Qt::Key_F4));
    actEditParams_->setStatusTip(QStringLiteral("改半径就重新生成几何 —— 参数是真相，网格是缓存"));
    connect(actEditParams_, &QAction::triggered, this, &MainWindow::onEditParams);

    actColor_ = new QAction(QStringLiteral("设置颜色…"), this);
    actColor_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    connect(actColor_, &QAction::triggered, this, &MainWindow::onSetColor);

    actStyle_ = new QAction(QStringLiteral("样式…"), this);
    actStyle_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    actStyle_->setStatusTip(QStringLiteral("整份 EntityStyle：颜色、边色、线宽、点大小、线型"));
    connect(actStyle_, &QAction::triggered, this, &MainWindow::onSetStyle);

    actTransform_ = new QAction(QStringLiteral("变换…"), this);
    actTransform_->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    actTransform_->setStatusTip(QStringLiteral("Gizmo 之外的那条路：直接输数"));
    connect(actTransform_, &QAction::triggered, this, &MainWindow::onSetTransform);

    actToggleVisible_ = new QAction(QStringLiteral("显示 / 隐藏"), this);
    actToggleVisible_->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
    connect(actToggleVisible_, &QAction::triggered, this, &MainWindow::onToggleSelectionVisible);

    actGroup_ = new QAction(QStringLiteral("编组…"), this);
    actGroup_->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    connect(actGroup_, &QAction::triggered, this, &MainWindow::onGroupSelection);

    actExportSelection_ = new QAction(QStringLiteral("导出选中…"), this);
    actExportSelection_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    connect(actExportSelection_, &QAction::triggered, this, [this]() { onExport(true); });

    actFitSelection_ = new QAction(QStringLiteral("缩放至选中"), this);
    actFitSelection_->setShortcut(QKeySequence(QStringLiteral("Shift+F")));
    connect(actFitSelection_, &QAction::triggered, this, [this]() { onFit(true); });

    actEntityInfo_ = new QAction(QStringLiteral("选中对象信息…"), this);
    connect(actEntityInfo_, &QAction::triggered, this, &MainWindow::onEntityInfo);

    actWorkPlaneFromPick_ = new QAction(QStringLiteral("由光标下的面设定"), this);
    actWorkPlaneFromPick_->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
    actWorkPlaneFromPick_->setStatusTip(
        QStringLiteral("点一个面、在上面画、再拉伸 —— CAD 的那条主路"));
    connect(actWorkPlaneFromPick_, &QAction::triggered, this, &MainWindow::onSetWorkPlaneFromPick);

    actCloseExtraViewports_ = new QAction(QStringLiteral("只留一个视口"), this);
    actCloseExtraViewports_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+W")));
    connect(actCloseExtraViewports_, &QAction::triggered, this,
            &MainWindow::onCloseExtraViewports);

    actGrid_ = new QAction(QStringLiteral("显示网格"), this);
    actGrid_->setCheckable(true);
    actGrid_->setShortcut(QKeySequence(Qt::Key_G));
    connect(actGrid_, &QAction::triggered, this, &MainWindow::onToggleGrid);

    actHud_ = new QAction(QStringLiteral("引擎自绘 HUD"), this);
    actHud_->setCheckable(true);
    actHud_->setShortcut(QKeySequence(Qt::Key_H));
    actHud_->setStatusTip(QStringLiteral("笔画字体画的 ASCII 状态行；真正的中文面板在下面"));
    connect(actHud_, &QAction::triggered, this, &MainWindow::onToggleHud);

    actPerspective_ = new QAction(QStringLiteral("透视投影"), this);
    actPerspective_->setCheckable(true);
    actPerspective_->setShortcut(QKeySequence(Qt::Key_P));
    connect(actPerspective_, &QAction::triggered, this, &MainWindow::onToggleProjection);

    actContinuous_ = new QAction(QStringLiteral("连续绘制"), this);
    actContinuous_->setCheckable(true);
    actContinuous_->setStatusTip(QStringLiteral("画完一段接着画下一段"));
    connect(actContinuous_, &QAction::triggered, this,
            [this](bool checked) { onContinuousMode(checked); });
}

void MainWindow::createMenus() {
    // QMenu::addAction(text, receiver, slot) 的那几个便利重载在 Qt5 / Qt6 之间挪
    // 过位置，这里统一用「先 addAction 再 connect」，两边都编得过。
    const auto add = [this](QMenu* menu, const QString& text, auto&& handler) {
        QAction* act = menu->addAction(text);
        connect(act, &QAction::triggered, this, std::forward<decltype(handler)>(handler));
        return act;
    };

    // -- 文件 ---------------------------------------------------------------
    QMenu* file = menuBar()->addMenu(QStringLiteral("文件(&F)"));

    QAction* act = add(file, QStringLiteral("新建"), [this]() { onNewScene(); });
    act->setShortcut(QKeySequence::New);
    act = add(file, QStringLiteral("载入示例图纸"), [this]() { onSampleScene(); });
    act->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    file->addSeparator();

    act = add(file, QStringLiteral("打开…"), [this]() { onImport(false); });
    act->setShortcut(QKeySequence::Open);
    act->setStatusTip(QStringLiteral("先清空场景再读；这样的导入不可撤销"));
    act = add(file, QStringLiteral("导入…"), [this]() { onImport(true); });
    act->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    act->setStatusTip(QStringLiteral("并入当前场景，整个导入是一个撤销步"));
    file->addSeparator();

    act = add(file, QStringLiteral("导出…"), [this]() { onExport(false); });
    act->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    act->setStatusTip(QStringLiteral("glTF 的 extras 里带着参数，圆导出去还是圆"));
    file->addAction(actExportSelection_);
    file->addSeparator();

    act = add(file, QStringLiteral("保存截图…"), [this]() { onScreenshot(); });
    act->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    file->addSeparator();

    act = add(file, QStringLiteral("退出"), [this]() { close(); });
    act->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));

    // -- 编辑 ---------------------------------------------------------------
    QMenu* edit = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    edit->addAction(actUndo_);
    edit->addAction(actRedo_);
    edit->addSeparator();
    edit->addAction(actDelete_);
    act = add(edit, QStringLiteral("全选"), [this]() { onSelectAll(); });
    act->setShortcut(QKeySequence::SelectAll);
    act = add(edit, QStringLiteral("取消选择"), [this]() { onDeselect(); });
    act->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
    edit->addSeparator();
    edit->addAction(actRename_);
    edit->addAction(actEditParams_);
    edit->addAction(actColor_);

    QMenu* lineStyle = edit->addMenu(QStringLiteral("线型"));
    const struct {
        LineStyle style;
        const char* name;
    } kLineStyles[] = {
        {LineStyle::Solid, "实线"},     {LineStyle::Dashed, "虚线"},
        {LineStyle::Dotted, "点线"},    {LineStyle::DashDot, "点画线"},
        {LineStyle::Center, "中心线"},  {LineStyle::Hidden, "隐藏线"},
    };
    for (const auto& entry : kLineStyles) {
        const LineStyle style = entry.style;
        add(lineStyle, QString::fromUtf8(entry.name), [this, style]() { onSetLineStyle(style); });
    }

    edit->addAction(actStyle_);
    edit->addAction(actTransform_);
    edit->addAction(actToggleVisible_);
    edit->addAction(actGroup_);
    edit->addSeparator();
    add(edit, QStringLiteral("撤销栈容量…"), [this]() { onUndoCapacity(); });
    add(edit, QStringLiteral("清空撤销历史"), [this]() { onClearHistory(); });

    // -- 创建 ---------------------------------------------------------------
    QMenu* create = menuBar()->addMenu(QStringLiteral("创建(&C)"));
    create->addAction(toolActions_[0]);  // 选择
    create->addSeparator();
    for (int i = 1; i <= 5; ++i) {  // 点 / 直线 / 圆 / 矩形 / 多段线
        create->addAction(toolActions_[i]);
    }
    create->addSeparator();
    create->addAction(toolActions_[6]);  // 拉伸
    create->addSeparator();
    for (int i = 7; i <= 9; ++i) {  // 移动 / 旋转 / 缩放
        create->addAction(toolActions_[i]);
    }
    create->addSeparator();
    add(create, QStringLiteral("按参数创建圆弧…"), [this]() { onCreateArc(); })
        ->setStatusTip(QStringLiteral("圆弧没有交互工具，但内核有 MakeArc"));

    QMenu* workPlane = create->addMenu(QStringLiteral("工作平面"));
    add(workPlane, QStringLiteral("XY 平面（俯视）"), [this]() { onSetWorkPlaneAxis(0); });
    add(workPlane, QStringLiteral("YZ 平面（侧视）"), [this]() { onSetWorkPlaneAxis(1); });
    add(workPlane, QStringLiteral("ZX 平面（正视）"), [this]() { onSetWorkPlaneAxis(2); });
    workPlane->addSeparator();
    workPlane->addAction(actWorkPlaneFromPick_);

    // -- 视图 ---------------------------------------------------------------
    QMenu* view = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    QMenu* standard = view->addMenu(QStringLiteral("标准视图"));
    for (const ViewEntry& entry : kViewTable) {
        const StandardView which = entry.view;
        QAction* viewAct = add(standard, QString::fromUtf8(entry.name),
                               [this, which]() { onStandardView(which); });
        viewAct->setShortcut(QKeySequence(QString::fromUtf8(entry.shortcut)));
    }
    view->addSeparator();
    act = add(view, QStringLiteral("缩放至全部"), [this]() { onFit(false); });
    act->setShortcut(QKeySequence(Qt::Key_F));
    act->setStatusTip(QStringLiteral("有选中时 F 先框选中的那些"));
    view->addAction(actFitSelection_);
    view->addAction(actPerspective_);
    view->addSeparator();

    QMenu* renderMode = view->addMenu(QStringLiteral("渲染模式"));
    for (QAction* modeAct : renderModeActions_) {
        renderMode->addAction(modeAct);
    }
    renderMode->addSeparator();
    act = add(renderMode, QStringLiteral("轮换"), [this]() { onCycleRenderMode(); });
    act->setShortcut(QKeySequence(Qt::Key_W));

    view->addAction(actGrid_);
    view->addAction(actHud_);
    add(view, QStringLiteral("背景色…"), [this]() { onBackgroundColor(); });
    add(view, QStringLiteral("相机参数…"), [this]() { onCameraParams(); });
    view->addSeparator();

    QMenu* pickFilter = view->addMenu(QStringLiteral("拾取过滤"));
    pickFilter->setStatusTip(QStringLiteral("一次拾取允许返回哪些子元素"));
    for (QAction* filterAct : pickFilterActions_) {
        pickFilter->addAction(filterAct);
    }

    view->addSeparator();
    act = add(view, QStringLiteral("新建视口"), [this]() { onNewViewport(); });
    act->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    act->setStatusTip(QStringLiteral("共享设备与几何，相机和显示状态各自独立"));
    view->addAction(actCloseExtraViewports_);
    view->addSeparator();
    if (actTreeDock_) {
        view->addAction(actTreeDock_);
    }
    if (actLogDock_) {
        view->addAction(actLogDock_);
    }

    // -- 捕捉 ---------------------------------------------------------------
    QMenu* snap = menuBar()->addMenu(QStringLiteral("捕捉(&S)"));
    for (QAction* snapAct : snapActions_) {
        snap->addAction(snapAct);
    }
    snap->addSeparator();
    add(snap, QStringLiteral("捕捉半径…"), [this]() { onSnapTolerance(); });
    snap->addAction(actContinuous_);
    snap->addSeparator();
    add(snap, QStringLiteral("细分精度…"), [this]() { onTessParams(); });

    // -- 测量 ---------------------------------------------------------------
    QMenu* measure = menuBar()->addMenu(QStringLiteral("测量(&M)"));
    measure->addAction(toolActions_[10]);  // 测量工具
    add(measure, QStringLiteral("最近一次测量…"), [this]() { onShowMeasurement(); });
    measure->addAction(actEntityInfo_);
    measure->addSeparator();
    act = add(measure, QStringLiteral("显示单位…"), [this]() { onUnitSettings(); });
    act->setShortcut(QKeySequence(QStringLiteral("Ctrl+U")));
    act->setStatusTip(QStringLiteral("只影响读数，一个顶点都不会动"));

    // -- 帮助 ---------------------------------------------------------------
    QMenu* help = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    act = add(help, QStringLiteral("快捷键…"), [this]() { ShowShortcutsDialog(this); });
    act->setShortcut(QKeySequence(Qt::Key_F1));
    QMenu* logLevel = help->addMenu(QStringLiteral("日志级别"));
    for (QAction* levelAct : logLevelActions_) {
        logLevel->addAction(levelAct);
    }
    help->addSeparator();
    add(help, QStringLiteral("关于 CadGeom…"), [this]() { onAbout(); });
    add(help, QStringLiteral("关于 Qt…"), [this]() { QApplication::aboutQt(); });
}

void MainWindow::createToolBar() {
    auto* bar = addToolBar(QStringLiteral("主工具栏"));
    bar->setObjectName(QStringLiteral("mainToolBar"));
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    bar->addAction(actUndo_);
    bar->addAction(actRedo_);
    bar->addSeparator();
    for (QAction* act : toolActions_) {
        bar->addAction(act);
    }
    bar->addSeparator();
    bar->addAction(actPerspective_);
    bar->addAction(actGrid_);
}

void MainWindow::createDocks() {
    // -- 模型树 -------------------------------------------------------------
    auto* treeDock = new QDockWidget(QStringLiteral("模型树"), this);
    treeDock->setObjectName(QStringLiteral("treeDock"));
    tree_ = new QTreeWidget(treeDock);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({QStringLiteral("名称"), QStringLiteral("类型")});
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setUniformRowHeights(true);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onTreeSelectionChanged);
    connect(tree_, &QTreeWidget::itemChanged, this, &MainWindow::onTreeItemChanged);
    treeDock->setWidget(tree_);
    addDockWidget(Qt::LeftDockWidgetArea, treeDock);

    // -- 日志 ---------------------------------------------------------------
    auto* logDock = new QDockWidget(QStringLiteral("引擎日志"), this);
    logDock->setObjectName(QStringLiteral("logDock"));
    log_ = new QPlainTextEdit(logDock);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(4000);
    logDock->setWidget(log_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);

    // 面板开关直接用 Qt 给的那两个动作，勾选状态不用自己维护。
    actTreeDock_ = treeDock->toggleViewAction();
    actLogDock_ = logDock->toggleViewAction();
}

void MainWindow::createStatusBar() {
    statusPrompt_ = new QLabel(this);
    statusSelection_ = new QLabel(this);
    statusMeasure_ = new QLabel(this);
    statusDevice_ = new QLabel(this);

    // 提示语占大头，右边三块是常驻信息。
    statusBar()->addWidget(statusPrompt_, 1);
    statusBar()->addPermanentWidget(statusSelection_);
    statusBar()->addPermanentWidget(statusMeasure_);
    statusBar()->addPermanentWidget(statusDevice_);
}

// ---------------------------------------------------------------------------
// 文件
// ---------------------------------------------------------------------------

void MainWindow::onNewScene() {
    IScene* scene = engine_.GetScene();
    if (scene->GetEntityCount() > 0 &&
        QMessageBox::question(this, QStringLiteral("新建"),
                              QStringLiteral("清空场景？撤销历史也会一起丢掉。")) !=
            QMessageBox::Yes) {
        return;
    }
    scene->Clear();
    appendLog(static_cast<int>(LogLevel::Info), QStringLiteral("场景已清空"));
}

void MainWindow::onSampleScene() {
    BuildSampleScene(*engine_.GetScene());
    onFit(false);
}

QString MainWindow::ioFilter(bool forImport) const {
    IIoRegistry* io = engine_.GetIoRegistry();
    QStringList patterns;
    QStringList entries;
    for (uint32_t i = 0; i < io->GetFormatCount(); ++i) {
        const char* ext = io->GetFormatExtension(i);
        if (!ext) {
            continue;
        }
        const bool usable = forImport ? io->CanImport(ext) : io->CanExport(ext);
        if (!usable) {
            continue;
        }
        const QString pattern = QStringLiteral("*.%1").arg(QString::fromUtf8(ext));
        patterns << pattern;
        entries << QStringLiteral("%1 (%2)").arg(QString::fromUtf8(ext).toUpper(), pattern);
    }
    QStringList all;
    if (!patterns.isEmpty()) {
        all << QStringLiteral("所有支持的格式 (%1)").arg(patterns.join(QLatin1Char(' ')));
    }
    all += entries;
    all << QStringLiteral("所有文件 (*)");
    return all.join(QStringLiteral(";;"));
}

void MainWindow::onImport(bool merge) {
    FrameLoopPause pause(frameTimer_);

    const QString path = QFileDialog::getOpenFileName(
        this, merge ? QStringLiteral("导入") : QStringLiteral("打开"), QString(), ioFilter(true));
    if (path.isEmpty()) {
        return;
    }

    ImportOptions options{};
    options.mergeIntoScene = merge;
    options.readParametricExtras = true;

    QProgressDialog progress(QStringLiteral("正在读取…"), QStringLiteral("取消"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    const CgResult result = engine_.GetIoRegistry()->Import(path.toUtf8().constData(), options,
                                                            &IoProgress, &progress);
    progress.close();

    if (CgFailed(result)) {
        reportError(QStringLiteral("导入失败"));
        return;
    }
    appendLog(static_cast<int>(LogLevel::Info),
              QStringLiteral("已读入 %1，场景里现在有 %2 个对象")
                  .arg(path)
                  .arg(engine_.GetScene()->GetEntityCount()));
    onFit(false);
}

void MainWindow::onExport(bool selectionOnly) {
    if (selectionOnly && engine_.GetScene()->GetSelection()->GetCount() == 0) {
        statusBar()->showMessage(QStringLiteral("没有选中任何对象"), 3000);
        return;
    }
    FrameLoopPause pause(frameTimer_);

    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出"),
                                                      QStringLiteral("scene.glb"), ioFilter(false));
    if (path.isEmpty()) {
        return;
    }

    ExportOptions options{};
    options.selectionOnly = selectionOnly;
    // 参数写进 glTF 的 extras：我们自己的读取器能把圆读回成圆，别人看到的还是网格。
    options.writeParametricExtras = true;

    QProgressDialog progress(QStringLiteral("正在写出…"), QStringLiteral("取消"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    const CgResult result = engine_.GetIoRegistry()->Export(path.toUtf8().constData(), options,
                                                            &IoProgress, &progress);
    progress.close();

    if (CgFailed(result)) {
        reportError(QStringLiteral("导出失败"));
        return;
    }
    appendLog(static_cast<int>(LogLevel::Info), QStringLiteral("已写出 %1").arg(path));
}

void MainWindow::onScreenshot() {
    IViewport* vp = activeViewport();
    if (!vp) {
        return;
    }
    FrameLoopPause pause(frameTimer_);

    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存截图"),
                                                      QStringLiteral("cadgeom.png"),
                                                      QStringLiteral("PNG 图片 (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    // 截的是「最后一次呈现的那一帧」，所以先老老实实画一帧出来。
    engine_.Tick(0.0);
    vp->Render();
    if (CgFailed(vp->SaveScreenshot(path.toUtf8().constData()))) {
        reportError(QStringLiteral("截图失败"));
        return;
    }
    appendLog(static_cast<int>(LogLevel::Info), QStringLiteral("截图已保存到 %1").arg(path));
}

// ---------------------------------------------------------------------------
// 编辑
// ---------------------------------------------------------------------------

void MainWindow::onUndo() {
    ICommandStack* stack = engine_.GetScene()->GetCommandStack();
    const QString name = QString::fromUtf8(stack->PeekUndoName() ? stack->PeekUndoName() : "");
    if (CgFailed(stack->Undo())) {
        reportError(QStringLiteral("撤销失败"));
        return;
    }
    statusBar()->showMessage(QStringLiteral("已撤销 %1").arg(name), 2000);
}

void MainWindow::onRedo() {
    ICommandStack* stack = engine_.GetScene()->GetCommandStack();
    const QString name = QString::fromUtf8(stack->PeekRedoName() ? stack->PeekRedoName() : "");
    if (CgFailed(stack->Redo())) {
        reportError(QStringLiteral("重做失败"));
        return;
    }
    statusBar()->showMessage(QStringLiteral("已重做 %1").arg(name), 2000);
}

QVector<EntityId> MainWindow::selectedEntities() const {
    const ISelection* selection = engine_.GetScene()->GetSelection();
    QVector<EntityId> out;
    out.reserve(static_cast<int>(selection->GetCount()));
    for (uint32_t i = 0; i < selection->GetCount(); ++i) {
        out.push_back(selection->GetAt(i));
    }
    return out;
}

void MainWindow::onDeleteSelection() {
    IScene* scene = engine_.GetScene();
    const QVector<EntityId> doomed = selectedEntities();
    if (doomed.isEmpty()) {
        return;
    }
    // 先清选择集：那些 id 马上就不存在了。
    scene->GetSelection()->Clear();
    std::vector<EntityId> ids(doomed.begin(), doomed.end());
    if (CgFailed(scene->DestroyEntities(CgSpan<const EntityId>{ids.data(), ids.size()}))) {
        reportError(QStringLiteral("删除失败"));
    }
}

void MainWindow::onSelectAll() {
    IScene* scene = engine_.GetScene();
    std::vector<EntityId> all;
    all.reserve(scene->GetEntityCount());
    for (uint32_t i = 0; i < scene->GetEntityCount(); ++i) {
        all.push_back(scene->GetEntityAt(i));
    }
    scene->GetSelection()->Set(CgSpan<const EntityId>{all.data(), all.size()});
}

void MainWindow::onDeselect() {
    engine_.GetScene()->GetSelection()->Clear();
}

void MainWindow::onRename() {
    const QVector<EntityId> targets = selectedEntities();
    if (targets.size() != 1) {
        statusBar()->showMessage(QStringLiteral("请先选中一个对象"), 3000);
        return;
    }
    IEntity* entity = engine_.GetScene()->GetEntity(targets.front());
    if (!entity) {
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, QStringLiteral("重命名"), QStringLiteral("名称"),
                              QLineEdit::Normal, QString::fromUtf8(entity->GetName()), &ok);
    if (!ok) {
        return;
    }
    // 宿主 new 的命令，引擎执行、持有，最后调它的 Release()——分配与释放同侧。
    engine_.GetScene()->GetCommandStack()->Push(
        new RenameCommand(targets.front(), name.toUtf8().constData()));
}

void MainWindow::onEditParams() {
    const QVector<EntityId> targets = selectedEntities();
    if (targets.size() != 1) {
        statusBar()->showMessage(QStringLiteral("请先选中一个对象"), 3000);
        return;
    }
    IScene* scene = engine_.GetScene();
    IEntity* entity = scene->GetEntity(targets.front());
    if (!entity) {
        return;
    }

    const ShapeType type = entity->GetShapeType();
    if (!ShapeParamsDialog::Supports(type)) {
        QMessageBox::information(this, QStringLiteral("无法编辑参数"),
                                 ShapeParamsDialog::UnsupportedReason(type));
        return;
    }

    ShapeParams params{};
    if (!scene->GetGeometryBuilder()->GetParams(targets.front(), params)) {
        reportError(QStringLiteral("读不出参数"));
        return;
    }

    ShapeParamsDialog dialog(params, ext_,
                             QStringLiteral("编辑参数 — %1").arg(ShapeTypeName(type)), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    // 改的是参数；网格是派生缓存，引擎自己会打脏标记重新细分。
    if (CgFailed(scene->GetGeometryBuilder()->SetParams(targets.front(), dialog.params()))) {
        reportError(QStringLiteral("参数无效"));
    }
}

void MainWindow::onSetColor() {
    const QVector<EntityId> targets = selectedEntities();
    if (targets.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("没有选中任何对象"), 3000);
        return;
    }
    QColor initial = Qt::white;
    if (IEntity* first = engine_.GetScene()->GetEntity(targets.front())) {
        EntityStyle style{};
        first->GetStyle(style);
        initial = ColorToQColor(style.color);
    }
    const QColor picked = QColorDialog::getColor(initial, this, QStringLiteral("对象颜色"));
    if (!picked.isValid()) {
        return;
    }
    const Color linear = ColorFromQColor(picked);
    engine_.GetScene()->GetCommandStack()->Push(new StyleCommand(
        std::vector<EntityId>(targets.begin(), targets.end()),
        [linear](EntityStyle& style) { style.color = linear; }, "Set colour"));
}

void MainWindow::onSetStyle() {
    const QVector<EntityId> targets = selectedEntities();
    if (targets.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("没有选中任何对象"), 3000);
        return;
    }
    // 表单从第一个选中对象读初值；确定之后整份样式套到所有选中对象上。
    EntityStyle current{};
    if (IEntity* first = engine_.GetScene()->GetEntity(targets.front())) {
        first->GetStyle(current);
    }

    EntityStyleDialog dialog(current, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const EntityStyle next = dialog.style();
    engine_.GetScene()->GetCommandStack()->Push(
        new StyleCommand(std::vector<EntityId>(targets.begin(), targets.end()),
                         [next](EntityStyle& style) { style = next; }, "Set style"));
}

void MainWindow::onSetTransform() {
    const QVector<EntityId> targets = selectedEntities();
    if (targets.size() != 1) {
        statusBar()->showMessage(QStringLiteral("请先选中一个对象"), 3000);
        return;
    }
    IEntity* entity = engine_.GetScene()->GetEntity(targets.front());
    if (!entity) {
        return;
    }
    Transform local{};
    entity->GetLocalTransform(local);

    TransformDialog dialog(local, ext_, this);
    if (dialog.exec() == QDialog::Accepted) {
        engine_.GetScene()->GetCommandStack()->Push(
            new TransformCommand(targets.front(), dialog.transform()));
    }
}

void MainWindow::onSetLineStyle(LineStyle style) {
    const QVector<EntityId> targets = selectedEntities();
    if (targets.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("没有选中任何对象"), 3000);
        return;
    }
    engine_.GetScene()->GetCommandStack()->Push(new StyleCommand(
        std::vector<EntityId>(targets.begin(), targets.end()),
        [style](EntityStyle& s) { s.lineStyle = style; }, "Set line style"));
}

void MainWindow::onToggleSelectionVisible() {
    const QVector<EntityId> targets = selectedEntities();
    if (targets.isEmpty()) {
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
    engine_.GetScene()->GetCommandStack()->Push(new StyleCommand(
        std::vector<EntityId>(targets.begin(), targets.end()),
        [target](EntityStyle& s) { s.visible = target; },
        target ? "Show" : "Hide"));
}

void MainWindow::onGroupSelection() {
    const QVector<EntityId> targets = selectedEntities();
    if (targets.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("没有选中任何对象"), 3000);
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("编组"), QStringLiteral("组名"),
                                               QLineEdit::Normal, QStringLiteral("Group"), &ok);
    if (!ok || name.isEmpty()) {
        return;
    }
    engine_.GetScene()->GetCommandStack()->Push(new GroupCommand(
        std::vector<EntityId>(targets.begin(), targets.end()), name.toUtf8().constData()));
}

void MainWindow::onUndoCapacity() {
    bool ok = false;
    const int capacity = QInputDialog::getInt(
        this, QStringLiteral("撤销栈容量"),
        QStringLiteral("最多保留多少步（0 表示不限）"), 0, 0, 100000, 1, &ok);
    if (ok) {
        // 超出容量时最老的那些先掉出去。
        engine_.GetScene()->GetCommandStack()->SetCapacity(static_cast<uint32_t>(capacity));
    }
}

void MainWindow::onClearHistory() {
    if (QMessageBox::question(this, QStringLiteral("清空撤销历史"),
                              QStringLiteral("撤销和重做都会清空，场景本身不动。")) !=
        QMessageBox::Yes) {
        return;
    }
    engine_.GetScene()->GetCommandStack()->Clear();
}

// ---------------------------------------------------------------------------
// 创建
// ---------------------------------------------------------------------------

void MainWindow::primeToolContext() {
    IViewport* vp = activeViewport();
    if (!vp || !active_) {
        return;
    }
    double x = 0.0;
    double y = 0.0;
    active_->lastCursorPixel(x, y);
    MouseEvent e{};
    e.button = MouseButton::None;
    e.action = MouseAction::Move;
    e.x = x;
    e.y = y;
    vp->OnMouseEvent(e);
}

void MainWindow::onActivateTool(ToolId id) {
    primeToolContext();
    if (CgFailed(engine_.GetToolManager()->Activate(id))) {
        reportError(QStringLiteral("切换工具失败"));
        return;
    }
    // 把焦点还给视口，否则接下来的 Esc / Enter 会被菜单栏吃掉。
    if (active_) {
        active_->setFocus();
    }
}

void MainWindow::onCreateArc() {
    IViewport* vp = activeViewport();
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

    ShapeParamsDialog dialog(params, ext_, QStringLiteral("按参数创建圆弧"), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const ShapeParams edited = dialog.params();
    const EntityId created = engine_.GetScene()->GetGeometryBuilder()->MakeArc(
        edited.arc.plane, edited.arc.radius, edited.arc.startAngle, edited.arc.sweepAngle);
    if (!IsValid(created)) {
        reportError(QStringLiteral("创建圆弧失败"));
    }
}

void MainWindow::onSetWorkPlaneAxis(int axis) {
    IViewport* vp = activeViewport();
    if (!vp) {
        return;
    }
    WorkPlane plane{};
    plane.origin = Vec3d{0.0, 0.0, 0.0};
    switch (axis) {
        case 1:  // YZ
            plane.normal = Vec3d{1, 0, 0};
            plane.uAxis = Vec3d{0, 1, 0};
            plane.vAxis = Vec3d{0, 0, 1};
            break;
        case 2:  // ZX
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
    statusBar()->showMessage(QStringLiteral("工作平面已重设"), 2000);
}

void MainWindow::onSetWorkPlaneFromPick() {
    IViewport* vp = activeViewport();
    if (!vp || !active_) {
        return;
    }
    double x = 0.0;
    double y = 0.0;
    active_->lastCursorPixel(x, y);

    PickResult pick{};
    if (!vp->Pick(x, y, PickFilter_All, pick)) {
        statusBar()->showMessage(QStringLiteral("光标下没有东西可拾取"), 3000);
        return;
    }
    if (CgFailed(vp->SetWorkPlaneFromPick(pick))) {
        reportError(QStringLiteral("这个命中点定不出平面"));
        return;
    }
    statusBar()->showMessage(
        QStringLiteral("工作平面已落在 %1 上").arg(FormatVec(pick.point)), 3000);
}

// ---------------------------------------------------------------------------
// 视图
// ---------------------------------------------------------------------------

IViewport* MainWindow::activeViewport() const {
    return active_ ? active_->viewport() : nullptr;
}

ICamera* MainWindow::activeCamera() const {
    IViewport* vp = activeViewport();
    return vp ? vp->GetCamera() : nullptr;
}

void MainWindow::onStandardView(StandardView view) {
    if (ICamera* camera = activeCamera()) {
        camera->SetStandardView(view);
    }
}

void MainWindow::onFit(bool selectionOnly) {
    ICamera* camera = activeCamera();
    if (!camera) {
        return;
    }
    Aabb bounds{};
    // F 的习惯：有选中就框选中的，没有就框整个场景。
    if (engine_.GetScene()->GetSelection()->GetBounds(bounds)) {
        camera->ZoomToFit(&bounds, 1.25);
        return;
    }
    if (selectionOnly) {
        statusBar()->showMessage(QStringLiteral("没有选中任何对象"), 3000);
        return;
    }
    camera->ZoomToFit(nullptr, 1.25);
}

void MainWindow::onToggleProjection() {
    if (ICamera* camera = activeCamera()) {
        camera->SetProjection(camera->GetProjection() == ProjectionMode::Orthographic
                                  ? ProjectionMode::Perspective
                                  : ProjectionMode::Orthographic);
    }
}

void MainWindow::onSetRenderMode(RenderMode mode) {
    if (IViewport* vp = activeViewport()) {
        vp->SetRenderMode(mode);
    }
}

void MainWindow::onCycleRenderMode() {
    IViewport* vp = activeViewport();
    if (!vp) {
        return;
    }
    const int next = (static_cast<int>(vp->GetRenderMode()) + 1) % 4;
    vp->SetRenderMode(static_cast<RenderMode>(next));
}

void MainWindow::onToggleGrid() {
    if (IViewport* vp = activeViewport()) {
        vp->SetGridVisible(!vp->IsGridVisible());
    }
}

void MainWindow::onToggleHud() {
    IViewport* vp = activeViewport();
    if (!ext_ || !vp) {
        return;
    }
    ext_->SetHudVisible(vp, !ext_->IsHudVisible(vp));
}

void MainWindow::onBackgroundColor() {
    IViewport* vp = activeViewport();
    if (!vp) {
        return;
    }
    const QColor picked =
        QColorDialog::getColor(QColor(20, 22, 26), this, QStringLiteral("背景色"));
    if (picked.isValid()) {
        vp->SetBackgroundColor(ColorFromQColor(picked));
    }
}

void MainWindow::onCameraParams() {
    ICamera* camera = activeCamera();
    if (!camera) {
        return;
    }
    CameraDialog dialog(*camera, ext_, this);
    if (dialog.exec() == QDialog::Accepted) {
        dialog.applyTo(*camera);
    }
}

void MainWindow::onPickFilterChanged() {
    IViewport* vp = activeViewport();
    if (!vp) {
        return;
    }
    uint32_t mask = PickFilter_None;
    for (int i = 0; i < pickFilterActions_.size(); ++i) {
        if (pickFilterActions_[i]->isChecked()) {
            mask |= kPickFilterTable[i].bit;
        }
    }
    vp->SetPickFilter(mask);
}

void MainWindow::onNewViewport() {
    if (views_.size() >= 4) {
        statusBar()->showMessage(QStringLiteral("视口开得够多了"), 3000);
        return;
    }
    addViewport(/*secondary=*/true);
}

void MainWindow::onCloseExtraViewports() {
    while (views_.size() > 1) {
        CadGeomWidget* victim = views_.takeLast();
        if (active_ == victim) {
            active_ = views_.isEmpty() ? nullptr : views_.first();
        }
        // 先藏起来：deleteLater 之前它还挂在分割条上，还会响应 paintEvent 再画一
        // 帧。析构里会 Release 视口；deleteLater 避开「正在自己的槽里删自己」那类栈。
        victim->hide();
        victim->deleteLater();
    }
}

// ---------------------------------------------------------------------------
// 捕捉 / 精度
// ---------------------------------------------------------------------------

void MainWindow::onSnapMaskChanged() {
    uint32_t mask = Snap_None;
    for (int i = 0; i < snapActions_.size(); ++i) {
        if (snapActions_[i]->isChecked()) {
            mask |= kSnapTable[i].bit;
        }
    }
    engine_.GetToolManager()->SetSnapMask(mask);
}

void MainWindow::onSnapTolerance() {
    bool ok = false;
    const double pixels = QInputDialog::getDouble(this, QStringLiteral("捕捉半径"),
                                                  QStringLiteral("像素"), 8.0, 1.0, 64.0, 1, &ok);
    if (ok) {
        engine_.GetToolManager()->SetSnapTolerance(pixels);
    }
}

void MainWindow::onContinuousMode(bool enabled) {
    engine_.GetToolManager()->SetContinuousMode(enabled);
}

void MainWindow::onTessParams() {
    IGeometryBuilder* builder = engine_.GetScene()->GetGeometryBuilder();
    TessParams params{};
    builder->GetTessParams(params);

    TessParamsDialog dialog(params, ext_, this);
    if (dialog.exec() == QDialog::Accepted) {
        builder->SetTessParams(dialog.params());
    }
}

// ---------------------------------------------------------------------------
// 测量
// ---------------------------------------------------------------------------

QString MainWindow::formatLength(double modelUnits) const {
    if (!ext_) {
        return QString::number(modelUnits, 'f', 3);
    }
    char buffer[64] = {};
    ext_->FormatLength(modelUnits, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

void MainWindow::onShowMeasurement() {
    Vec3d from{};
    Vec3d to{};
    double distance = 0.0;
    if (!ext_ || !ext_->GetMeasurement(from, to, distance)) {
        QMessageBox::information(this, QStringLiteral("测量"),
                                 QStringLiteral("还没量过任何东西。按 D 切到测量工具，"
                                                "点两个点。"));
        return;
    }
    const Vec3d delta{to.x - from.x, to.y - from.y, to.z - from.z};
    QMessageBox::information(
        this, QStringLiteral("最近一次测量"),
        QStringLiteral("起点：%1\n终点：%2\n\n距离：%3\nΔX：%4\nΔY：%5\nΔZ：%6")
            .arg(FormatVec(from), FormatVec(to), formatLength(distance), formatLength(delta.x),
                 formatLength(delta.y), formatLength(delta.z)));
}

void MainWindow::onEntityInfo() {
    const QVector<EntityId> targets = selectedEntities();
    if (targets.size() != 1) {
        statusBar()->showMessage(QStringLiteral("请先选中一个对象"), 3000);
        return;
    }
    IEntity* entity = engine_.GetScene()->GetEntity(targets.front());
    if (!entity) {
        return;
    }

    QString text = QStringLiteral("名称：%1\n类型：%2\nEntityId：%3\nShapeId：%4\n可见：%5")
                       .arg(QString::fromUtf8(entity->GetName()),
                            ShapeTypeName(entity->GetShapeType()))
                       .arg(entity->GetId().value)
                       .arg(entity->GetShape().value)
                       .arg(entity->IsVisible() ? QStringLiteral("是") : QStringLiteral("否"));

    Aabb bounds{};
    if (entity->GetWorldBounds(bounds)) {
        text += QStringLiteral("\n\n包围盒：\n  min %1\n  max %2\n  尺寸 %3 × %4 × %5")
                    .arg(FormatVec(bounds.min), FormatVec(bounds.max),
                         formatLength(bounds.max.x - bounds.min.x),
                         formatLength(bounds.max.y - bounds.min.y),
                         formatLength(bounds.max.z - bounds.min.z));
    }

    Transform local{};
    entity->GetLocalTransform(local);
    text += QStringLiteral("\n\n局部变换：\n  平移 %1\n  缩放 %2")
                .arg(FormatVec(local.translation), FormatVec(local.scale));

    QMessageBox::information(this, QStringLiteral("对象信息"), text);
}

void MainWindow::onUnitSettings() {
    if (!ext_) {
        return;
    }
    UnitSettings settings{};
    ext_->GetUnitSettings(settings);

    UnitSettingsDialog dialog(settings, this);
    if (dialog.exec() == QDialog::Accepted) {
        ext_->SetUnitSettings(dialog.settings());
    }
}

// ---------------------------------------------------------------------------
// 帮助
// ---------------------------------------------------------------------------

void MainWindow::onAbout() {
    const uint32_t runtime = CadGeom_GetApiVersion();
    QString samples = QStringLiteral("—");
    if (ext_ && activeViewport()) {
        samples = QStringLiteral("%1×").arg(ext_->GetSampleCount(activeViewport()));
    }

    QMessageBox::about(
        this, QStringLiteral("关于 CadGeom"),
        QStringLiteral(
            "<b>CadGeom — Qt 宿主示例</b><br><br>"
            "宿主头文件 API 版本：%1.%2.%3<br>"
            "运行时库 API 版本：%4.%5.%6<br>"
            "构建：%7<br>"
            "设备：%8<br>"
            "MSAA：%9<br>"
            "存活接口对象：%10<br><br>"
            "整个宿主只链接一个 cadgeom 共享库，全部通过纯虚接口通信 —— "
            "没有一个 STL 类型、没有一个异常跨过那道边界。")
            .arg(CADGEOM_API_VERSION_MAJOR)
            .arg(CADGEOM_API_VERSION_MINOR)
            .arg(CADGEOM_API_VERSION_PATCH)
            .arg(CADGEOM_VERSION_MAJOR(runtime))
            .arg(CADGEOM_VERSION_MINOR(runtime))
            .arg(CADGEOM_VERSION_PATCH(runtime))
            .arg(QString::fromUtf8(CadGeom_GetBuildInfo()))
            .arg(QString::fromUtf8(engine_.GetDeviceName()))
            .arg(samples)
            .arg(CadGeom_GetLiveObjectCount()));
}

// ---------------------------------------------------------------------------
// 每帧
// ---------------------------------------------------------------------------

void MainWindow::onFrame() {
    const double delta = clock_.restart() / 1000.0;

    // Tick 每帧**只调一次**：脏几何解析一次、上传一次，之后每个视口各画各的。
    engine_.Tick(delta);
    for (CadGeomWidget* view : views_) {
        view->renderFrame();
    }

    refreshTree();
    syncTreeSelection();
    syncStatusBar();
    syncActions();

    // 有模态框开着时不数帧、更不关窗：关掉主窗口而模态框的事件循环还在，程序就
    // 卡在那儿谁也退不出去。
    if (autoShotFrames_ > 0 && !QApplication::activeModalWidget() &&
        ++frameCount_ >= autoShotFrames_) {
        autoShotFrames_ = 0;
        if (!autoShotPath_.isEmpty()) {
            if (IViewport* vp = activeViewport()) {
                vp->SaveScreenshot(autoShotPath_.toUtf8().constData());
            }
        }
        close();
    }
}

void MainWindow::syncActions() {
    ICommandStack* stack = engine_.GetScene()->GetCommandStack();
    const char* undoName = stack->PeekUndoName();
    const char* redoName = stack->PeekRedoName();
    actUndo_->setEnabled(stack->CanUndo());
    actRedo_->setEnabled(stack->CanRedo());
    actUndo_->setText(undoName ? QStringLiteral("撤销 %1").arg(QString::fromUtf8(undoName))
                               : QStringLiteral("撤销"));
    actRedo_->setText(redoName ? QStringLiteral("重做 %1").arg(QString::fromUtf8(redoName))
                               : QStringLiteral("重做"));

    const uint32_t selected = engine_.GetScene()->GetSelection()->GetCount();
    actDelete_->setEnabled(selected > 0);
    actColor_->setEnabled(selected > 0);
    actStyle_->setEnabled(selected > 0);
    actToggleVisible_->setEnabled(selected > 0);
    actGroup_->setEnabled(selected > 0);
    actExportSelection_->setEnabled(selected > 0);
    actFitSelection_->setEnabled(selected > 0);
    actRename_->setEnabled(selected == 1);
    actEditParams_->setEnabled(selected == 1);
    actTransform_->setEnabled(selected == 1);
    actEntityInfo_->setEnabled(selected == 1);
    actCloseExtraViewports_->setEnabled(views_.size() > 1);

    const ToolId activeTool = engine_.GetToolManager()->GetActiveTool();
    for (int i = 0; i < toolActions_.size(); ++i) {
        toolActions_[i]->setChecked(kToolTable[i].id == activeTool);
    }

    const uint32_t snapMask = engine_.GetToolManager()->GetSnapMask();
    for (int i = 0; i < snapActions_.size(); ++i) {
        snapActions_[i]->setChecked((snapMask & kSnapTable[i].bit) != 0);
    }
    actContinuous_->setChecked(engine_.GetToolManager()->IsContinuousMode());

    IViewport* vp = activeViewport();
    const bool hasViewport = vp != nullptr;
    actGrid_->setEnabled(hasViewport);
    actHud_->setEnabled(hasViewport && ext_ != nullptr);
    actPerspective_->setEnabled(hasViewport);
    actWorkPlaneFromPick_->setEnabled(hasViewport);
    if (!hasViewport) {
        return;
    }

    actGrid_->setChecked(vp->IsGridVisible());
    actPerspective_->setChecked(vp->GetCamera()->GetProjection() == ProjectionMode::Perspective);
    if (ext_) {
        actHud_->setChecked(ext_->IsHudVisible(vp));
    }

    const RenderMode mode = vp->GetRenderMode();
    for (int i = 0; i < renderModeActions_.size(); ++i) {
        renderModeActions_[i]->setChecked(kRenderModeTable[i].mode == mode);
    }

    const uint32_t pickFilter = vp->GetPickFilter();
    for (int i = 0; i < pickFilterActions_.size(); ++i) {
        pickFilterActions_[i]->setChecked((pickFilter & kPickFilterTable[i].bit) != 0);
    }
}

void MainWindow::syncStatusBar() {
    IViewport* vp = activeViewport();

    // 工具写给状态栏的提示语。引擎自绘的 HUD 显示的是同一份文本的 ASCII 版本 ——
    // 宿主拿到的是原始字符串，想怎么排版都行。
    QString prompt;
    if (ext_) {
        prompt = QString::fromUtf8(ext_->GetStatusText(vp));
    }
    if (prompt.isEmpty()) {
        prompt = QStringLiteral("就绪");
    }
    if (statusPrompt_->text() != prompt) {
        statusPrompt_->setText(prompt);
    }

    const ISelection* selection = engine_.GetScene()->GetSelection();
    QString selectionText = QStringLiteral("选中 %1 / %2")
                                .arg(selection->GetCount())
                                .arg(engine_.GetScene()->GetEntityCount());
    if (selection->GetCount() == 1) {
        if (const IEntity* e = engine_.GetScene()->GetEntity(selection->GetAt(0))) {
            selectionText += QStringLiteral("：%1").arg(QString::fromUtf8(e->GetName()));
        }
    }
    if (statusSelection_->text() != selectionText) {
        statusSelection_->setText(selectionText);
    }

    QString measureText = QStringLiteral("测量 —");
    Vec3d from{};
    Vec3d to{};
    double distance = 0.0;
    if (ext_ && ext_->GetMeasurement(from, to, distance)) {
        measureText = QStringLiteral("测量 %1").arg(formatLength(distance));
    }
    if (statusMeasure_->text() != measureText) {
        statusMeasure_->setText(measureText);
    }

    QString deviceText = QString::fromUtf8(engine_.GetDeviceName());
    if (ext_ && vp) {
        UnitSettings settings{};
        ext_->GetUnitSettings(settings);
        deviceText += QStringLiteral(" · %1× MSAA · %2")
                          .arg(ext_->GetSampleCount(vp))
                          .arg(LengthUnitName(settings.displayUnit));
    }
    if (statusDevice_->text() != deviceText) {
        statusDevice_->setText(deviceText);
    }
}

// ---------------------------------------------------------------------------
// 模型树
// ---------------------------------------------------------------------------

namespace {

/// @brief 递归建一个树节点。EntityId 存在 UserRole 里，回头点树时要拿它。
QTreeWidgetItem* BuildTreeItem(const IScene& scene, EntityId id, QTreeWidgetItem* parentItem,
                               QTreeWidget* tree) {
    const IEntity* entity = scene.GetEntity(id);
    if (!entity) {
        return nullptr;
    }
    auto* item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(tree);
    item->setText(0, QString::fromUtf8(entity->GetName()));
    item->setText(1, ShapeTypeName(entity->GetShapeType()));
    item->setData(0, Qt::UserRole, static_cast<qulonglong>(id.value));
    item->setCheckState(0, entity->IsVisible() ? Qt::Checked : Qt::Unchecked);

    for (uint32_t i = 0; i < entity->GetChildCount(); ++i) {
        BuildTreeItem(scene, entity->GetChild(i), item, tree);
    }
    return item;
}

} // namespace

void MainWindow::refreshTree() {
    const uint64_t revision = engine_.GetScene()->GetRevision();
    if (haveRevision_ && revision == lastRevision_) {
        return;
    }
    lastRevision_ = revision;
    haveRevision_ = true;

    const IScene& scene = *engine_.GetScene();
    syncingTree_ = true;
    tree_->blockSignals(true);
    tree_->clear();
    for (uint32_t i = 0; i < scene.GetRootCount(); ++i) {
        BuildTreeItem(scene, scene.GetRootAt(i), nullptr, tree_);
    }
    tree_->expandAll();
    tree_->blockSignals(false);
    syncingTree_ = false;

    // 树重建之后选中状态也没了，下一句 syncTreeSelection 会补上。
    lastSelectionSignature_ = 0;
}

void MainWindow::syncTreeSelection() {
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

    syncingTree_ = true;
    tree_->blockSignals(true);
    QTreeWidgetItemIterator it(tree_);
    while (*it) {
        const EntityId id{(*it)->data(0, Qt::UserRole).toULongLong()};
        (*it)->setSelected(selection->Contains(id));
        ++it;
    }
    tree_->blockSignals(false);
    syncingTree_ = false;
}

void MainWindow::onTreeSelectionChanged() {
    if (syncingTree_) {
        return;
    }
    std::vector<EntityId> ids;
    const QList<QTreeWidgetItem*> items = tree_->selectedItems();
    ids.reserve(items.size());
    for (QTreeWidgetItem* item : items) {
        ids.push_back(EntityId{item->data(0, Qt::UserRole).toULongLong()});
    }
    syncingTree_ = true;
    engine_.GetScene()->GetSelection()->Set(CgSpan<const EntityId>{ids.data(), ids.size()});
    syncingTree_ = false;
}

void MainWindow::onTreeItemChanged(QTreeWidgetItem* item, int column) {
    if (syncingTree_ || column != 0 || !item) {
        return;
    }
    const EntityId id{item->data(0, Qt::UserRole).toULongLong()};
    const bool visible = item->checkState(0) == Qt::Checked;
    IEntity* entity = engine_.GetScene()->GetEntity(id);
    if (!entity || entity->IsVisible() == visible) {
        return;
    }
    engine_.GetScene()->GetCommandStack()->Push(
        new StyleCommand(std::vector<EntityId>{id},
                         [visible](EntityStyle& style) { style.visible = visible; },
                         visible ? "Show" : "Hide"));
}

// ---------------------------------------------------------------------------
// 杂项
// ---------------------------------------------------------------------------

void MainWindow::appendLog(int level, const QString& text) {
    if (!log_) {
        return;
    }
    static const char* const kTags[] = {"trace", "debug", "info ", "WARN ", "ERROR", "FATAL", ""};
    const int index = (level >= 0 && level < 7) ? level : 2;
    log_->appendPlainText(QStringLiteral("[%1] %2").arg(QString::fromUtf8(kTags[index]), text));
}

void MainWindow::reportError(const QString& what) {
    const QString detail = QString::fromUtf8(engine_.GetLastErrorMessage());
    appendLog(static_cast<int>(LogLevel::Error), QStringLiteral("%1：%2").arg(what, detail));
    QMessageBox::warning(this, QStringLiteral("CadGeom"),
                         QStringLiteral("%1\n\n%2").arg(what, detail));
    engine_.ClearLastError();
}
