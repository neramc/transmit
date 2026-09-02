#include <QElapsedTimer>
#include <QGuiApplication>
#include <QIcon>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QUrl>

#include "core/utils/Logging.h"

namespace {

#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD)

/// Whether the scene graph would be able to build an OpenGL context.
///
/// Qt Quick does not cope with the answer being no: it prints
/// "Failed to initialize graphics backend for OpenGL" and calls qFatal, so the
/// process dumps core with no window and nothing the person in front of it can
/// act on. That is not a theoretical machine. A Wayland session whose Qt
/// installation is missing the client buffer integration reaches it, and so
/// does a remote desktop, a virtual machine with no GL, and a driver that
/// fails to load.
///
/// Asking first costs one context that is thrown away immediately, and turns
/// the crash into a slower window.
bool openGlIsUsable() {
    QOffscreenSurface surface;
    surface.setFormat(QSurfaceFormat::defaultFormat());
    surface.create();
    if (!surface.isValid()) {
        return false;
    }

    QOpenGLContext context;
    context.setFormat(surface.format());
    if (!context.create()) {
        return false;
    }
    if (!context.makeCurrent(&surface)) {
        return false;
    }
    context.doneCurrent();
    return true;
}

/// Picks the scene graph backend before the first window exists, which is the
/// only moment Qt allows the choice to be made.
///
/// Anyone who has already chosen a backend keeps it - an explicit
/// QSG_RHI_BACKEND or QT_QUICK_BACKEND is an instruction, not a suggestion.
void chooseGraphicsBackend() {
    for (const char* const chosen : {"QSG_RHI_BACKEND", "QT_QUICK_BACKEND", "QMLSCENE_DEVICE"}) {
        if (qEnvironmentVariableIsSet(chosen)) {
            return;
        }
    }

    if (openGlIsUsable()) {
        return;
    }

    qCWarning(logApp) << "no usable OpenGL context; drawing in software instead";
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
}

#else

/// Only the platforms whose default backend is OpenGL are probed. Windows
/// draws through Direct3D and macOS through Metal, so an OpenGL context would
/// answer a question neither of them asks - and a helper left compiled but
/// unreferenced is an error under -Werror, so there is nothing here to leave
/// behind.
void chooseGraphicsBackend() {}

#endif

}  // namespace

int main(int argc, char** argv) {
    // Started before anything else so the number below is the whole cost of
    // launching, not the part after the expensive bit.
    QElapsedTimer startup;
    startup.start();

    QGuiApplication app(argc, argv);

    QGuiApplication::setApplicationName(QStringLiteral("Transmit"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Transmit"));
    QGuiApplication::setApplicationVersion(QStringLiteral(TRANSMIT_VERSION));
    QGuiApplication::setOrganizationName(QStringLiteral("Transmit"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("transmit.local"));
    // Every size, rather than one large image for the window manager to
    // squeeze into a title bar: at 16 pixels a scaled-down 256 is mush, and
    // the drawn-for-16 version is not. The sizes come from the build, which is
    // where the resource was filled from - a QIcon handed a path that does not
    // resolve stays empty and says nothing about it.
    QIcon icon;
    for (const int size : {TRANSMIT_ICON_SIZES}) {
        icon.addFile(QStringLiteral(":/icons/transmit-%1.png").arg(size));
    }
    QGuiApplication::setWindowIcon(icon);

    // The Basic style is the one that honours a custom design system; the
    // native styles override colours and defeat the point of having one.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    transmit::core::configureLogging(qEnvironmentVariableIsSet("TRANSMIT_VERBOSE"));

    chooseGraphicsBackend();

    QQmlApplicationEngine engine;

    // Qt 6.5 and later search ":/qt/qml" for modules by default; 6.4 does not,
    // and this project supports 6.4. Adding it explicitly is harmless on newer
    // versions and is what makes the statically linked QML modules resolvable.
    engine.addImportPath(QStringLiteral(":/qt/qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() {
            qCCritical(logApp) << "the main window could not be created";
            QCoreApplication::exit(1);
        },
        Qt::QueuedConnection);

    // loadFromModule needs Qt 6.5; the resource URL form works from 6.4, which
    // is the minimum this project supports.
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Transmit/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    // Measurement before optimisation, so the two numbers that matter are
    // reported rather than guessed at. Enable them with
    // QT_LOGGING_RULES="transmit.performance.debug=true"; setting
    // TRANSMIT_STARTUP_BENCHMARK as well quits once the window has painted,
    // which is what makes it cheap enough to run a launch fifty times over.
    const bool benchmarking = qEnvironmentVariableIsSet("TRANSMIT_STARTUP_BENCHMARK");
    qCDebug(logPerformance) << "window built after" << startup.elapsed() << "ms";

    if (auto* const window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst())) {
        // The second half of the graphics fallback, and the reason the window
        // is reached at all rather than the process being aborted from under
        // it. Qt Quick only calls qFatal when nothing is listening here, so a
        // connection - even one that does nothing but say what happened and
        // leave - is what turns a core dump into an error message.
        //
        // Connected before exec(), because the scene graph is initialised from
        // the expose event and that arrives once the loop is running.
        QObject::connect(window, &QQuickWindow::sceneGraphError, &app,
                         [](QQuickWindow::SceneGraphError, const QString& message) {
                             qCCritical(logApp)
                                 << "the graphics backend could not be started:" << message;
                             QCoreApplication::exit(1);
                         });

        QObject::connect(
            window, &QQuickWindow::frameSwapped, window,
            [&startup, benchmarking]() {
                qCDebug(logPerformance) << "first frame after" << startup.elapsed() << "ms";
                if (benchmarking) {
                    QCoreApplication::quit();
                }
            },
            Qt::SingleShotConnection);
    }

    return QGuiApplication::exec();
}
