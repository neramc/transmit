#include "core/recipe/RecipeCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"

namespace transmit::core {
namespace {

QString osKey(OsFamily os) {
    return fromUtf8(format::osFamilyName(os));
}

ContinuityGrade gradeFromName(const QString& name) {
    if (name == QLatin1String("adapted"))
        return ContinuityGrade::Adapted;
    if (name == QLatin1String("manual"))
        return ContinuityGrade::Manual;
    if (name == QLatin1String("impossible"))
        return ContinuityGrade::Impossible;
    return ContinuityGrade::Full;
}

QStringList toStringList(const QJsonValue& value) {
    QStringList list;
    if (value.isString()) {
        list << value.toString();
    } else if (value.isArray()) {
        for (const QJsonValue& item : value.toArray()) {
            if (item.isString()) {
                list << item.toString();
            }
        }
    }
    return list;
}

RecipeContent readContent(const QJsonObject& object) {
    RecipeContent content;
    content.path = object.value(QStringLiteral("path")).toString();
    content.role = contentRoleFromName(object.value(QStringLiteral("role")).toString());
    content.format = object.value(QStringLiteral("format")).toString();
    content.portable = portabilityFromName(object.value(QStringLiteral("portable")).toString());
    content.sensitive = object.value(QStringLiteral("sensitive")).toBool(false);
    content.live = object.value(QStringLiteral("live")).toBool(false);
    content.note = object.value(QStringLiteral("note")).toString();

    // A cache is never worth carrying and a lock file means nothing off the
    // machine that made it. Saying so once here means every recipe does not
    // have to repeat "portable": "never" beside each of them.
    if (!object.contains(QStringLiteral("portable"))) {
        if (content.role == ContentRole::Cache || content.role == ContentRole::Lock ||
            content.role == ContentRole::Log) {
            content.portable = Portability::Never;
        } else if (content.role == ContentRole::Credentials) {
            content.portable = Portability::SameOs;
        }
    }
    if (content.role == ContentRole::Credentials && !object.contains(QStringLiteral("sensitive"))) {
        content.sensitive = true;
    }

    for (const QJsonValue& child : object.value(QStringLiteral("contents")).toArray()) {
        if (child.isObject()) {
            content.children.push_back(readContent(child.toObject()));
        }
    }
    return content;
}

/// Reads a state root in either schema.
///
/// Version 1 wrote one path per system directly on the object; version 2 puts
/// a list of candidates under "paths". Both are accepted for good: a user
/// overlay written against the old shape must keep working, and there is no
/// version of this program that can be trusted to rewrite somebody's own file
/// for them.
RecipeStatePath readStatePath(const QJsonObject& object) {
    RecipeStatePath state;
    state.role = object.value(QStringLiteral("role")).toString(QStringLiteral("config"));
    state.id = object.value(QStringLiteral("id")).toString(state.role);

    const QJsonObject paths = object.value(QStringLiteral("paths")).toObject();
    for (const QString& os :
         {QStringLiteral("windows"), QStringLiteral("macos"), QStringLiteral("linux")}) {
        QStringList candidates = toStringList(paths.value(os));
        if (candidates.isEmpty()) {
            candidates = toStringList(object.value(os));  // version 1
        }
        if (!candidates.isEmpty()) {
            state.candidatesByOs.insert(os, candidates);
        }
    }

    state.excludePatterns = toStringList(object.value(QStringLiteral("exclude")));
    for (const QJsonValue& item : object.value(QStringLiteral("contents")).toArray()) {
        if (item.isObject()) {
            state.contents.push_back(readContent(item.toObject()));
        }
    }
    return state;
}

RecipeRewriteRule readRewriteRule(const QJsonObject& object) {
    RecipeRewriteRule rule;
    rule.filePattern = object.value(QStringLiteral("file")).toString();
    rule.format = object.value(QStringLiteral("format")).toString(QStringLiteral("text"));
    rule.keys = toStringList(object.value(QStringLiteral("keys")));
    rule.pattern = object.value(QStringLiteral("pattern")).toString();
    rule.captureGroup = object.value(QStringLiteral("group")).toInt(1);
    rule.table = object.value(QStringLiteral("table")).toString();
    rule.column = object.value(QStringLiteral("column")).toString();
    return rule;
}

OsFamily osFromName(const QString& name) {
    if (name.isEmpty() || name == QLatin1String("*")) {
        return OsFamily::Unknown;
    }
    const auto parsed = format::osFamilyFromName(toUtf8(name));
    return parsed ? *parsed : OsFamily::Unknown;
}

RecipeMoveStep readMoveStep(const QJsonObject& object) {
    RecipeMoveStep step;
    const QJsonObject when = object.value(QStringLiteral("when")).toObject();
    step.fromOs = when.value(QStringLiteral("from")).toString(QStringLiteral("*"));
    step.toOs = when.value(QStringLiteral("to")).toString(QStringLiteral("*"));
    step.rootId = object.value(QStringLiteral("root")).toString();
    step.file = object.value(QStringLiteral("file")).toString();
    step.format = object.value(QStringLiteral("format")).toString();
    step.action = moveActionFromName(object.value(QStringLiteral("action")).toString());
    step.keys = toStringList(object.value(QStringLiteral("keys")));
    step.target = object.value(QStringLiteral("target")).toString();
    step.note = object.value(QStringLiteral("note")).toString();
    for (const QJsonValue& item : object.value(QStringLiteral("rewrite")).toArray()) {
        if (item.isObject()) {
            step.rewrites.push_back(readRewriteRule(item.toObject()));
        }
    }
    for (const QJsonValue& item : object.value(QStringLiteral("set")).toArray()) {
        const QJsonObject assignment = item.toObject();
        step.assignments.push_back(
            RecipeMoveStep::Assignment{assignment.value(QStringLiteral("key")).toString(),
                                       assignment.value(QStringLiteral("value")).toString()});
    }
    return step;
}

RecipePortability readPortability(const QJsonObject& object, const AppRecipe& recipe) {
    RecipePortability portability;

    // Defaulted from the state rather than assumed false: a recipe that names
    // somewhere its settings live can carry them, and requiring every one of
    // the seventy-three entries to say so again would only be a way to get it
    // wrong in a few of them.
    portability.carriesData =
        object.value(QStringLiteral("carries_data")).toBool(!recipe.state.isEmpty());

    for (const QJsonValue& item : object.value(QStringLiteral("pairs")).toArray()) {
        const QJsonObject pair = item.toObject();
        RecipePortability::Pair entry;
        entry.from = osFromName(pair.value(QStringLiteral("from")).toString());
        entry.to = osFromName(pair.value(QStringLiteral("to")).toString());
        entry.grade = gradeFromName(pair.value(QStringLiteral("grade")).toString());
        entry.why = pair.value(QStringLiteral("why")).toString();
        portability.pairs.push_back(entry);
    }
    return portability;
}

/// Whether an installed application answers to a name the recipe lists.
///
/// A plain prefix test is too eager: "Git" would then claim "GitHub Desktop".
/// A prefix only counts when what follows it is a separator, which is how
/// registry entries decorate a product name ("Mozilla Firefox (x64 en-GB)",
/// "Sublime Text 4").
bool namesMatch(const platform::InstalledApp& app, const QString& candidate) {
    if (app.id.compare(candidate, Qt::CaseInsensitive) == 0 ||
        app.displayName.compare(candidate, Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (!app.displayName.startsWith(candidate, Qt::CaseInsensitive)) {
        return false;
    }
    const QChar next = app.displayName.at(candidate.size());
    return next.isSpace() || next == u'(' || next == u'-' || next.isDigit();
}

/// Splits a tokenised recipe path such as "{APPCONFIG}/Mozilla/Firefox" into
/// its known-folder token and the part below it.
struct SplitStatePath {
    format::PathTokenId token = format::PathTokenId::Absolute;
    QString relative;
    bool valid = false;
};

SplitStatePath splitStatePath(const QString& tokenised) {
    SplitStatePath split;
    const qsizetype close = tokenised.indexOf(u'}');
    if (!tokenised.startsWith(u'{') || close < 0) {
        return split;
    }

    const auto token = format::tokenFromName(toUtf8(tokenised.left(close + 1)));
    if (!token) {
        return split;
    }

    split.token = *token;
    split.relative = tokenised.mid(close + 1);
    while (split.relative.startsWith(u'/')) {
        split.relative.remove(0, 1);
    }
    split.valid = true;
    return split;
}

}  // namespace

RecipeCatalog::RecipeCatalog() = default;

QString RecipeCatalog::userCatalogDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
           QStringLiteral("/catalog.d");
}

QString RecipeCatalog::resolveStatePath(const QString& tokenised,
                                        const format::PathTokenMap& folders) {
    const SplitStatePath split = splitStatePath(tokenised);
    if (!split.valid) {
        return {};
    }

    const auto base = folders.base(split.token);
    if (!base.has_value()) {
        return {};
    }
    return split.relative.isEmpty() ? fromUtf8(*base)
                                    : fromUtf8(format::joinPath(*base, toUtf8(split.relative)));
}

void RecipeCatalog::loadDefaults() {
    const int builtIn = loadFromFile(QStringLiteral(":/catalog/app-catalog.json"));
    const int overlay = loadFromDirectory(userCatalogDirectory());
    qCInfo(logRecipe) << "loaded" << builtIn << "built-in recipes and" << overlay
                      << "from user overlays";
}

int RecipeCatalog::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(logRecipe) << "could not read the recipe file" << path << file.errorString();
        return 0;
    }

