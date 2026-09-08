#include "core/rewrite/PathRewriter.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

#include <string>

#include "core/rewrite/formats/Rewriters.h"
#include "core/utils/Logging.h"
#include "format/PathToken.h"

namespace transmit::core {
namespace {

/// Whether a file a rule named is inside the folder the rule was given.
///
/// A rewrite rule's file pattern arrives in the archive's application list, so
/// it is a claim made by whoever wrote the archive rather than a fact. Joined
/// to the state root and opened, "../../../../.bashrc" is a real path on this
/// machine - and what happens to a file this pass names is that it gets
/// opened, edited and swapped in. The catalogue's schema refuses ".." in a
/// pattern, but that is a check on the file this project ships: a rule can
/// also come out of an archive, or out of an overlay in the user's own
/// configuration folder, and neither goes past the schema. So the promise is
/// kept where it is made, the way PathTokenMap::resolve keeps the same one.
bool insideTheFolderItWasGiven(const QString& stateRoot, const QString& candidate) {
    const std::string root = QDir::cleanPath(stateRoot).toStdString();
    const std::string path = QDir::cleanPath(candidate).toStdString();
    return format::isWithin(root, path, format::hostOsFamily());
}

/// Expands a rule's wildcard file pattern into the files that actually exist
/// under the restored state directory.
QStringList matchFiles(const QString& stateRoot, const QString& pattern) {
    QStringList found;
    if (pattern.isEmpty()) {
        return found;
    }

    const auto accept = [&found, &stateRoot](const QString& path) {
        if (insideTheFolderItWasGiven(stateRoot, path)) {
            found << path;
        } else {
            qCWarning(logRewrite) << "refusing a rewrite rule that names a file outside"
                                  << stateRoot << ":" << path;
        }
    };

    // A pattern with no wildcard is the common case and needs no walking.
    if (!pattern.contains(u'*') && !pattern.contains(u'?')) {
        const QString direct = stateRoot + u'/' + pattern;
        if (QFileInfo::exists(direct)) {
            accept(direct);
        }
        return found;
    }

    QString expression = QRegularExpression::escape(pattern);

    // "**/" is any number of folders including none, which is what everything
    // else that reads a glob means by it and what somebody writing
    // "**/prefs.js" in a recipe is asking for. It used to become ".*/", which
    // demands at least one folder - so a file sitting directly in the state
    // root was not matched, the rule did nothing, and the restore reported
    // success with the paths inside that file still pointing at the old
    // machine. The order matters: this has to run before the plain "**".
    expression.replace(QStringLiteral("\\*\\*\\/"), QStringLiteral("(?:.*/)?"));
    expression.replace(QStringLiteral("\\*\\*"), QStringLiteral(".*"));
    expression.replace(QStringLiteral("\\*"), QStringLiteral("[^/]*"));
    expression.replace(QStringLiteral("\\?"), QStringLiteral("[^/]"));

    const QRegularExpression matcher(QStringLiteral("\\A%1\\z").arg(expression),
                                     QRegularExpression::CaseInsensitiveOption);

    QDirIterator iterator(stateRoot, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    const qsizetype prefix = stateRoot.size() + 1;
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (matcher.match(path.mid(prefix)).hasMatch()) {
            accept(path);
        }
    }
    return found;
}

/// Files worth scanning in the broad pass: text-like and small enough that
/// reading them all is not the dominant cost of a restore.
bool isPlausibleTextFile(const QFileInfo& info) {
    static const QStringList suffixes = {
        QStringLiteral("conf"), QStringLiteral("cfg"),  QStringLiteral("ini"),
        QStringLiteral("json"), QStringLiteral("toml"), QStringLiteral("yaml"),
        QStringLiteral("yml"),  QStringLiteral("xml"),  QStringLiteral("txt"),
        QStringLiteral("rc"),   QStringLiteral("js"),   QStringLiteral("lua")};

    constexpr qint64 kMaxScanBytes = 4 * 1024 * 1024;
    return info.size() <= kMaxScanBytes && suffixes.contains(info.suffix().toLower());
}

}  // namespace

PathRewriter::PathRewriter(const PathTranslator& translator) : translator_(translator) {}

QStringList PathRewriter::supportedFormats() {
    return {QStringLiteral("text"), QStringLiteral("json"), QStringLiteral("ini"),
            QStringLiteral("plist"), QStringLiteral("sqlite")};
}

void PathRewriter::planForRule(const RecipeRewriteRule& rule, const QString& stateRoot,
                               const QString& appId, RewritePlan& plan) const {
    for (const QString& path : matchFiles(stateRoot, rule.filePattern)) {
        QList<RewriteEdit> edits;

        if (rule.format == QLatin1String("json")) {
            edits = rewriters::rewriteJson(path, rule.keys, translator_, appId);
        } else if (rule.format == QLatin1String("ini")) {
            edits = rewriters::rewriteIni(path, rule.keys, translator_, appId);
        } else if (rule.format == QLatin1String("plist")) {
            edits = rewriters::rewritePlist(path, rule.keys, translator_, appId);
        } else if (rule.format == QLatin1String("sqlite")) {
            edits = rewriters::rewriteSqlite(path, rule.table, rule.column, translator_, appId);
        } else if (rule.format == QLatin1String("text")) {
            edits =
                rewriters::rewriteText(path, rule.pattern, rule.captureGroup, translator_, appId);
        } else {
            qCWarning(logRewrite) << "unknown rewrite format" << rule.format << "for" << appId;
            continue;
        }

        for (RewriteEdit& edit : edits) {
            plan.add(std::move(edit));
        }
    }
}

void PathRewriter::planFor(const AppRecipe& recipe, const QString& stateRoot,
                           RewritePlan& plan) const {
    if (!QFileInfo::exists(stateRoot)) {
        return;
    }
    for (const RecipeRewriteRule& rule : recipe.rewrites) {
        planForRule(rule, stateRoot, recipe.id, plan);
    }
}

void PathRewriter::planHeuristic(const QString& root, const QString& appId,
                                 RewritePlan& plan) const {
    // Deliberately opt-in. Every path-shaped string in every text file is a
    // much wider net than the recipes cast, and a false positive here edits a
    // file nobody asked us to touch.
    QDirIterator iterator(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (!isPlausibleTextFile(iterator.fileInfo())) {
            continue;
        }

        // Any absolute path on a line, whatever it is called.
        const QList<RewriteEdit> edits = rewriters::rewriteText(
            path, QStringLiteral(R"(([A-Za-z]:[\\/][^\s"'<>|*?]*|/[A-Za-z0-9_.\-/ ]{4,}))"), 1,
            translator_, appId);
        for (const RewriteEdit& edit : edits) {
            plan.add(edit);
        }
    }
}

}  // namespace transmit::core
