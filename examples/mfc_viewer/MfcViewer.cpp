/**
 * @file MfcViewer.cpp
 * @brief CMfcViewerApp 的实现 —— 启动、命令行、退出时的泄漏检查。
 */
#include "MfcViewer.h"

#include "LogSink.h"
#include "MainFrame.h"
#include "Utf8.h"
#include "resource.h"

#include <afxcmn.h>

#include <tchar.h>

CMfcViewerApp theApp;

namespace {

/// @brief 让进程成为 per-monitor v2 的 DPI 感知进程。
///
/// 不声明的话，Windows 会在高 DPI 屏上把整个窗口按比例拉伸：交换链按「逻辑像素」
/// 建出来，再被系统放大到物理像素，画面糊一层；更糟的是鼠标坐标和 GetClientRect
/// 从此说的是两套数，拾取会整体偏掉。声明之后客户区坐标就是物理像素，和引擎那边
/// 的 GetClientRect 对得上，视口窗口里一次折算都不用做（对照 qt_viewer 那边处处
/// 要乘的 devicePixelRatio）。
///
/// 用运行时查找而不是直接调：这个 API 是 Windows 10 1703 才有的，老系统上没有它
/// 也该照常启动，只是画面会糊一点。
void EnablePerMonitorDpi() {
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (!user32) {
        return;
    }
    // 签名本该是 BOOL(DPI_AWARENESS_CONTEXT)，但那个类型要新一点的 SDK 才有；
    // 它实际上就是一个句柄，-4 是 PER_MONITOR_AWARE_V2 的取值。
    using SetContextFn = BOOL(WINAPI*)(HANDLE);
    auto setContext =
        reinterpret_cast<SetContextFn>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setContext) {
        setContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
    }
}

} // namespace

// ---------------------------------------------------------------------------

BOOL CMfcViewerApp::InitInstance() {
    EnablePerMonitorDpi();

    // 树控件和进度条来自公共控件库，用之前先初始化。
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_WIN95_CLASSES | ICC_TREEVIEW_CLASSES | ICC_PROGRESS_CLASS |
                     ICC_BAR_CLASSES | ICC_COOL_CLASSES;
    ::InitCommonControlsEx(&controls);

    CWinApp::InitInstance();

    cadgeom::EngineDesc desc{};
    desc.applicationName = "CadGeom MFC Viewer";
    desc.kernelType = cadgeom::KernelType::Simple;
#if defined(_DEBUG) || !defined(NDEBUG)
    desc.enableValidation = true;
#endif
    desc.logLevel = cadgeom::LogLevel::Info;
    // 日志槽先于窗口存在，所以引擎创建阶段那几条消息也攒得下（LogSink.h）。
    desc.logCallback = &LogSink::Callback;

    // CreateEngine 先比对 API 版本再分配：主版本对不上就直接返回空，而不是让宿主
    // 拿着一张错位的 vtable 往下走（§2.2 第 7 条）。
    engine_ = cadgeom::CreateEngine(desc);
    if (!engine_) {
        CString text;
        text.Format(_T("引擎创建失败：\n\n%s"),
                    static_cast<LPCTSTR>(FromUtf8(CadGeom_GetCreateEngineError())));
        AfxMessageBox(text, MB_OK | MB_ICONERROR);
        return FALSE;
    }

    // 窗口自己 delete 自己（CFrameWnd::PostNcDestroy），所以这里不用记着删它。
    auto* frame = new CMainFrame(*engine_);
    m_pMainWnd = frame;
    if (!frame->LoadFrame(IDR_MAINFRAME, WS_OVERLAPPEDWINDOW | FWS_ADDTOTITLE, nullptr, nullptr)) {
        return FALSE;
    }

    CString shotPath;
    int frames = 0;
    bool sample = false;
    ParseArguments(shotPath, frames, sample);
    if (frames > 0) {
        frame->SetAutoShot(shotPath, frames);
    }
    if (sample) {
        // 视口是第一次拿到尺寸时建的，这会儿场景里有东西，它就绪时那一次
        // ZoomToFit 正好框住。
        frame->LoadSampleScene();
    }

    // 默认大小按显示器 DPI 折算 —— 进程是 DPI 感知的，SetWindowPos 收的是物理
    // 像素。再夹到工作区的九成以内：150% 缩放的笔记本上，折算完的 1480×920 比屏幕
    // 还大。
    {
        CClientDC dc(frame);
        const int dpi = dc.GetDeviceCaps(LOGPIXELSX);
        RECT work{};
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
        int width = MulDiv(1480, dpi, 96);
        int height = MulDiv(920, dpi, 96);
        const int maxWidth = (work.right - work.left) * 9 / 10;
        const int maxHeight = (work.bottom - work.top) * 9 / 10;
        width = width > maxWidth ? maxWidth : width;
        height = height > maxHeight ? maxHeight : height;
        frame->SetWindowPos(nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
    }
    frame->CenterWindow();
    frame->ShowWindow(SW_SHOW);
    frame->UpdateWindow();
    return TRUE;
}

void CMfcViewerApp::ParseArguments(CString& shotPath, int& frames, bool& sample) const {
    // __targv / __argc 是 CRT 给的、已经拆好的命令行；MFC 的 CCommandLineInfo 是
    // 冲着「文档 / 视图」那套去的，这个示例没有文档可开。
    for (int i = 1; i < __argc; ++i) {
        const CString argument = __targv[i];
        if (argument == _T("--screenshot") && i + 1 < __argc) {
            shotPath = __targv[++i];
            if (frames <= 0) {
                frames = 30;  // 给交换链和第一次 ZoomToFit 留出几帧
            }
        } else if (argument == _T("--frames") && i + 1 < __argc) {
            frames = _ttoi(__targv[++i]);
        } else if (argument == _T("--sample")) {
            sample = true;
        }
    }
}

int CMfcViewerApp::ExitInstance() {
    // 主窗口已经没了（连带它的视口），现在放引擎。
    engine_.reset();

    int code = CWinApp::ExitInstance();

    // 引擎和窗口都已经走完，这个数必须回到 0 —— 项目里从第一个里程碑起就守着这条
    // 线，宿主也一样守：没回到 0 就是有人漏了一次 Release()。退出码带上它，脚本才
    // 抓得住（GUI 程序没有控制台，TRACE 只到调试输出）。
    const uint64_t live = CadGeom_GetLiveObjectCount();
    if (live != 0) {
        CString text;
        text.Format(_T("CadGeom: %llu 个接口对象没有被释放\n"),
                    static_cast<unsigned long long>(live));
        ::OutputDebugString(text);
        if (code == 0) {
            code = 2;
        }
    }
    return code;
}
