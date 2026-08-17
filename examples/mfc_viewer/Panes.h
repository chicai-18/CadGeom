/**
 * @file Panes.h
 * @brief 主窗口客户区里的三块地方：视口区、模型树、引擎日志。
 *
 * 用 CSplitterWnd 而不是 MFC 功能包的 CDockablePane：静态分割条不需要
 * CFrameWndEx、不需要视觉管理器，也就不需要一套图标资源 —— 这份示例想展示的是
 * 「引擎怎么嵌进宿主」，不是「MFC 的停靠框架怎么用」。代价是面板只能收起来，不能
 * 拖出去浮动，qt_viewer 那边的 QDockWidget 能。
 */
#ifndef CADGEOM_MFC_VIEWER_PANES_H
#define CADGEOM_MFC_VIEWER_PANES_H

#include <afxcmn.h>
#include <afxwin.h>

#include <cadgeom/CadGeom.h>

#include <vector>

class CMainFrame;
class CViewportWnd;

/// @brief 视口区：把 N 个 CViewportWnd 横着平铺。
///
/// 多个视口共享设备与几何，相机和显示状态各自独立（docs/architecture.md §4.1）。
class CViewArea : public CWnd {
public:
    CViewArea() = default;
    ~CViewArea() override;

    BOOL CreateIn(CWnd* parent, UINT id);
    /// @brief 引擎和主窗口要在建视口之前交给它。
    void Attach(cadgeom::ICadEngine* engine, CMainFrame* owner);

    /// @param secondary 第二个及以后的视口 —— 主窗口会给它一套不同的初始视角。
    /// @return 新窗口；建不出来时为 null。
    CViewportWnd* AddViewport(bool secondary);
    /// @brief 关掉除第一个之外的所有视口。
    void CloseExtraViewports();

    int Count() const { return static_cast<int>(views_.size()); }
    CViewportWnd* At(int index) const;
    /// @brief 挨个画一帧。
    void RenderAll();

protected:
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg BOOL OnEraseBkgnd(CDC* dc);
    DECLARE_MESSAGE_MAP()

private:
    /// @brief 按当前窗口数重排。
    void Layout();

    cadgeom::ICadEngine* engine_{nullptr};
    CMainFrame* owner_{nullptr};
    std::vector<CViewportWnd*> views_;
    UINT nextId_{2000};
};

/// @brief 模型树。控件是这个面板建的，树的内容由 CMainFrame 填（那是引擎的事）。
class CTreePane : public CWnd {
public:
    BOOL CreateIn(CWnd* parent, UINT id);
    void SetOwner(CMainFrame* owner) { owner_ = owner; }

    CTreeCtrl& Tree() { return tree_; }

protected:
    afx_msg int OnCreate(LPCREATESTRUCT cs);
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnTreeSelChanged(NMHDR* header, LRESULT* result);
    afx_msg void OnTreeClick(NMHDR* header, LRESULT* result);
    afx_msg LRESULT OnDeferredCheck(WPARAM wparam, LPARAM lparam);
    DECLARE_MESSAGE_MAP()

private:
    CTreeCtrl tree_;
    CMainFrame* owner_{nullptr};
};

/// @brief 引擎日志。只读、等宽、自动滚到底。
class CLogPane : public CWnd {
public:
    BOOL CreateIn(CWnd* parent, UINT id);

    /// @param level cadgeom::LogLevel 的整数值。
    void Append(int level, const CString& text);

protected:
    afx_msg int OnCreate(LPCREATESTRUCT cs);
    afx_msg void OnSize(UINT type, int cx, int cy);
    DECLARE_MESSAGE_MAP()

private:
    CEdit edit_;
    CFont font_;
};

#endif // CADGEOM_MFC_VIEWER_PANES_H
