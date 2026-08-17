/**
 * @file CadGeomWidget.cpp
 * @brief CadGeomWidget 的实现 —— 事件翻译层，见头文件。
 */
#include "CadGeomWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>

#include <algorithm>

using namespace cadgeom;

namespace {

/// @brief Qt 的按键修饰位 → 引擎的 KeyMod 位组合。
uint32_t ToKeyMods(Qt::KeyboardModifiers mods) {
    uint32_t out = KeyMod_None;
    if (mods & Qt::ShiftModifier) {
        out |= KeyMod_Shift;
    }
    if (mods & Qt::ControlModifier) {
        out |= KeyMod_Ctrl;
    }
    if (mods & Qt::AltModifier) {
        out |= KeyMod_Alt;
    }
    if (mods & Qt::MetaModifier) {
        out |= KeyMod_Super;
    }
    return out;
}

/// @brief Qt 的鼠标键 → 引擎的 MouseButton。
MouseButton ToMouseButton(Qt::MouseButton button) {
    switch (button) {
        case Qt::LeftButton: return MouseButton::Left;
        case Qt::RightButton: return MouseButton::Right;
        case Qt::MiddleButton: return MouseButton::Middle;
        default: return MouseButton::None;
    }
}

/// @brief Qt 的键码 → 引擎的键码。
/// @note 可打印字符两边都用大写 ASCII（Qt::Key_A 本来就是 'A'），所以只有功能键
///       需要一张表。表外的键返回 Key_Unknown，转发过去也不会有人认。
int32_t ToEngineKey(int qtKey) {
    switch (qtKey) {
        case Qt::Key_Escape: return Key_Escape;
        case Qt::Key_Return:
        case Qt::Key_Enter: return Key_Enter;
        case Qt::Key_Tab: return Key_Tab;
        case Qt::Key_Backspace: return Key_Backspace;
        case Qt::Key_Insert: return Key_Insert;
        case Qt::Key_Delete: return Key_Delete;
        case Qt::Key_Right: return Key_Right;
        case Qt::Key_Left: return Key_Left;
        case Qt::Key_Down: return Key_Down;
        case Qt::Key_Up: return Key_Up;
        case Qt::Key_Home: return Key_Home;
        case Qt::Key_End: return Key_End;
        case Qt::Key_F1: return Key_F1;
        default: break;
    }
    if (qtKey >= 0x20 && qtKey < 0x7F) {
        return static_cast<int32_t>(qtKey);
    }
    return Key_Unknown;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
QPointF LocalPos(const QMouseEvent* e) { return e->position(); }
#else
QPointF LocalPos(const QMouseEvent* e) { return e->localPos(); }
#endif

} // namespace

// ---------------------------------------------------------------------------

CadGeomWidget::CadGeomWidget(ICadEngine& engine, const ViewportDesc& desc, QWidget* parent)
    : QWidget(parent), engine_(engine), desc_(desc) {
    // 这四个属性合起来是「这块地方归别人画」的意思，缺一不可：没有
    // WA_NativeWindow 就没有 HWND；没有 WA_PaintOnScreen（配合 paintEngine() 返
    // 回 null）Qt 的 backing store 会盖住交换链；后两个免掉一次每帧的清背景。
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);

    // 工具要键盘（Esc 取消、Enter 结束多段线），拾取高亮要没按键时的移动事件。
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 240);
}

CadGeomWidget::~CadGeomWidget() {
    // 视口是宿主可以提前释放的少数引擎对象之一（IEngine.h）。这里必须显式放掉：
    // 引擎析构时也会收，但那时控件已经没了，谁先谁后就成了运气。
    if (viewport_) {
        viewport_->Release();
        viewport_ = nullptr;
    }
}

// ---------------------------------------------------------------------------

bool CadGeomWidget::ensureViewport() {
    if (viewport_ || creationAttempted_) {
        return viewport_ != nullptr;
    }
    creationAttempted_ = true;

    uint32_t w = 0;
    uint32_t h = 0;
    devicePixelSize(w, h);

    desc_.surface.width = w;
    desc_.surface.height = h;
    desc_.surface.title = nullptr;  // 标题是 Qt 窗口的事，引擎不开窗口
#if defined(Q_OS_WIN)
    desc_.surface.kind = SurfaceKind::NativeWin32;
#elif defined(Q_OS_MACOS)
    desc_.surface.kind = SurfaceKind::NativeCocoa;
#else
    desc_.surface.kind = SurfaceKind::NativeXlib;
#endif
    // winId() 在 WA_NativeWindow 之下必然给出一个已经存在的原生窗口句柄。
    desc_.surface.nativeWindow = reinterpret_cast<void*>(winId());

    viewport_ = engine_.CreateViewport(desc_);
    if (!viewport_) {
        emit viewportFailed(QString::fromUtf8(engine_.GetLastErrorMessage()));
        return false;
    }
    emit viewportReady();
    return true;
}

void CadGeomWidget::devicePixelSize(uint32_t& width, uint32_t& height) const {
    // 引擎那边的 NativeSurface 是拿 GetClientRect 量的，量出来是物理像素；Qt 的
    // width()/height() 是逻辑像素。高 DPI 下两者差一个 devicePixelRatio，鼠标坐标
    // 也一样 —— 不折算的话，拾取会整体偏到左上角去。
    const qreal ratio = devicePixelRatioF();
    width = static_cast<uint32_t>(std::max<int>(qRound(this->width() * ratio), 0));
    height = static_cast<uint32_t>(std::max<int>(qRound(this->height() * ratio), 0));
}

void CadGeomWidget::lastCursorPixel(double& x, double& y) const {
    x = cursorX_;
    y = cursorY_;
}

