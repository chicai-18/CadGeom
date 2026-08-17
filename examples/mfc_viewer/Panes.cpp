/**
 * @file Panes.cpp
 * @brief Panes.h 的实现。
 */
#include "Panes.h"

#include "MainFrame.h"
#include "ViewportWnd.h"

using namespace cadgeom;

/// @brief 树控件里点了一下复选框；带着 HTREEITEM 投递给自己，晚一步再读状态。
///
/// NM_CLICK 是在控件**自己**把勾翻过来之前发出来的，当场读到的是旧值。投一条消息
/// 出去，等控件处理完这次点击，回到消息循环时读到的才是新值。
#define WM_CADGEOM_TREECHECK (WM_APP + 2)

namespace {

/// 视口之间留的缝，像素。
constexpr int kViewGap = 4;

/// @brief 一个只有背景色、不响应双击的通用子窗口类。
LPCTSTR PaneWndClass() {
    return AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// CViewArea
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CViewArea, CWnd)
    ON_WM_SIZE()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CViewArea::~CViewArea() {
    for (CViewportWnd* view : views_) {
        delete view;
    }
    views_.clear();
}

BOOL CViewArea::CreateIn(CWnd* parent, UINT id) {
    return CWnd::Create(PaneWndClass(), nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                        CRect(0, 0, 0, 0), parent, id);
}

void CViewArea::Attach(ICadEngine* engine, CMainFrame* owner) {
    engine_ = engine;
    owner_ = owner;
}

CViewportWnd* CViewArea::At(int index) const {
    if (index < 0 || index >= static_cast<int>(views_.size())) {
        return nullptr;
    }
    return views_[static_cast<size_t>(index)];
}

CViewportWnd* CViewArea::AddViewport(bool secondary) {
    if (!engine_) {
        return nullptr;
    }

    ViewportDesc desc{};
    // 线性光里的「深灰」大约是 0.04，不是 0.2。
    desc.background = Color{0.035f, 0.038f, 0.045f, 1.0f};
    desc.projection = ProjectionMode::Orthographic;
    desc.showGrid = true;
    desc.vsync = true;
    desc.sampleCount = 4;

    auto* view = new CViewportWnd(*engine_, owner_, desc);
    if (!view->CreateIn(this, CRect(0, 0, 0, 0), nextId_++)) {
        delete view;
        return nullptr;
    }
    view->SetSecondary(secondary);
    views_.push_back(view);
    Layout();
    return view;
}

void CViewArea::CloseExtraViewports() {
    while (views_.size() > 1) {
        CViewportWnd* victim = views_.back();
        views_.pop_back();
        if (owner_) {
            owner_->OnViewportClosing(victim);
        }
        // DestroyWindow 会走 OnDestroy，视口在那儿被 Release；窗口对象是我们 new
        // 的，所以也由我们 delete —— CWnd 的默认 PostNcDestroy 什么都不做。
        victim->DestroyWindow();
        delete victim;
    }
    Layout();
}

void CViewArea::RenderAll() {
    for (CViewportWnd* view : views_) {
        view->RenderFrame();
    }
}

void CViewArea::OnSize(UINT type, int cx, int cy) {
    CWnd::OnSize(type, cx, cy);
    Layout();
}

BOOL CViewArea::OnEraseBkgnd(CDC* dc) {
    // 只有视口之间那几像素的缝要擦，其余地方马上会被交换链盖住。
    CRect client;
    GetClientRect(&client);
    dc->FillSolidRect(client, ::GetSysColor(COLOR_3DSHADOW));
    return TRUE;
}

void CViewArea::Layout() {
    if (!::IsWindow(m_hWnd) || views_.empty()) {
        return;
    }
    CRect client;
    GetClientRect(&client);

    const int count = static_cast<int>(views_.size());
    const int total = client.Width() - kViewGap * (count - 1);
    // 不用 std::max：windows.h 把 max 定义成了宏，`std::max(` 过不了预处理。
    const int width = total / count > 1 ? total / count : 1;

    int x = client.left;
    for (int i = 0; i < count; ++i) {
        // 最后一块吃掉除不尽的那几像素，右边缘才不会留一条缝。
        const int last = client.right - x > 1 ? client.right - x : 1;
        const int w = (i == count - 1) ? last : width;
        views_[static_cast<size_t>(i)]->MoveWindow(x, client.top, w, client.Height());
        x += w + kViewGap;
    }
}

// ---------------------------------------------------------------------------
// CTreePane
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CTreePane, CWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_NOTIFY(TVN_SELCHANGED, 1, &CTreePane::OnTreeSelChanged)
    ON_NOTIFY(NM_CLICK, 1, &CTreePane::OnTreeClick)
    ON_MESSAGE(WM_CADGEOM_TREECHECK, &CTreePane::OnDeferredCheck)
END_MESSAGE_MAP()

BOOL CTreePane::CreateIn(CWnd* parent, UINT id) {
    return CWnd::Create(PaneWndClass(), nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                        CRect(0, 0, 0, 0), parent, id);
}

int CTreePane::OnCreate(LPCREATESTRUCT cs) {
    if (CWnd::OnCreate(cs) == -1) {
        return -1;
    }
    // TVS_CHECKBOXES 只能在建控件时给：控件靠它准备状态图像列表，事后 ModifyStyle
    // 会得到一棵勾不上的树。
    if (!tree_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASBUTTONS | TVS_HASLINES |
                          TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_CHECKBOXES |
                          TVS_DISABLEDRAGDROP,
                      CRect(0, 0, 0, 0), this, 1)) {
        return -1;
    }
    return 0;
}

