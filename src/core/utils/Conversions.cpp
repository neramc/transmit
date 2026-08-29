#include "core/utils/Conversions.h"

#include <QCoreApplication>
#include <QLocale>

#include <algorithm>

namespace transmit::core {

QString formatBytes(quint64 bytes) {
    static constexpr const char* kUnits[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    auto value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    const int decimals = (unit == 0) ? 0 : (value < 10.0 ? 2 : 1);
    return QStringLiteral("%1 %2")
        .arg(QLocale().toString(value, 'f', decimals))
        .arg(QLatin1String(kUnits[unit]));
}

QString formatDuration(qint64 milliseconds) {
    if (milliseconds < 0) {
        return QCoreApplication::translate("Duration", "unknown");
    }
    const qint64 totalSeconds = milliseconds / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;

    if (hours > 0) {
        return QCoreApplication::translate("Duration", "%1 h %2 min").arg(hours).arg(minutes);
    }
    if (minutes > 0) {
        return QCoreApplication::translate("Duration", "%1 min %2 s").arg(minutes).arg(seconds);
    }
    return QCoreApplication::translate("Duration", "%1 s").arg(seconds);
}

QString fileExtension(const QString& path) {
    const qsizetype lastSeparator = std::max(path.lastIndexOf(u'/'), path.lastIndexOf(u'\\'));
    const qsizetype lastDot = path.lastIndexOf(u'.');

    // No dot in the last component, or nothing after it. A dot at the very
    // start counts: QFileInfo(".bashrc").suffix() is "bashrc", and this has to
    // give the same answer as the QFileInfo call it replaced or it would
    // quietly reorder everybody's archives.
    if (lastDot <= lastSeparator || lastDot == path.size() - 1) {
        return {};
    }
    return path.mid(lastDot + 1).toLower();
}

double percentage(quint64 done, quint64 total) {
    if (total == 0) {
        return 0.0;
    }
    const double value = (static_cast<double>(done) * 100.0) / static_cast<double>(total);
    return value < 0.0 ? 0.0 : (value > 100.0 ? 100.0 : value);
}

}  // namespace transmit::core