void CadGeomWidget::renderFrame() {
    if (!viewport_) {
        return;
    }
    viewport_->Render();
}

// ---------------------------------------------------------------------------
// 窗口事件
// ---------------------------------------------------------------------------

void CadGeomWidget::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    ensureViewport();
}

void CadGeomWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (!viewport_) {
        return;
    }
    uint32_t w = 0;
    uint32_t h = 0;
    devicePixelSize(w, h);
    // 交换链重建。零尺寸（最小化）是允许的，引擎会停下来等一个非零尺寸。
    viewport_->Resize(w, h);
}

void CadGeomWidget::paintEvent(QPaintEvent* e) {
    Q_UNUSED(e);
    // 拖动窗口边框时 Qt 会连着发 paintEvent 而定时器插不进来，这里补画一帧，
    // 缩放过程才不是一块冻住的画面。Tick 归宿主，这里只重画。
    if (viewport_) {
        viewport_->Render();
    }
}

// ---------------------------------------------------------------------------
// 输入转发
// ---------------------------------------------------------------------------

void CadGeomWidget::dispatchMouse(const QPointF& localPos, MouseButton button, MouseAction action,
                                  double wheelDelta, Qt::KeyboardModifiers mods) {
    const qreal ratio = devicePixelRatioF();
    cursorX_ = localPos.x() * ratio;
    cursorY_ = localPos.y() * ratio;

    if (!viewport_) {
        return;
    }
    MouseEvent e{};
    e.button = button;
    e.action = action;
    e.x = cursorX_;
    e.y = cursorY_;
    e.wheelDelta = wheelDelta;
    e.mods = ToKeyMods(mods);
    if (viewport_->OnMouseEvent(e)) {
        emit engineConsumedInput();
    }
}

void CadGeomWidget::mousePressEvent(QMouseEvent* e) {
    emit activated();
    dispatchMouse(LocalPos(e), ToMouseButton(e->button()), MouseAction::Down, 0.0, e->modifiers());
    e->accept();
}

void CadGeomWidget::mouseReleaseEvent(QMouseEvent* e) {
    dispatchMouse(LocalPos(e), ToMouseButton(e->button()), MouseAction::Up, 0.0, e->modifiers());
    e->accept();
}

void CadGeomWidget::mouseMoveEvent(QMouseEvent* e) {
    // 移动事件不带「哪个键按着」，引擎自己记着拖拽状态，这里照实报 None。
    dispatchMouse(LocalPos(e), MouseButton::None, MouseAction::Move, 0.0, e->modifiers());
    e->accept();
}

void CadGeomWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    dispatchMouse(LocalPos(e), ToMouseButton(e->button()), MouseAction::DoubleClick, 0.0,
                  e->modifiers());
    e->accept();
}

void CadGeomWidget::wheelEvent(QWheelEvent* e) {
    emit activated();
    // 一格 120，引擎收的是「格数」，正数为推远手指 = 拉近视图。
    const double notches = e->angleDelta().y() / 120.0;
    dispatchMouse(e->position(), MouseButton::None, MouseAction::Wheel, notches, e->modifiers());
    e->accept();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void CadGeomWidget::enterEvent(QEnterEvent* e) {
    dispatchMouse(e->position(), MouseButton::None, MouseAction::Enter, 0.0, Qt::NoModifier);
    QWidget::enterEvent(e);
}
#else
void CadGeomWidget::enterEvent(QEvent* e) {
    if (viewport_) {
        MouseEvent me{};
        me.button = MouseButton::None;
        me.action = MouseAction::Enter;
        me.x = cursorX_;
        me.y = cursorY_;
        viewport_->OnMouseEvent(me);
    }
    QWidget::enterEvent(e);
}
#endif

void CadGeomWidget::leaveEvent(QEvent* e) {
    if (viewport_) {
        MouseEvent me{};
        me.button = MouseButton::None;
        me.action = MouseAction::Leave;
        me.x = cursorX_;
        me.y = cursorY_;
        viewport_->OnMouseEvent(me);
    }
    QWidget::leaveEvent(e);
}

void CadGeomWidget::dispatchKey(QKeyEvent* e, KeyAction action) {
    if (!viewport_) {
        e->ignore();
        return;
    }
    KeyEvent ke{};
    ke.key = ToEngineKey(e->key());
    ke.action = (action == KeyAction::Down && e->isAutoRepeat()) ? KeyAction::Repeat : action;
    ke.mods = ToKeyMods(e->modifiers());
    if (ke.key != Key_Unknown && viewport_->OnKeyEvent(ke)) {
        e->accept();
        emit engineConsumedInput();
        return;
    }
    e->ignore();
}

void CadGeomWidget::keyPressEvent(QKeyEvent* e) {
    // 走到这里的，都是**没有**被菜单快捷键截走的键。引擎自己也认一套字母键
    // （V/L/C/... 切工具、1~7 切视图），但那些字母在这个宿主里已经挂在 QAction 上，
    // Qt 会先把它们变成菜单动作，于是同一个键只会被处理一次。剩下的 —— Esc 取消、
    // Enter 结束多段线 —— 正是工具自己要的那几个。
    dispatchKey(e, KeyAction::Down);
    if (!e->isAccepted()) {
        QWidget::keyPressEvent(e);
    }
}

void CadGeomWidget::keyReleaseEvent(QKeyEvent* e) {
    dispatchKey(e, KeyAction::Up);
    if (!e->isAccepted()) {
        QWidget::keyReleaseEvent(e);
    }
}

void CadGeomWidget::focusInEvent(QFocusEvent* e) {
    emit activated();
    QWidget::focusInEvent(e);
}