void CTreePane::OnSize(UINT type, int cx, int cy) {
    CWnd::OnSize(type, cx, cy);
    if (tree_.GetSafeHwnd()) {
        tree_.MoveWindow(0, 0, cx, cy);
    }
}

void CTreePane::OnTreeSelChanged(NMHDR* header, LRESULT* result) {
    auto* view = reinterpret_cast<NMTREEVIEW*>(header);
    if (owner_) {
        owner_->OnTreeSelectionChanged(view->itemNew.hItem);
    }
    *result = 0;
}

void CTreePane::OnTreeClick(NMHDR* /*header*/, LRESULT* result) {
    *result = 0;

    CPoint point;
    ::GetCursorPos(&point);
    tree_.ScreenToClient(&point);

    UINT flags = 0;
    HTREEITEM item = tree_.HitTest(point, &flags);
    if (item && (flags & TVHT_ONITEMSTATEICON)) {
        PostMessage(WM_CADGEOM_TREECHECK, reinterpret_cast<WPARAM>(item));
    }
}

LRESULT CTreePane::OnDeferredCheck(WPARAM wparam, LPARAM /*lparam*/) {
    HTREEITEM item = reinterpret_cast<HTREEITEM>(wparam);
    if (owner_ && item) {
        owner_->OnTreeItemChecked(item);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// CLogPane
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CLogPane, CWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
END_MESSAGE_MAP()

BOOL CLogPane::CreateIn(CWnd* parent, UINT id) {
    return CWnd::Create(PaneWndClass(), nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                        CRect(0, 0, 0, 0), parent, id);
}

int CLogPane::OnCreate(LPCREATESTRUCT cs) {
    if (CWnd::OnCreate(cs) == -1) {
        return -1;
    }
    if (!edit_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE |
                          ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
                      CRect(0, 0, 0, 0), this, 1)) {
        return -1;
    }
    // 等宽字体：日志里的数字对得齐才看得出量级。
    font_.CreatePointFont(90, _T("Consolas"));
    edit_.SetFont(&font_);
    return 0;
}

void CLogPane::OnSize(UINT type, int cx, int cy) {
    CWnd::OnSize(type, cx, cy);
    if (edit_.GetSafeHwnd()) {
        edit_.MoveWindow(0, 0, cx, cy);
    }
}

void CLogPane::Append(int level, const CString& text) {
    if (!edit_.GetSafeHwnd()) {
        return;
    }
    static const TCHAR* const kTags[] = {_T("trace"), _T("debug"), _T("info "), _T("WARN "),
                                         _T("ERROR"), _T("FATAL"), _T("     ")};
    const int index = (level >= 0 && level < 7) ? level : 2;

    // 攒太长就把最老的一千行剪掉。CEdit 不像 QPlainTextEdit 有 maximumBlockCount，
    // 得自己动手；不剪的话跑久了控件会越来越慢。
    if (edit_.GetLineCount() > 4000) {
        const int cut = edit_.LineIndex(1000);
        if (cut > 0) {
            edit_.SetSel(0, cut);
            edit_.ReplaceSel(_T(""));
        }
    }

    CString line;
    line.Format(_T("[%s] %s\r\n"), kTags[index], static_cast<LPCTSTR>(text));

    const int end = edit_.GetWindowTextLength();
    edit_.SetSel(end, end);
    edit_.ReplaceSel(line);
}
