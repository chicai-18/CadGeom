/**
 * @file ViewportWnd.cpp
 * @brief CViewportWnd 的实现 —— 消息翻译层，见头文件。
 */
#include "ViewportWnd.h"

#include "MainFrame.h"
#include "Utf8.h"

using namespace cadgeom;

namespace {

/// @brief 当前的修饰键状态 → 引擎的 KeyMod 位组合。
///
/// Win32 的 `nFlags` 只带 MK_CONTROL 和 MK_SHIFT，没有 Alt，所以三个键统一问
/// GetKeyState —— 少一个分支，也少一处「鼠标事件有 Alt 而键盘事件没有」的不一致。
uint32_t CurrentKeyMods() {
    uint32_t mods = KeyMod_None;
    if (::GetKeyState(VK_SHIFT) < 0) {
        mods |= KeyMod_Shift;
    }
    if (::GetKeyState(VK_CONTROL) < 0) {
        mods |= KeyMod_Ctrl;
    }
    if (::GetKeyState(VK_MENU) < 0) {
        mods |= KeyMod_Alt;
    }
    if (::GetKeyState(VK_LWIN) < 0 || ::GetKeyState(VK_RWIN) < 0) {
        mods |= KeyMod_Super;
    }
    return mods;
}

/// @brief 虚拟键码 → 引擎的键码。
/// @note 可打印字符两边都用大写 ASCII（VK_A 本来就是 'A'），所以只有功能键需要一
///       张表。表外的键返回 Key_Unknown，转发过去也不会有人认。
int32_t ToEngineKey(UINT vk) {
    switch (vk) {
        case VK_ESCAPE: return Key_Escape;
        case VK_RETURN: return Key_Enter;
        case VK_TAB: return Key_Tab;
        case VK_BACK: return Key_Backspace;
        case VK_INSERT: return Key_Insert;
        case VK_DELETE: return Key_Delete;
        case VK_RIGHT: return Key_Right;
        case VK_LEFT: return Key_Left;
        case VK_DOWN: return Key_Down;
        case VK_UP: return Key_Up;
        case VK_HOME: return Key_Home;
        case VK_END: return Key_End;
        case VK_F1: return Key_F1;
        default: break;
    }
    if (vk >= 0x20 && vk < 0x7F) {
        return static_cast<int32_t>(vk);
    }
    return Key_Unknown;
}

/// @brief 视口用的窗口类：响应双击，光标是十字，**没有背景刷**。
///
/// 背景刷给 null 是关键的一笔：有刷子的话 DefWindowProc 会在每次 WM_ERASEBKGND
/// 里把客户区涂一遍，交换链刚呈现的画面就被盖成一块灰的，表现为闪烁。
LPCTSTR ViewportWndClass() {
    return AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_CROSS), nullptr, nullptr);
}

} // namespace

// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CViewportWnd, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_DESTROY()
    ON_WM_SETFOCUS()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_RBUTTONDOWN()
    ON_WM_RBUTTONUP()
    ON_WM_RBUTTONDBLCLK()
    ON_WM_MBUTTONDOWN()
    ON_WM_MBUTTONUP()
    ON_WM_MBUTTONDBLCLK()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSEWHEEL()
    ON_WM_MOUSELEAVE()
    ON_WM_KEYDOWN()
    ON_WM_KEYUP()
END_MESSAGE_MAP()

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

CViewportWnd::CViewportWnd(ICadEngine& engine, CMainFrame* owner, const ViewportDesc& desc)
    : engine_(engine), owner_(owner), desc_(desc) {}

CViewportWnd::~CViewportWnd() {
    // 正常路径上 OnDestroy 已经放过了；这里是最后一道保险。视口是宿主可以提前
    // 释放的少数引擎对象之一（IEngine.h），漏掉一次 CadGeom_GetLiveObjectCount()
    // 就回不到 0，退出码变成 2。
    if (viewport_) {
        viewport_->Release();
        viewport_ = nullptr;
    }
}

BOOL CViewportWnd::CreateIn(CWnd* parent, const CRect& rect, UINT id) {
    return CWnd::Create(ViewportWndClass(), nullptr,
                        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, rect, parent,
                        id);
}

void CViewportWnd::OnDestroy() {
    if (viewport_) {
        viewport_->Release();
        viewport_ = nullptr;
    }
    CWnd::OnDestroy();
}

