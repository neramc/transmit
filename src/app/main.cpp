#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QUrl>

#include "core/utils/Logging.h"

int main(int argc, char** argv) {
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
    return QGuiApplication::exec();
}
