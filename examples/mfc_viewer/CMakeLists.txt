# 把引擎嵌进一个 MFC 宿主：菜单、工具栏、模型树、日志、状态栏。
#
# 和 qt_viewer 演示的是同一件事（SurfaceKind::NativeWin32，宿主拥有窗口和消息
# 循环），换一套 UI 框架说一遍 —— 两边的差别正是这份示例想说明的东西，都写在
# README.md 里。链接的引擎侧同样只有 cadgeom 一个目标。
#
# 三个前提，缺一个就整块跳过，不让一个可选示例挡住主库和测试的构建：
#   * MSVC —— MFC 是微软的库，别的编译器没有；
#   * Visual Studio 生成器 —— CMAKE_MFC_FLAG 只有它认（Ninja 下 CMake 不会把
#     atlmfc 的 include/lib 路径喂给编译器）；
#   * 装了 VS 的「MFC 组件」—— 默认的 C++ 桌面工作负载并不带它。

if(NOT MSVC)
    message(STATUS "  MFC example     : skipped (MFC needs MSVC)")
    return()
endif()

if(NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    message(STATUS "  MFC example     : skipped (needs the Visual Studio generator)")
    return()
endif()

# cl.exe 在 <toolset>/bin/Host<arch>/<arch>/ 下，往上数四层就是工具集根目录，
# MFC 头文件跟着它走。用 find_path 或者 try_compile 都不行：atlmfc 的 include
# 路径是 MSBuild 在 UseOfMfc 打开之后才补上的，探测的时候还看不见。
get_filename_component(_cadgeom_msvc "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(_cadgeom_msvc "${_cadgeom_msvc}" DIRECTORY)
get_filename_component(_cadgeom_msvc "${_cadgeom_msvc}" DIRECTORY)
get_filename_component(_cadgeom_msvc "${_cadgeom_msvc}" DIRECTORY)
if(NOT EXISTS "${_cadgeom_msvc}/atlmfc/include/afxwin.h")
    message(STATUS "  MFC example     : skipped (Visual Studio 的 MFC 组件没装)")
    return()
endif()

message(STATUS "  MFC example     : ${_cadgeom_msvc}/atlmfc")

# 2 = 动态链接 MFC（mfc140u.dll）。VS 生成器把它写成工程里的 <UseOfMfc>Dynamic，
# MSBuild 据此补上 atlmfc 的 include/lib 路径和 _AFXDLL 宏。必须在 add_executable
# 之前设。
set(CMAKE_MFC_FLAG 2)

# WIN32 = 不带控制台子系统；入口是 MFC 自己的 AfxWinMain。
add_executable(mfc_viewer WIN32
    MfcViewer.cpp
    MfcViewer.h
    MainFrame.cpp
    MainFrame.h
    ViewportWnd.cpp
    ViewportWnd.h
    Panes.cpp
    Panes.h
    Dialogs.cpp
    Dialogs.h
    HostCommands.cpp
    HostCommands.h
    LogSink.cpp
    LogSink.h
    SampleScene.cpp
    SampleScene.h
    Utf8.h
    MfcViewer.rc
    resource.h)

set_target_properties(mfc_viewer PROPERTIES FOLDER "examples")

target_link_libraries(mfc_viewer PRIVATE CadGeom::cadgeom)

# 公共头文件只要求 C++17，这个宿主也就停在 17。
target_compile_features(mfc_viewer PRIVATE cxx_std_17)

# Unicode 构建：CString 就是 CStringW，控件收发的都是 UTF-16。引擎那边全是
# UTF-8，边界上的换算集中在 Utf8.h 一处。CMake 看见这两个宏就会把工程的
# CharacterSet 写成 Unicode，MFC 的 #pragma comment 于是挑中 mfc140u.lib。
target_compile_definitions(mfc_viewer PRIVATE UNICODE _UNICODE)

# /utf-8 不是可选项：源码和资源脚本里都有中文。少了它，编译器按 936 解释 UTF-8
# 字节，一个汉字就能把后面的换行吃掉（rc 那边另有 #pragma code_page）。
# MFC 的头文件在 /W4 下会刷一批与本示例无关的警告，所以这里停在 /W3。
target_compile_options(mfc_viewer PRIVATE /W3 /utf-8)
if(CADGEOM_WARNINGS_AS_ERRORS)
    target_compile_options(mfc_viewer PRIVATE /WX)
endif()

# ---------------------------------------------------------------------------
# 安装 —— 进 CPack 的那份拷贝
#
# 这里只装可执行文件。MFC 的运行时（mfc140u.dll）走的是 VC++ 可再发行组件包，
# 不像 Qt 那样能 windeployqt 一份到旁边；装了 Visual Studio 或者 VC++ Redist
# 的机器上它已经在系统目录里了。
# ---------------------------------------------------------------------------

if(CADGEOM_INSTALL_EXAMPLES)
    install(TARGETS mfc_viewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
