#include "core/rewrite/PathTranslator.h"

#include <QRegularExpression>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"

namespace transmit::core {
namespace {

/// How an absolute path begins, on each of the platforms Transmit supports.
///   C:\Users\bob\x   C:/Users/bob/x   \\server\share\x   /home/bob/x
constexpr char kPathPrefix[] = R"((?:[A-Za-z]:[\\/]|\\\\[^\\/]+[\\/]|/))";

/// The characters that cannot appear in a path on any of them. A space is not
/// among them, and that omission is the whole difficulty: a space ends a path
/// when the path is quoted inside a sentence, and sits in the middle of one
/// almost everywhere else. macOS keeps application state under
/// "Library/Application Support" and Windows keeps programs under
/// "Program Files", so a matcher that stops at the first space stops before it
/// has reached the interesting part of nearly every path it is ever handed.
constexpr char kPathBody[] = R"([^\s"'<>|*?])";

/// So a space is taken as part of the path only when another component
/// follows it: something that is not itself a separator, then more ordinary
/// characters, then a separator. "Application Support/Firefox" qualifies and
/// is kept; "a.txt and then closed" does not, and the path ends at the file.
///
/// The rule is a guess either way - "My Notes" at the end of a sentence is
/// indistinguishable from prose - so it is made in the direction that cannot
/// corrupt anything: a component too few leaves a suffix that is carried
/// through unchanged, which is what would have happened had nothing matched.
/// The run is bounded so a long line cannot be walked repeatedly.
constexpr char kSpaceInsidePath[] = R"( (?=[^\s"'<>|*?\\/][^\s"'<>|*?]{0,255}[\\/]))";

/// The same folder written with either separator, since both are separators
/// on Windows and a value copied between programs may use either. What comes
/// in has been through QRegularExpression::escape, so every separator in it
/// carries a backslash in front; both forms go to a placeholder first, or the
/// character class this puts in would be rewritten by the following pass.
QString eitherSeparator(const QString& escaped) {
    const QString here = QStringLiteral("\x01");
    QString flexible = escaped;
    flexible.replace(QStringLiteral("\\\\"), here);
    flexible.replace(QStringLiteral("\\/"), here);
    flexible.replace(here, QStringLiteral("[\\\\/]"));
    return flexible;
}

/// Matches an absolute path, so a value can be examined before anything is
/// changed.
///
/// The folders the source machine declared come first, spelled out. Two of
/// them have spaces in the middle - macOS keeps application state under
/// "Library/Application Support" and its window state under "Library/Saved
/// Application State" - and where a path begins is not something to be
/// guessing at when the machine that wrote it said so exactly. Guessing is
/// left for what follows the folder, where there is nothing better.
///
/// Longest first, so a folder is not cut short by the parent it sits in.
QRegularExpression pathPatternFor(const format::PathTokenMap& folders, OsFamily os) {
    QStringList beginnings;
    for (const format::PathTokenId token : format::allTokens()) {
        const auto base = folders.base(token);

        // The upper bound is on the archive's word, not on ours: these come
        // from the environment the capture recorded, and a known folder is
        // never anywhere near this long. Escaping already stops a base from
        // meaning anything in the expression; this stops one from being the
        // size of the expression.
        constexpr int kLongestSensibleBase = 512;
        if (!base || base->size() < 2 || base->size() > kLongestSensibleBase) {
            continue;
        }
        const QString spelled = eitherSeparator(QRegularExpression::escape(fromUtf8(*base)));
        if (!beginnings.contains(spelled)) {
            beginnings.append(spelled);
        }
    }
    std::sort(beginnings.begin(), beginnings.end(),
              [](const QString& a, const QString& b) { return a.size() > b.size(); });
    beginnings.append(QString::fromLatin1(kPathPrefix));

    QRegularExpression pattern(QStringLiteral("(?:") + beginnings.join(u'|') +
                               QStringLiteral(")(?:") + QString::fromLatin1(kPathBody) + u'|' +
                               QString::fromLatin1(kSpaceInsidePath) + QStringLiteral(")*"));
    if (format::usesWindowsPathStyle(os)) {
        // The same folder in a different case is the same folder there, and
        // the value in the file was not necessarily written by the program
        // that made the directory.
        pattern.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    }
    return pattern;
}

/// A URI form that shows up in bookmarks and in LibreOffice's registry. A
/// space should arrive percent-encoded here and usually does, but macOS
/// property lists carry plenty that do not, so the same rule applies.
constexpr char kUriBody[] = R"RX([^\s"'<>])RX";

const QRegularExpression& fileUriPattern() {
    static const QRegularExpression pattern(
        QStringLiteral("file://(/(?:") + QString::fromLatin1(kUriBody) + u'|' +
        QString::fromLatin1(kSpaceInsidePath) + QStringLiteral(")*)"));
    return pattern;
}

}  // namespace

