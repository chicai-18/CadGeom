# 把引擎嵌进一个真实宿主 UI 的示例：菜单、工具栏、模型树、日志、状态栏。
#
# 和 glfw_viewer 一样，它链接的引擎侧只有 cadgeom 一个目标 —— Vulkan、GLFW、
# tinygltf 都在共享库里面，不会漏进宿主的编译单元。多出来的只有 Qt Widgets。
#
# Qt 找不到就整块跳过，不让一个可选示例挡住主库和测试的构建：
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=D:/Qt/5.15.2/msvc2019_64

find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets QUIET)
if(NOT QT_FOUND)
    message(STATUS "  Qt example      : skipped (Qt5/Qt6 Widgets not found; set CMAKE_PREFIX_PATH)")
    return()
endif()

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
message(STATUS "  Qt example      : Qt${QT_VERSION_MAJOR} ${Qt${QT_VERSION_MAJOR}Widgets_VERSION}")

# WIN32 = 不带控制台子系统。引擎的日志有自己的面板，不需要一个黑框跟着。
add_executable(qt_viewer WIN32
    main.cpp
    CadGeomWidget.cpp
    CadGeomWidget.h
    Dialogs.cpp
    Dialogs.h
    HostCommands.cpp
    HostCommands.h
    LogBridge.cpp
    LogBridge.h
    MainWindow.cpp
    MainWindow.h
    SampleScene.cpp
    SampleScene.h)

# Q_OBJECT 要 moc；只给这个目标开，别污染整棵树。
set_target_properties(qt_viewer PROPERTIES AUTOMOC ON FOLDER "examples")

target_link_libraries(qt_viewer PRIVATE
    CadGeom::cadgeom
    Qt${QT_VERSION_MAJOR}::Widgets)

# 公共头文件只要求 C++17，这个宿主也就停在 17。
target_compile_features(qt_viewer PRIVATE cxx_std_17)

if(MSVC)
    # /utf-8 不是可选项：源码里有中文注释和中文界面字符串。
    target_compile_options(qt_viewer PRIVATE /W4 /utf-8)
    if(CADGEOM_WARNINGS_AS_ERRORS)
        target_compile_options(qt_viewer PRIVATE /WX)
    endif()
else()
    target_compile_options(qt_viewer PRIVATE -Wall -Wextra)
    if(CADGEOM_WARNINGS_AS_ERRORS)
        target_compile_options(qt_viewer PRIVATE -Werror)
    endif()
endif()

# Windows 上把 Qt 的运行时拷到输出目录，构建完就能双击运行 —— 否则要么改 PATH，
# 要么对着一个「缺少 Qt5Widgets.dll」的弹窗发呆。
option(CADGEOM_QT_DEPLOY "Run windeployqt after building qt_viewer" ON)
if(WIN32)
    # 找一次，构建后和安装时都用它：前者让构建树能直接双击运行，后者让发布包
    # 解开就能运行。CADGEOM_QT_DEPLOY 只关掉前者 —— 发布包里少了 Qt 运行时就
    # 不叫发布包了。
    get_target_property(_cadgeom_qmake Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    if(_cadgeom_qmake)
        get_filename_component(_cadgeom_qt_bin "${_cadgeom_qmake}" DIRECTORY)
        find_program(CADGEOM_WINDEPLOYQT windeployqt HINTS "${_cadgeom_qt_bin}")
    endif()
endif()

if(WIN32 AND CADGEOM_QT_DEPLOY AND CADGEOM_WINDEPLOYQT)
    add_custom_command(TARGET qt_viewer POST_BUILD
        COMMAND "${CADGEOM_WINDEPLOYQT}"
                --no-translations
                --no-compiler-runtime
                --no-system-d3d-compiler
                "$<TARGET_FILE:qt_viewer>"
        COMMENT "windeployqt qt_viewer"
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# 安装 —— 进 CPack 的那份拷贝
# ---------------------------------------------------------------------------

if(CADGEOM_INSTALL_EXAMPLES)
    install(TARGETS qt_viewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    if(WIN32 AND CADGEOM_WINDEPLOYQT)
        # windeployqt 认的是可执行文件本身，所以只能在 exe 装进去之后再跑；
        # install(CODE) 的执行顺序跟声明顺序一致，上面那条 install(TARGETS)
        # 已经把它放好了。--dir 指向同一个 bin/，插件目录（platforms/ 等）
        # 也一并落在那里，Qt 才找得到它们。
        install(CODE "
set(_qt_dir \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
message(STATUS \"windeployqt -> \${_qt_dir}\")
execute_process(
    COMMAND \"${CADGEOM_WINDEPLOYQT}\"
            --no-translations --no-compiler-runtime --no-system-d3d-compiler
            --dir \"\${_qt_dir}\" \"\${_qt_dir}/qt_viewer.exe\"
    RESULT_VARIABLE _qt_rc)
if(NOT _qt_rc EQUAL 0)
    message(FATAL_ERROR \"windeployqt failed with \${_qt_rc}\")
endif()
")
    endif()
endif()
