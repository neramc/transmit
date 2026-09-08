#include "core/utils/WrittenValues.h"

#include <QHash>
#include <QStringView>

#include <limits>

namespace transmit::core {
namespace {

/// The suffixes, largest first so the search is a plain loop rather than a
/// chain of branches that has to be kept in the right order by hand.
struct Suffix {
    QChar letter;
    quint64 multiplier;
};

constexpr quint64 kKibi = 1024ULL;

const Suffix kSuffixes[] = {
    {QLatin1Char('T'), kKibi* kKibi* kKibi* kKibi},
    {QLatin1Char('G'), kKibi* kKibi* kKibi},
    {QLatin1Char('M'), kKibi* kKibi},
    {QLatin1Char('K'), kKibi},
};

}  // namespace

std::optional<quint64> sizeFromText(const QString& text) {
    const QString upper = text.trimmed().toUpper();
    if (upper.isEmpty()) {
        return std::nullopt;
    }

    quint64 multiplier = 1;
    QString digits = upper;
    for (const Suffix& suffix : kSuffixes) {
        if (upper.endsWith(suffix.letter)) {
            multiplier = suffix.multiplier;
            digits.chop(1);
            break;
        }
    }

    bool valid = false;
    const quint64 value = digits.trimmed().toULongLong(&valid);
    if (!valid) {
        return std::nullopt;
    }

    // Checked before it is done rather than looked at afterwards: the wrap has
    // already happened by then and there is nothing left to notice it by.
    // "18446744073709551G" used to come back as a limit of about fourteen
    // exabytes, which is not what anybody asked for and does not look wrong.
    if (multiplier > 1 && value > std::numeric_limits<quint64>::max() / multiplier) {
        return std::nullopt;
    }
    return value * multiplier;
}

std::optional<QDateTime> timeFromText(const QString& text, const QDateTime& now) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }

    const QChar unit = trimmed.back().toLower();
    if (unit == u'd' || unit == u'w' || unit == u'm' || unit == u'y') {
        bool valid = false;
        // Sixty-four bits, and multiplied as sixty-four bits. The count used
        // to be an int and the week case multiplied it by seven, so
        // "400000000w" was signed overflow - undefined behaviour reached by
        // typing a number.
        const qint64 count = QStringView(trimmed).chopped(1).toLongLong(&valid);
        if (!valid || count < 0) {
            return std::nullopt;
        }

        // How far back this is, at most, in days. Checked before the date
        // arithmetic rather than after it, because past roughly here the
        // arithmetic stops answering rather than failing: two billion years
        // back came out as the first of January 1970, which is a date, is
        // before now, and is nothing to do with what was asked for.
        constexpr qint64 kFurthestBackInDays = 4'000'000;  // about ten thousand years
        static const QHash<char16_t, qint64> daysPer{{u'd', 1}, {u'w', 7}, {u'm', 31}, {u'y', 366}};
        const qint64 perUnit = daysPer.value(unit.unicode());
        if (count > kFurthestBackInDays / perUnit) {
            return std::nullopt;
        }

        QDateTime when;
        switch (unit.unicode()) {
            case u'd':
                when = now.addDays(-count);
                break;
            case u'w':
                when = now.addDays(-count * 7);
                break;
            case u'm':
                when = now.addMonths(-static_cast<int>(count));
                break;
            default:
                when = now.addYears(-static_cast<int>(count));
                break;
        }

        // And still checked afterwards. Handing on a date that is not a date
        // would put an unusable bound into the scope, and nothing downstream
        // would say why nothing matched.
        return when.isValid() ? std::optional<QDateTime>(when) : std::nullopt;
    }

    const QDateTime absolute = QDateTime::fromString(trimmed, Qt::ISODate);
    return absolute.isValid() ? std::optional<QDateTime>(absolute) : std::nullopt;
}

std::optional<QDateTime> timeFromText(const QString& text) {
    return timeFromText(text, QDateTime::currentDateTime());
}

QSet<QString> extensionsFromText(const QString& text) {
    QSet<QString> extensions;
    for (const QString& piece : text.split(u',', Qt::SkipEmptyParts)) {
        QString extension = piece.trimmed().toLower();
        while (extension.startsWith(u'.')) {
            extension.remove(0, 1);
        }
        if (!extension.isEmpty()) {
            extensions.insert(extension);
        }
    }
    return extensions;
}

}  // namespace transmit::core
