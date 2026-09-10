#include <QFile>
#include <QStringList>

#include "core/rewrite/formats/Rewriters.h"

namespace transmit::core::rewriters {
namespace {

/// True when the rule asks for this key. A rule may name "section/key" to be
/// specific, or just "key" to mean it anywhere in the file.
bool ruleWants(const QStringList& keys, const QString& section, const QString& key) {
    for (const QString& wanted : keys) {
        const qsizetype slash = wanted.indexOf(u'/');
        if (slash < 0) {
            if (key.compare(wanted, Qt::CaseInsensitive) == 0) {
                return true;
            }
        } else if (section.compare(wanted.left(slash), Qt::CaseInsensitive) == 0 &&
                   key.compare(wanted.mid(slash + 1), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

/// The section a "section/key" name asks for, or "*" for any.
bool sectionMatches(const QString& wanted, const QString& section) {
    return wanted == QLatin1String("*") || section.compare(wanted, Qt::CaseInsensitive) == 0;
}

/// Splits one line of an ini file into what precedes the value and the value,
/// with the trailing carriage return kept aside so it can go back on. Returns
/// -1 for a line that is not a setting.
qsizetype settingSplit(const QString& line) {
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(u'#') || trimmed.startsWith(u';') ||
        (trimmed.startsWith(u'[') && trimmed.endsWith(u']'))) {
        return -1;
    }
    return line.indexOf(u'=');
}

}  // namespace

QList<RewriteEdit> rewriteIni(const QString& path, const QStringList& keys,
                              const PathTranslator& translator, const QString& appId) {
    QList<RewriteEdit> edits;

    const QByteArray raw = readForStaging(path);
    if (raw.isEmpty()) {
        return edits;
    }

    // Edited line by line rather than through QSettings: a parse-and-re-emit
    // pass would drop comments, reorder keys and normalise quoting, all of
    // which the user would notice and none of which we were asked to do.
    const bool crlf = raw.contains("\r\n");
    QString text = QString::fromUtf8(raw);
    QStringList lines = text.split(u'\n');

    QString section;
    bool changed = false;

    for (qsizetype i = 0; i < lines.size(); ++i) {
        QString line = lines[i];
        const QString trimmedEnd = line.endsWith(u'\r') ? line.chopped(1) : line;
        const QString trimmed = trimmedEnd.trimmed();

        if (trimmed.startsWith(u'[') && trimmed.endsWith(u']')) {
            section = trimmed.mid(1, trimmed.size() - 2);
            continue;
        }
        if (trimmed.isEmpty() || trimmed.startsWith(u'#') || trimmed.startsWith(u';')) {
            continue;
        }

        const qsizetype equals = trimmedEnd.indexOf(u'=');
        if (equals < 0) {
            continue;
        }

        const QString key = trimmedEnd.left(equals).trimmed();
        if (!ruleWants(keys, section, key)) {
            continue;
        }

        const QString value = trimmedEnd.mid(equals + 1);
        int replacements = 0;
        const QString rewritten = translator.translateWithin(value, &replacements);
        if (replacements == 0 || rewritten == value) {
            continue;
        }

        lines[i] = trimmedEnd.left(equals + 1) + rewritten +
                   (line.endsWith(u'\r') ? QStringLiteral("\r") : QString());
        edits.append(RewriteEdit{path, section.isEmpty() ? key : section + u'/' + key,
                                 value.trimmed(), rewritten.trimmed(), appId});
        changed = true;
    }

    if (!changed) {
        return edits;
    }

    const QString output = lines.join(u'\n');
    Q_UNUSED(crlf);  // line endings are preserved per line, above
    if (!writeStaged(path + QStringLiteral(".transmit-staged"), output.toUtf8())) {
        return {};
    }
    return edits;
}

QList<RewriteEdit> dropKeysIni(const QString& path, const QStringList& keys, const QString& appId) {
    QList<RewriteEdit> edits;
    if (keys.isEmpty()) {
        return edits;
    }

    const QByteArray raw = readForStaging(path);
    if (raw.isEmpty()) {
        return edits;
    }

    QStringList lines = QString::fromUtf8(raw).split(u'\n');
    QStringList kept;
    kept.reserve(lines.size());

    QString section;
    for (const QString& line : lines) {
        const QString withoutReturn = line.endsWith(u'\r') ? line.chopped(1) : line;
        const QString trimmed = withoutReturn.trimmed();
        if (trimmed.startsWith(u'[') && trimmed.endsWith(u']')) {
            section = trimmed.mid(1, trimmed.size() - 2);
            kept.push_back(line);
            continue;
        }

        const qsizetype equals = settingSplit(withoutReturn);
        if (equals < 0) {
            kept.push_back(line);
            continue;
        }

        const QString key = withoutReturn.left(equals).trimmed();
        if (!ruleWants(keys, section, key)) {
            kept.push_back(line);
            continue;
        }

        edits.append(RewriteEdit{path, section.isEmpty() ? key : section + u'/' + key,
                                 withoutReturn.mid(equals + 1).trimmed(), QString(), appId,
                                 EditKind::Change});
    }

    if (edits.isEmpty()) {
        return edits;
    }
    if (!writeStaged(path + QStringLiteral(".transmit-staged"), kept.join(u'\n').toUtf8())) {
        return {};
    }
    return edits;
}

QList<RewriteEdit> assignIni(const QString& path, const QList<QPair<QString, QString>>& assignments,
                             const QString& appId) {
    QList<RewriteEdit> edits;
    if (assignments.isEmpty()) {
        return edits;
    }

    const QByteArray raw = readForStaging(path);
    if (raw.isEmpty()) {
        return edits;
    }

    QStringList lines = QString::fromUtf8(raw).split(u'\n');
    QString section;
    bool changed = false;

    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString line = lines[i];
        const bool hasReturn = line.endsWith(u'\r');
        const QString withoutReturn = hasReturn ? line.chopped(1) : line;
        const QString trimmed = withoutReturn.trimmed();

        if (trimmed.startsWith(u'[') && trimmed.endsWith(u']')) {
            section = trimmed.mid(1, trimmed.size() - 2);
            continue;
        }

        const qsizetype equals = settingSplit(withoutReturn);
        if (equals < 0) {
            continue;
        }
        const QString key = withoutReturn.left(equals).trimmed();

        for (const auto& [wanted, value] : assignments) {
            const qsizetype slash = wanted.indexOf(u'/');
            const QString wantedSection = slash < 0 ? QStringLiteral("*") : wanted.left(slash);
            const QString wantedKey = slash < 0 ? wanted : wanted.mid(slash + 1);

            if (!sectionMatches(wantedSection, section) ||
                key.compare(wantedKey, Qt::CaseInsensitive) != 0) {
                continue;
            }

            const QString existing = withoutReturn.mid(equals + 1).trimmed();
            if (existing == value) {
                break;  // already what it has to be
            }

            lines[i] = withoutReturn.left(equals + 1) + value +
                       (hasReturn ? QStringLiteral("\r") : QString());
            edits.append(RewriteEdit{path, section.isEmpty() ? key : section + u'/' + key, existing,
                                     value, appId, EditKind::Change});
            changed = true;
            break;
        }
    }

    if (!changed) {
        return edits;
    }
    if (!writeStaged(path + QStringLiteral(".transmit-staged"), lines.join(u'\n').toUtf8())) {
        return {};
    }
    return edits;
}

}  // namespace transmit::core::rewriters
