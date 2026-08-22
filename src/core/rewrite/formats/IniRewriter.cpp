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

}  // namespace

QList<RewriteEdit> rewriteIni(const QString& path, const QStringList& keys,
                              const PathTranslator& translator, const QString& appId) {
    QList<RewriteEdit> edits;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return edits;
    }
    const QByteArray raw = file.readAll();
    file.close();

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

        lines[i] = trimmedEnd.left(equals + 1) + rewritten + (line.endsWith(u'\r') ? QStringLiteral("\r") : QString());
        edits.append(RewriteEdit{path,
                                 section.isEmpty() ? key : section + u'/' + key,
                                 value.trimmed(), rewritten.trimmed(), appId});
        changed = true;
    }

    if (!changed) {
        return edits;
    }

    QFile staged(path + QStringLiteral(".transmit-staged"));
    if (!staged.open(QIODevice::WriteOnly)) {
        return {};
    }
    QString output = lines.join(u'\n');
    Q_UNUSED(crlf);  // line endings are preserved per line, above
    staged.write(output.toUtf8());
    return edits;
}

}  // namespace transmit::core::rewriters
