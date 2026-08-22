#include "app/AppController.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

#include "core/utils/Conversions.h"
#include "format/crypto/ArchiveCipher.h"

namespace transmit::app {

AppController::AppController(QObject* parent)
    : QObject(parent), settings_(QStringLiteral("Transmit"), QStringLiteral("Transmit")) {
    platform_ = platform::PlatformService::create();
    environment_ = platform_->environment();
    themeMode_ = settings_.value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString();
    reduceMotion_ = settings_.value(QStringLiteral("ui/reduceMotion"), false).toBool();
}

void AppController::setCurrentPage(const QString& page) {
    if (currentPage_ == page) {
        return;
    }
    currentPage_ = page;
    emit currentPageChanged();
}

void AppController::setThemeMode(const QString& mode) {
    if (themeMode_ == mode) {
        return;
    }
    themeMode_ = mode;
    settings_.setValue(QStringLiteral("ui/theme"), mode);
    emit themeModeChanged();
}

void AppController::setReduceMotion(bool reduce) {
    if (reduceMotion_ == reduce) {
        return;
    }
    reduceMotion_ = reduce;
    settings_.setValue(QStringLiteral("ui/reduceMotion"), reduce);
    emit reduceMotionChanged();
}

QString AppController::osName() const {
    return environment_.osName;
}

QString AppController::osFamily() const {
    return core::fromUtf8(format::osFamilyName(environment_.os));
}

QString AppController::hostName() const {
    return environment_.hostName;
}
QString AppController::userName() const {
    return environment_.userName;
}
QString AppController::homeDirectory() const {
    return environment_.homeDirectory;
}
QString AppController::desktopEnvironment() const {
    return environment_.desktopEnvironment;
}

QString AppController::packageManager() const {
    return platform::packageSourceName(platform_->nativePackageSource());
}

QString AppController::appVersion() {
    return QStringLiteral(TRANSMIT_VERSION);
}

bool AppController::encryptionAvailable() {
    return format::ArchiveCipher::isAvailable();
}

bool AppController::isMac() const {
    return environment_.os == format::OsFamily::MacOs;
}

QString AppController::toFileUrl(const QString& path) {
    return path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString();
}

QString AppController::fromFileUrl(const QString& url) {
    if (url.isEmpty()) {
        return {};
    }
    const QUrl parsed(url);
    return parsed.isLocalFile() ? parsed.toLocalFile() : url;
}

void AppController::revealInFileManager(const QString& path) {
    const QFileInfo info(path);
    const QString target = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

QString AppController::formatBytes(quint64 bytes) {
    return core::formatBytes(bytes);
}

}  // namespace transmit::app
