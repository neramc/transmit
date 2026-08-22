#pragma once

#include <QByteArray>
#include <QString>

#include "format/Bytes.h"
#include "format/PathToken.h"
#include "format/Result.h"

namespace transmit::core {

/// Qt strings are UTF-16 and the format layer speaks UTF-8; these are the only
/// two places that conversion should happen.
inline std::string toUtf8(const QString& text) {
    return text.toStdString();
}
inline QString fromUtf8(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

inline format::ByteView toByteView(const QByteArray& data) {
    return {reinterpret_cast<const format::Byte*>(data.constData()),
            static_cast<std::size_t>(data.size())};
}

inline QByteArray toByteArray(format::ByteView data) {
    return QByteArray(reinterpret_cast<const char*>(data.data()),
                      static_cast<qsizetype>(data.size()));
}

/// Renders a format-layer failure for the user interface.
inline QString describeError(const format::Error& error) {
    return fromUtf8(error.toString());
}

/// Human-readable byte counts. Uses binary units because that is what disk and
/// USB capacity dialogs on every target platform show.
QString formatBytes(quint64 bytes);

/// "3 minutes 20 seconds" style text for progress estimates.
QString formatDuration(qint64 milliseconds);

/// A percentage clamped to [0, 100], safe when the total is still unknown.
double percentage(quint64 done, quint64 total);

}  // namespace transmit::core
