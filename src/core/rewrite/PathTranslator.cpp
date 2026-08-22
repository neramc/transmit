#include "core/rewrite/PathTranslator.h"

#include <QRegularExpression>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"

namespace transmit::core {
namespace {

/// Matches the shapes an absolute path takes on the platforms Transmit
/// supports, so a value can be examined before anything is changed.
///   C:\Users\bob\x   C:/Users/bob/x   \\server\share\x   /home/bob/x
const QRegularExpression& absolutePathPattern() {
    static const QRegularExpression pattern(
        QStringLiteral(R"((?:[A-Za-z]:[\\/]|\\\\[^\\/]+[\\/]|/)[^\s"'<>|*?]*)"));
    return pattern;
}

/// A URI form that shows up in bookmarks and in LibreOffice's registry.
const QRegularExpression& fileUriPattern() {
    static const QRegularExpression pattern(QStringLiteral(R"(file://(/[^\s"'<>]*))"));
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
    const auto match = absolutePathPattern().match(text);
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

    const QString relative =
        applyRenames(tokenised.token, fromUtf8(tokenised.relative));

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

    auto pathMatches = absolutePathPattern().globalMatch(working);
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