PathTranslator::PathTranslator(const format::SourceEnvironment& source,
                               format::PathTokenMap targetFolders, OsFamily targetOs)
    : sourceOs_(source.os),
      targetOs_(targetOs),
      sourceFolders_(source.os),
      targetFolders_(std::move(targetFolders)) {
    for (const auto& [token, base] : source.tokenBases) {
        sourceFolders_.setBase(token, base);
    }
    pathPattern_ = pathPatternFor(sourceFolders_, sourceOs_);
}

void PathTranslator::setRenames(const QList<QPair<QString, QString>>& renames) {
    renames_.clear();
    for (const auto& [original, applied] : renames) {
        renames_.insert(original, applied);
    }
}

QString PathTranslator::applyRenames(format::PathTokenId token, const QString& relative) const {
    if (renames_.isEmpty() || relative.isEmpty()) {
        return relative;
    }

    // The sanitiser records renames by their relative path, and a rename of a
    // parent directory applies to everything beneath it.
    if (const auto exact = renames_.constFind(relative); exact != renames_.constEnd()) {
        return exact.value();
    }
    for (auto it = renames_.constBegin(); it != renames_.constEnd(); ++it) {
        if (relative.startsWith(it.key() + u'/')) {
            return it.value() + relative.mid(it.key().size());
        }
    }
    Q_UNUSED(token);
    return relative;
}

bool PathTranslator::looksLikeSourcePath(const QString& text) const {
    if (text.isEmpty() || text.size() > 4096) {
        return false;
    }
    const auto match = pathPattern_.match(text);
    return match.hasMatch() && match.capturedStart() == 0;
}

std::optional<QString> PathTranslator::translate(const QString& sourcePath) const {
    if (!looksLikeSourcePath(sourcePath)) {
        return std::nullopt;
    }

    format::TokenizedPath tokenised = sourceFolders_.tokenize(toUtf8(sourcePath));
    if (tokenised.isAbsoluteFallback()) {
        // Not inside any folder Transmit knows about, so there is nothing
        // trustworthy to map it onto. Leaving it alone is the safe answer.
        return std::nullopt;
    }

    // The restore may have moved this application's state to the folder this
    // system keeps it in; the reference has to follow it there.
    if (relocator_ != nullptr) {
        tokenised = relocator_->relocate(tokenised);
    }

    const QString relative = applyRenames(tokenised.token, fromUtf8(tokenised.relative));

    const auto resolved =
        targetFolders_.resolve(format::TokenizedPath{tokenised.token, toUtf8(relative)});
    if (!resolved) {
        return std::nullopt;
    }
    return fromUtf8(format::toNativePath(*resolved, targetOs_));
}

QString PathTranslator::translateOr(const QString& sourcePath) const {
    return translate(sourcePath).value_or(sourcePath);
}

QString PathTranslator::translateWithin(const QString& text, int* replacements) const {
    if (replacements != nullptr) {
        *replacements = 0;
    }
    if (text.isEmpty()) {
        return text;
    }

    // file:// URIs first: their payload is a path, but the surrounding scheme
    // must survive, and on Windows the result needs a slash separator.
    QString working = text;
    auto uriMatches = fileUriPattern().globalMatch(working);
    QList<QPair<qsizetype, QPair<qsizetype, QString>>> edits;

    while (uriMatches.hasNext()) {
        const auto match = uriMatches.next();

        // "file:///C:/Users/..." carries the drive letter behind a leading
        // slash that is part of the URI, not of the path.
        QString candidate = match.captured(1);
        static const QRegularExpression driveAfterSlash(QStringLiteral("\\A/[A-Za-z]:"));
        if (driveAfterSlash.match(candidate).hasMatch()) {
            candidate.remove(0, 1);
        }

        const auto translated = translate(candidate);
        if (!translated.has_value()) {
            continue;
        }
        QString uriPath = *translated;
        uriPath.replace(u'\\', u'/');
        if (!uriPath.startsWith(u'/')) {
            uriPath.prepend(u'/');
        }
        edits.append({match.capturedStart(0),
                      {match.capturedLength(0), QStringLiteral("file://") + uriPath}});
    }

    auto pathMatches = pathPattern_.globalMatch(working);
    while (pathMatches.hasNext()) {
        const auto match = pathMatches.next();
        // Skip anything already covered by a URI edit.
        const bool overlaps = std::any_of(edits.begin(), edits.end(), [&match](const auto& edit) {
            return match.capturedStart(0) >= edit.first &&
                   match.capturedStart(0) < edit.first + edit.second.first;
        });
        if (overlaps) {
            continue;
        }
        const auto translated = translate(match.captured(0));
        if (translated.has_value()) {
            edits.append({match.capturedStart(0), {match.capturedLength(0), *translated}});
        }
    }

    // Apply back to front so earlier offsets stay valid.
    std::sort(edits.begin(), edits.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [start, edit] : edits) {
        working.replace(start, edit.first, edit.second);
    }

    if (replacements != nullptr) {
        *replacements = static_cast<int>(edits.size());
    }
    return working;
}

}  // namespace transmit::core
