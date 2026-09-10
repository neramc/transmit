#include <QFile>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QStringEncoder>

#include "core/rewrite/formats/Rewriters.h"
#include "core/utils/Logging.h"

namespace transmit::core::rewriters {
namespace {

/// Which encoding a text file is in, and whether it carried a byte order mark.
/// Getting this wrong would turn a UTF-16 preferences file into mojibake, so
/// the original form is detected and reproduced exactly.
struct TextEncoding {
    QStringConverter::Encoding encoding = QStringConverter::Utf8;
    bool byteOrderMark = false;
};

TextEncoding detectEncoding(const QByteArray& raw) {
    TextEncoding form;
    if (raw.startsWith("\xEF\xBB\xBF")) {
        form.encoding = QStringConverter::Utf8;
        form.byteOrderMark = true;
    } else if (raw.startsWith("\xFF\xFE")) {
        form.encoding = QStringConverter::Utf16LE;
        form.byteOrderMark = true;
    } else if (raw.startsWith("\xFE\xFF")) {
        form.encoding = QStringConverter::Utf16BE;
        form.byteOrderMark = true;
    }
    return form;
}

QString decode(const QByteArray& raw, const TextEncoding& form) {
    QStringDecoder decoder(form.encoding, form.byteOrderMark
                                              ? QStringConverter::Flag::Default
                                              : QStringConverter::Flag::ConvertInvalidToNull);
    return decoder.decode(raw);
}

QByteArray encode(const QString& text, const TextEncoding& form) {
    QStringEncoder encoder(form.encoding, form.byteOrderMark ? QStringConverter::Flag::WriteBom
                                                             : QStringConverter::Flag::Default);
    return encoder.encode(text);
}

}  // namespace

QList<RewriteEdit> rewriteText(const QString& path, const QString& pattern, int captureGroup,
                               const PathTranslator& translator, const QString& appId) {
    QList<RewriteEdit> edits;

    const QByteArray raw = readForStaging(path);
    if (raw.isEmpty()) {
        return edits;
    }

    const TextEncoding form = detectEncoding(raw);
    QString text = decode(raw, form);
    if (text.isEmpty()) {
        return edits;
    }

    const QRegularExpression expression(pattern, QRegularExpression::MultilineOption);
    if (!expression.isValid()) {
        qCWarning(logRewrite) << "invalid rewrite pattern" << pattern << expression.errorString();
        return edits;
    }

    // Collected first and applied back to front, so each replacement does not
    // shift the offsets of the ones still to come.
    struct Replacement {
        qsizetype start = 0;
        qsizetype length = 0;
        QString value;
    };
    QList<Replacement> replacements;

    auto matches = expression.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const int group = (captureGroup <= match.lastCapturedIndex()) ? captureGroup : 0;
        const QString captured = match.captured(group);
        if (captured.isEmpty()) {
            continue;
        }

        int changes = 0;
        const QString rewritten = translator.translateWithin(captured, &changes);
        if (changes == 0 || rewritten == captured) {
            continue;
        }

        replacements.append({match.capturedStart(group), match.capturedLength(group), rewritten});
        edits.append(RewriteEdit{
            path,
            QStringLiteral("line %1").arg(text.left(match.capturedStart(group)).count(u'\n') + 1),
            captured, rewritten, appId});
    }

    if (replacements.isEmpty()) {
        return edits;
    }

    std::sort(replacements.begin(), replacements.end(),
              [](const Replacement& a, const Replacement& b) { return a.start > b.start; });
    for (const Replacement& replacement : replacements) {
        text.replace(replacement.start, replacement.length, replacement.value);
    }

    if (!writeStaged(path + QStringLiteral(".transmit-staged"), encode(text, form))) {
        return {};
    }
    return edits;
}

namespace {

/// Whether one of the names asks for this setting. A name ending in "."
/// asks for the family beneath it, which is how a catalogue writes
/// "every gfx.blacklist.* this machine's old graphics card produced".
bool namesTheSetting(const QStringList& keys, const QString& key) {
    for (const QString& wanted : keys) {
        if (wanted.endsWith(u'.') ? key.startsWith(wanted) : key == wanted) {
            return true;
        }
    }
    return false;
}

/// The setting a line declares, for the line-per-setting shape:
///   user_pref("browser.startup.homepage", "about:home");
/// Empty for a line that declares nothing, which includes every comment and
/// every blank line - both of which have to survive untouched.
QString settingOnLine(const QString& line) {
    static const QRegularExpression declaration(
        QStringLiteral("^\\s*[A-Za-z_][A-Za-z0-9_]*\\s*\\(\\s*[\"']([^\"']+)[\"']\\s*,"));
    const auto match = declaration.match(line);
    return match.hasMatch() ? match.captured(1) : QString();
}

}  // namespace

QList<RewriteEdit> dropKeysText(const QString& path, const QStringList& keys,
                                const QString& appId) {
    QList<RewriteEdit> edits;
    if (keys.isEmpty()) {
        return edits;
    }

    const QByteArray raw = readForStaging(path);
    if (raw.isEmpty()) {
        return edits;
    }

    // Through the same detect-and-reproduce as the rewriting pass, or a
    // UTF-16 preferences file would come back as mojibake with the settings
    // correctly removed from it.
    const TextEncoding form = detectEncoding(raw);
    const QStringList lines = decode(raw, form).split(u'\n');
    QStringList kept;
    kept.reserve(lines.size());

    for (const QString& line : lines) {
        const QString key = settingOnLine(line);
        if (key.isEmpty() || !namesTheSetting(keys, key)) {
            kept.push_back(line);
            continue;
        }
        edits.append(RewriteEdit{path, key, line.trimmed(), QString(), appId, EditKind::Change});
    }

    if (edits.isEmpty()) {
        return edits;
    }
    if (!writeStaged(path + QStringLiteral(".transmit-staged"), encode(kept.join(u'\n'), form))) {
        return {};
    }
    return edits;
}

}  // namespace transmit::core::rewriters