bool CViewportWnd::EnsureViewport() {
    if (viewport_ || creationAttempted_) {
        return viewport_ != nullptr;
    }
    if (!::IsWindow(m_hWnd)) {
        return false;
    }
    CRect client;
    GetClientRect(&client);
    if (client.Width() <= 0 || client.Height() <= 0) {
        // 还没有面积可画。这不是失败，下一个 WM_SIZE 会再来问一次。
        return false;
    }
    creationAttempted_ = true;

    desc_.surface.kind = SurfaceKind::NativeWin32;
    desc_.surface.nativeWindow = m_hWnd;
    // hinstance 留空：引擎会自己从窗口上问出来（Surface.cpp 里的 GWLP_HINSTANCE）。
    desc_.surface.nativeDisplay = nullptr;
    desc_.surface.width = static_cast<uint32_t>(client.Width());
    desc_.surface.height = static_cast<uint32_t>(client.Height());
    desc_.surface.title = nullptr;  // 标题是宿主窗口的事，引擎不开窗口

    viewport_ = engine_.CreateViewport(desc_);
    if (!viewport_) {
        const CString reason = FromUtf8(engine_.GetLastErrorMessage());
        engine_.ClearLastError();
        if (owner_) {
            owner_->OnViewportFailed(this, reason);
        }
        return false;
    }
    if (owner_) {
        owner_->OnViewportReady(this);
    }
    return true;
}

void CViewportWnd::RenderFrame() {
    if (!EnsureViewport()) {
        return;
    }
    viewport_->Render();
}

void CViewportWnd::LastCursorPixel(double& x, double& y) const {
    x = cursorX_;
    y = cursorY_;
}

// ---------------------------------------------------------------------------
// 窗口消息
// ---------------------------------------------------------------------------

void CViewportWnd::OnPaint() {
    // CPaintDC 的析构会 EndPaint，把无效区清掉 —— 少了它 WM_PAINT 会一直重来。
    // 画面本身不是 GDI 画的，所以这个 DC 一笔都不用。
    CPaintDC dc(this);
    // 拖窗口边框的时候 Windows 会连着发 WM_PAINT 而定时器插不进来，这里补画一帧，
    // 缩放过程才不是一块冻住的画面。Tick 归主窗口，这里只重画。
    if (EnsureViewport()) {
        viewport_->Render();
    }
}

BOOL CViewportWnd::OnEraseBkgnd(CDC* /*dc*/) {
    // 「这块地方归别人画」。窗口类本来就没有背景刷，这里再挡一道，免得将来有人
    // 给类加上刷子之后画面开始闪。
    return TRUE;
}

void CViewportWnd::OnSize(UINT type, int cx, int cy) {
    CWnd::OnSize(type, cx, cy);
    if (!viewport_) {
        // 第一次拿到尺寸就是建视口的时机。
        EnsureViewport();
        return;
    }
    // 交换链重建。零尺寸（最小化）是允许的，引擎会停下来等一个非零尺寸。
    viewport_->Resize(static_cast<uint32_t>(cx < 0 ? 0 : cx),
                      static_cast<uint32_t>(cy < 0 ? 0 : cy));
}

void CViewportWnd::OnSetFocus(CWnd* oldWnd) {
    CWnd::OnSetFocus(oldWnd);
    if (owner_) {
        owner_->SetActiveViewport(this);
    }
}

// ---------------------------------------------------------------------------
// 输入转发
// ---------------------------------------------------------------------------

void CViewportWnd::DispatchMouse(CPoint point, MouseButton button, MouseAction action,
                                 double wheelDelta) {
    // 进程是 per-monitor v2 DPI 感知的（CMfcViewerApp::InitInstance），所以客户区
    // 坐标就是物理像素，和引擎那边 GetClientRect 量出来的是同一套数 —— Qt 那边要
    // 乘一个 devicePixelRatio，这里不用。
    cursorX_ = static_cast<double>(point.x);
    cursorY_ = static_cast<double>(point.y);

    if (!viewport_) {
        return;
    }
    MouseEvent e{};
    e.button = button;
    e.action = action;
    e.x = cursorX_;
    e.y = cursorY_;
    e.wheelDelta = wheelDelta;
    e.mods = CurrentKeyMods();
    if (viewport_->OnMouseEvent(e) && owner_) {
        // 引擎吃掉了一次输入 —— 工具状态多半变了，状态栏立刻跟上，不用等下一帧。
        owner_->OnEngineConsumedInput();
    }
}

void CViewportWnd::PressButton(MouseButton button, CPoint point, MouseAction action) {
    if (owner_) {
        owner_->SetActiveViewport(this);
    }
    if (::GetFocus() != m_hWnd) {
        SetFocus();
    }
    // Win32 不像 Qt 那样自动隐式抓取：不 SetCapture 的话，拖到窗口外面就收不到
    // 移动和抬起了 —— 环绕视角拖出边界会卡在半路。
    if (buttonsDown_++ == 0) {
        SetCapture();
    }
    DispatchMouse(point, button, action, 0.0);
}