    QString error;
    const int accepted = loadFromJson(file.readAll(), &error);
    if (!error.isEmpty()) {
        qCWarning(logRecipe) << path << error;
    }
    return accepted;
}

int RecipeCatalog::loadFromDirectory(const QString& directory) {
    const QDir dir(directory);
    if (!dir.exists()) {
        return 0;
    }

    int accepted = 0;
    // Sorted by name so an overlay's precedence is predictable rather than
    // whatever order the filesystem happens to return.
    for (const QString& name : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        accepted += loadFromFile(dir.absoluteFilePath(name));
    }
    return accepted;
}

int RecipeCatalog::loadFromJson(const QByteArray& json, QString* errorMessage) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMessage != nullptr) {
            *errorMessage = parseError.errorString();
        }
        return 0;
    }

    QJsonArray entries;
    if (document.isArray()) {
        entries = document.array();
    } else if (document.isObject()) {
        entries = document.object().value(QStringLiteral("apps")).toArray();
    }

    int accepted = 0;
    for (const QJsonValue& value : entries) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();

        AppRecipe recipe;
        recipe.id = object.value(QStringLiteral("id")).toString();
        if (recipe.id.isEmpty()) {
            // One malformed entry must not cost the user the whole catalog.
            qCWarning(logRecipe) << "skipping a recipe with no id";
            continue;
        }
        recipe.displayName = object.value(QStringLiteral("name")).toString(recipe.id);

        const QJsonObject detect = object.value(QStringLiteral("detect")).toObject();
        for (auto it = detect.constBegin(); it != detect.constEnd(); ++it) {
            recipe.detectNames.insert(it.key(), toStringList(it.value()));
        }

        const QJsonObject install = object.value(QStringLiteral("install")).toObject();
        for (auto it = install.constBegin(); it != install.constEnd(); ++it) {
            recipe.installIds.insert(it.key(), it.value().toString());
        }

        for (const QJsonValue& item : object.value(QStringLiteral("state")).toArray()) {
            if (item.isObject()) {
                recipe.state.push_back(readStatePath(item.toObject()));
            }
        }
        for (const QJsonValue& item : object.value(QStringLiteral("rewrite")).toArray()) {
            if (item.isObject()) {
                recipe.rewrites.push_back(readRewriteRule(item.toObject()));
            }
        }

        for (const QJsonValue& item : object.value(QStringLiteral("move")).toArray()) {
            if (item.isObject()) {
                recipe.moves.push_back(readMoveStep(item.toObject()));
            }
        }

        recipe.quiesceProcesses = toStringList(object.value(QStringLiteral("quiesce")));
        recipe.expectedGrade = gradeFromName(object.value(QStringLiteral("grade")).toString());
        recipe.note = object.value(QStringLiteral("note")).toString();
        recipe.portability =
            readPortability(object.value(QStringLiteral("portability")).toObject(), recipe);

        // A later definition replaces an earlier one with the same id, which is
        // what lets a user overlay correct a built-in entry.
        recipes_.insert(recipe.id, recipe);
        ++accepted;
    }
    return accepted;
}

