#include <QElapsedTimer>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>

#include "core/utils/Logging.h"

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
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/transmit-256.png")));

    // The Basic style is the one that honours a custom design system; the
    // native styles override colours and defeat the point of having one.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    transmit::core::configureLogging(qEnvironmentVariableIsSet("TRANSMIT_VERBOSE"));

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