void CViewportWnd::ReleaseButton(MouseButton button, CPoint point) {
    if (buttonsDown_ > 0 && --buttonsDown_ == 0 && ::GetCapture() == m_hWnd) {
        ::ReleaseCapture();
    }
    DispatchMouse(point, button, MouseAction::Up, 0.0);
}

void CViewportWnd::OnLButtonDown(UINT /*flags*/, CPoint point) {
    PressButton(MouseButton::Left, point, MouseAction::Down);
}

void CViewportWnd::OnLButtonUp(UINT /*flags*/, CPoint point) {
    ReleaseButton(MouseButton::Left, point);
}

void CViewportWnd::OnLButtonDblClk(UINT /*flags*/, CPoint point) {
    PressButton(MouseButton::Left, point, MouseAction::DoubleClick);
}

void CViewportWnd::OnRButtonDown(UINT /*flags*/, CPoint point) {
    PressButton(MouseButton::Right, point, MouseAction::Down);
}

void CViewportWnd::OnRButtonUp(UINT /*flags*/, CPoint point) {
    ReleaseButton(MouseButton::Right, point);
}

void CViewportWnd::OnRButtonDblClk(UINT /*flags*/, CPoint point) {
    PressButton(MouseButton::Right, point, MouseAction::DoubleClick);
}

void CViewportWnd::OnMButtonDown(UINT /*flags*/, CPoint point) {
    PressButton(MouseButton::Middle, point, MouseAction::Down);
}

void CViewportWnd::OnMButtonUp(UINT /*flags*/, CPoint point) {
    ReleaseButton(MouseButton::Middle, point);
}

void CViewportWnd::OnMButtonDblClk(UINT /*flags*/, CPoint point) {
    // 中键双击是引擎内建的「缩放至全部」。
    PressButton(MouseButton::Middle, point, MouseAction::DoubleClick);
}

void CViewportWnd::OnMouseMove(UINT /*flags*/, CPoint point) {
    if (!trackingLeave_) {
        // WM_MOUSELEAVE 要主动订阅，而且是一次性的：收到之后要重新订阅。
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = m_hWnd;
        if (::TrackMouseEvent(&track)) {
            trackingLeave_ = true;
            DispatchMouse(point, MouseButton::None, MouseAction::Enter, 0.0);
        }
    }
    // 移动事件不带「哪个键按着」，引擎自己记着拖拽状态，这里照实报 None。
    DispatchMouse(point, MouseButton::None, MouseAction::Move, 0.0);
}

void CViewportWnd::OnMouseLeave() {
    trackingLeave_ = false;
    if (viewport_) {
        MouseEvent e{};
        e.button = MouseButton::None;
        e.action = MouseAction::Leave;
        e.x = cursorX_;
        e.y = cursorY_;
        e.mods = CurrentKeyMods();
        viewport_->OnMouseEvent(e);
    }
}

BOOL CViewportWnd::OnMouseWheel(UINT /*flags*/, short delta, CPoint point) {
    // WM_MOUSEWHEEL 的坐标是**屏幕**坐标，别忘了换。
    ScreenToClient(&point);
    if (owner_) {
        owner_->SetActiveViewport(this);
    }
    // 一格 120，引擎收的是「格数」，正数为推远手指 = 拉近视图。
    DispatchMouse(point, MouseButton::None, MouseAction::Wheel, delta / 120.0);
    return TRUE;
}

void CViewportWnd::DispatchKey(UINT vk, KeyAction action, bool autoRepeat) {
    if (!viewport_) {
        return;
    }
    KeyEvent e{};
    e.key = ToEngineKey(vk);
    e.action = (action == KeyAction::Down && autoRepeat) ? KeyAction::Repeat : action;
    e.mods = CurrentKeyMods();
    if (e.key != Key_Unknown && viewport_->OnKeyEvent(e) && owner_) {
        owner_->OnEngineConsumedInput();
    }
}

void CViewportWnd::OnKeyDown(UINT ch, UINT repeat, UINT flags) {
    // 走到这里的，都是**没有**被快捷键表截走的键。引擎自己也认一套字母键
    // （V/L/C/… 切工具、1~7 切视图），但那些字母在这个宿主里挂在 ACCELERATORS 上，
    // CFrameWnd::PreTranslateMessage 会先把它们变成菜单命令，于是同一个键只会被
    // 处理一次。剩下的 —— Esc 取消、Enter 结束多段线 —— 正是工具自己要的那几个。
    DispatchKey(ch, KeyAction::Down, (flags & KF_REPEAT) != 0 || repeat > 1);
    CWnd::OnKeyDown(ch, repeat, flags);
}

void CViewportWnd::OnKeyUp(UINT ch, UINT repeat, UINT flags) {
    DispatchKey(ch, KeyAction::Up, false);
    CWnd::OnKeyUp(ch, repeat, flags);
}