QList<MatchedApp> RecipeCatalog::match(const QList<platform::InstalledApp>& installed,
                                       OsFamily os) const {
    const QString key = osKey(os);
    QList<MatchedApp> matched;

    for (const AppRecipe& recipe : recipes_) {
        const QStringList names = recipe.detectNames.value(key);
        if (names.isEmpty()) {
            continue;
        }

        for (const platform::InstalledApp& app : installed) {
            const bool hit = std::any_of(names.begin(), names.end(), [&app](const QString& name) {
                return !name.isEmpty() && namesMatch(app, name);
            });
            if (!hit) {
                continue;
            }

            MatchedApp result;
            result.recipe = recipe;
            result.installation = app;
            matched.push_back(result);
            break;  // one installation per recipe is enough
        }
    }

    std::sort(matched.begin(), matched.end(), [](const MatchedApp& a, const MatchedApp& b) {
        return a.recipe.displayName.localeAwareCompare(b.recipe.displayName) < 0;
    });
    return matched;
}

QList<MatchedApp> RecipeCatalog::matchByStateOnly(const QList<MatchedApp>& alreadyMatched,
                                                  OsFamily os,
                                                  const format::PathTokenMap& folders) const {
    QSet<QString> seen;
    for (const MatchedApp& match : alreadyMatched) {
        seen.insert(match.recipe.id);
    }

    QList<MatchedApp> extra;
    for (const AppRecipe& recipe : recipes_) {
        if (seen.contains(recipe.id)) {
            continue;
        }

        // A recipe whose data directory is present counts even when no package
        // manager knows about it: portable installs, sideloaded builds, and
        // applications uninstalled without their settings being cleaned up.
        for (const RecipeStatePath& state : recipe.state) {
            const QString absolute = resolveStatePath(state.forOs(os), folders);
            if (absolute.isEmpty() || !QFileInfo::exists(absolute)) {
                continue;
            }

            MatchedApp match;
            match.recipe = recipe;
            match.installation.id = recipe.id;
            match.installation.displayName = recipe.displayName;
            match.hasState = true;
            extra.push_back(match);
            break;
        }
    }

    std::sort(extra.begin(), extra.end(), [](const MatchedApp& a, const MatchedApp& b) {
        return a.recipe.displayName.localeAwareCompare(b.recipe.displayName) < 0;
    });
    return extra;
}

QList<CaptureRoot> RecipeCatalog::captureRootsFor(const QList<MatchedApp>& matched, OsFamily os,
                                                  const format::PathTokenMap& folders) const {
    QList<CaptureRoot> roots;

    for (const MatchedApp& match : matched) {
        for (const RecipeStatePath& state : match.recipe.state) {
            const QString tokenised = state.forOs(os);
            if (tokenised.isEmpty()) {
                continue;  // this application has no state on this platform
            }

            const SplitStatePath split = splitStatePath(tokenised);
            if (!split.valid) {
                qCWarning(logRecipe)
                    << match.recipe.id << "has an unusable state path" << tokenised;
                continue;
            }

            const QString absolute = resolveStatePath(tokenised, folders);
            if (absolute.isEmpty() || !QFileInfo::exists(absolute)) {
                continue;  // this machine does not have that directory
            }

            CaptureRoot root;
            root.token = split.token;
            root.relative = split.relative;
            root.domain = DomainId::AppState;
            root.appId = match.recipe.id;
            root.recursive = true;
            root.excludePatterns = state.excludePatterns;
            roots.push_back(root);
        }
    }
    return roots;
}

}  // namespace transmit::core
