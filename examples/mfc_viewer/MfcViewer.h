/**
 * @file MfcViewer.h
 * @brief MFC 宿主的应用对象 —— 引擎的所有权在这里。
 *
 * 这个示例和 glfw_viewer 想说明的是两件不同的事：那边是「引擎自带窗口，宿主什么
 * 都不用管」，这边是「宿主已经有一整套 UI 了，引擎只做那块 3D 区域」——
 * docs/architecture.md §4.4 里 NativeSurface 那一半。qt_viewer 说的是同一件事，
 * 换成 MFC 再说一遍：两边的差别都记在 README.md 里。
 *
 * 它链接的东西只有两样：cadgeom 和 MFC。引擎的 Vulkan、GLFW、tinygltf 全在共享库
 * 里面，一个都不会漏到宿主的编译单元里来。
 */
#ifndef CADGEOM_MFC_VIEWER_MFCVIEWER_H
#define CADGEOM_MFC_VIEWER_MFCVIEWER_H

#include <afxwin.h>

#include <cadgeom/CadGeomRAII.h>

/// @brief 应用对象。引擎在 InitInstance 里建、在 ExitInstance 里放 —— 窗口比它先
///        死，所以窗口析构时视口还有得可放。
class CMfcViewerApp : public CWinApp {
public:
    BOOL InitInstance() override;
    int ExitInstance() override;

    /// @return 引擎；还没建起来时为 null。
    cadgeom::ICadEngine* Engine() { return engine_.get(); }

private:
    /// @brief 自检开关：--screenshot PATH [--frames N] [--sample]。
    void ParseArguments(CString& shotPath, int& frames, bool& sample) const;

    cadgeom::EnginePtr engine_;
};

extern CMfcViewerApp theApp;

#endif // CADGEOM_MFC_VIEWER_MFCVIEWER_H
