/**
 * @file main.cpp
 * @brief Qt 宿主的入口。
 *
 * 这个示例和 glfw_viewer 想说明的是两件不同的事：那边是「引擎自带窗口，宿主什么
 * 都不用管」，这边是「宿主已经有一整套 UI 了，引擎只做那块 3D 区域」——
 * docs/architecture.md §4.4 里 NativeSurface 那一半。
 *
 * 它链接的东西也就两样：cadgeom 和 Qt Widgets。引擎的 Vulkan、GLFW、tinygltf 全
 * 在共享库里面，一个都不会漏到宿主的编译单元里来。
 */
#include "LogBridge.h"
#include "MainWindow.h"

#include <cadgeom/CadGeomRAII.h>

#include <QApplication>
#include <QMessageBox>

int main(int argc, char** argv) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt6 起这是默认行为。高 DPI 下逻辑像素和物理像素会差一个比例，控件那边已经
    // 按 devicePixelRatio 折算过再交给引擎了。
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("CadGeom Qt Viewer"));
    app.setOrganizationName(QStringLiteral("CadGeom"));

    // 先在 GUI 线程里把日志桥建出来：它的线程亲缘决定了跨线程日志排到哪儿去。
    (void)LogBridge::instance();

    int exitCode = 0;
    {
        cadgeom::EngineDesc desc{};
        desc.applicationName = "CadGeom Qt Viewer";
        desc.kernelType = cadgeom::KernelType::Simple;
#if defined(_DEBUG) || !defined(NDEBUG)
        desc.enableValidation = true;
#endif
        desc.logLevel = cadgeom::LogLevel::Info;
        desc.logCallback = &LogBridge::Callback;

        // CreateEngine 先比对 API 版本再分配：主版本对不上就直接返回空，而不是让
        // 宿主拿着一张错位的 vtable 往下走（§2.2 第 7 条）。
        cadgeom::EnginePtr engine = cadgeom::CreateEngine(desc);
        if (!engine) {
            QMessageBox::critical(
                nullptr, QStringLiteral("CadGeom"),
                QStringLiteral("引擎创建失败：\n\n%1")
                    .arg(QString::fromUtf8(CadGeom_GetCreateEngineError())));
            return 1;
        }

        // 声明顺序即析构顺序的反面：window 先走（连带释放它的视口），engine 后走。
        MainWindow window(*engine);

        // 自检开关：--screenshot PATH [--frames N]，画满就存图退出。没有它，
        // 「嵌入宿主窗口的那条渲染路径还通不通」就只能靠人盯着看。--sample 画那张
        // 示例图纸 —— 自检要它，不然截出来的是一张只有网格的空图。
        QString shotPath;
        int frames = 0;
        bool sample = false;
        const QStringList args = app.arguments();
        for (int i = 1; i < args.size(); ++i) {
            if (args[i] == QStringLiteral("--screenshot") && i + 1 < args.size()) {
                shotPath = args[++i];
                if (frames <= 0) {
                    frames = 30;  // 给交换链和第一次 ZoomToFit 留出几帧
                }
            } else if (args[i] == QStringLiteral("--frames") && i + 1 < args.size()) {
                frames = args[++i].toInt();
            } else if (args[i] == QStringLiteral("--sample")) {
                sample = true;
            }
        }
        if (frames > 0) {
            window.setAutoShot(shotPath, frames);
        }
        if (sample) {
            // 视口还要等第一次 showEvent 才建得起来，这会儿场景里有东西，它就绪时
            // 那一次 ZoomToFit 正好框住。
            window.loadSampleScene();
        }

        window.show();
        exitCode = app.exec();
    }

    // 引擎和窗口都已经走完，这个数必须回到 0 —— 项目里从第一个里程碑起就守着这条
    // 线，宿主也一样守：没回到 0 就是有人漏了一次 Release()。退出码带上它，脚本才
    // 抓得住（GUI 程序没有控制台，qWarning 只到调试输出）。
    if (const uint64_t live = CadGeom_GetLiveObjectCount(); live != 0) {
        qWarning("CadGeom: %llu 个接口对象没有被释放", static_cast<unsigned long long>(live));
        if (exitCode == 0) {
            exitCode = 2;
        }
    }
    return exitCode;
}
