#include "core/recipe/AppRecipe.h"

#include "core/utils/Conversions.h"

namespace transmit::core {
namespace {

QString osKey(OsFamily os) {
    return fromUtf8(format::osFamilyName(os));
}

/// "*" matches any system, so a step that applies everywhere is written once.
bool osMatches(const QString& pattern, OsFamily os) {
    if (pattern.isEmpty() || pattern == QLatin1String("*")) {
        return true;
    }
    return pattern.compare(osKey(os), Qt::CaseInsensitive) == 0;
}

}  // namespace

QString contentRoleName(ContentRole role) {
    switch (role) {
        case ContentRole::Index:
            return QStringLiteral("index");
        case ContentRole::Settings:
            return QStringLiteral("settings");
        case ContentRole::Profile:
            return QStringLiteral("profile");
        case ContentRole::Database:
            return QStringLiteral("database");
        case ContentRole::Credentials:
            return QStringLiteral("credentials");
        case ContentRole::Extension:
            return QStringLiteral("extension");
        case ContentRole::Content:
            return QStringLiteral("content");
        case ContentRole::State:
            return QStringLiteral("state");
        case ContentRole::Cache:
            return QStringLiteral("cache");
        case ContentRole::Log:
            return QStringLiteral("log");
        case ContentRole::Lock:
            return QStringLiteral("lock");
        case ContentRole::Unknown:
            break;
    }
    return QStringLiteral("unknown");
}

ContentRole contentRoleFromName(const QString& name) {
    static const QHash<QString, ContentRole> kByName = {
        {QStringLiteral("index"), ContentRole::Index},
        {QStringLiteral("settings"), ContentRole::Settings},
        {QStringLiteral("profile"), ContentRole::Profile},
        {QStringLiteral("database"), ContentRole::Database},
        {QStringLiteral("credentials"), ContentRole::Credentials},
        {QStringLiteral("extension"), ContentRole::Extension},
        {QStringLiteral("content"), ContentRole::Content},
        {QStringLiteral("state"), ContentRole::State},
        {QStringLiteral("cache"), ContentRole::Cache},
        {QStringLiteral("log"), ContentRole::Log},
        {QStringLiteral("lock"), ContentRole::Lock},
    };
    return kByName.value(name.toLower(), ContentRole::Unknown);
}

QString portabilityName(Portability portability) {
    switch (portability) {
        case Portability::Rewrite:
            return QStringLiteral("rewrite");
        case Portability::SameOs:
            return QStringLiteral("same-os");
        case Portability::Never:
            return QStringLiteral("never");
        case Portability::Always:
            break;
    }
    return QStringLiteral("always");
}

Portability portabilityFromName(const QString& name) {
    if (name == QLatin1String("rewrite"))
        return Portability::Rewrite;
    if (name == QLatin1String("same-os"))
        return Portability::SameOs;
    if (name == QLatin1String("never"))
        return Portability::Never;
    return Portability::Always;
}

QString moveActionName(MoveAction action) {
    switch (action) {
        case MoveAction::Skip:
            return QStringLiteral("skip");
        case MoveAction::Rename:
            return QStringLiteral("rename");
        case MoveAction::Merge:
            return QStringLiteral("merge");
        case MoveAction::Regenerate:
            return QStringLiteral("regenerate");
        case MoveAction::Rewrite:
            return QStringLiteral("rewrite");
        case MoveAction::DropKeys:
            return QStringLiteral("drop-keys");
        case MoveAction::Copy:
            break;
    }
    return QStringLiteral("copy");
}

MoveAction moveActionFromName(const QString& name) {
    static const QHash<QString, MoveAction> kByName = {
        {QStringLiteral("copy"), MoveAction::Copy},
        {QStringLiteral("skip"), MoveAction::Skip},
        {QStringLiteral("rename"), MoveAction::Rename},
        {QStringLiteral("merge"), MoveAction::Merge},
        {QStringLiteral("regenerate"), MoveAction::Regenerate},
        {QStringLiteral("rewrite"), MoveAction::Rewrite},
        {QStringLiteral("drop-keys"), MoveAction::DropKeys},
    };
    return kByName.value(name.toLower(), MoveAction::Copy);
}

QString RecipeStatePath::forOs(OsFamily os) const {
    const QStringList candidates = candidatesForOs(os);
    return candidates.isEmpty() ? QString() : candidates.constFirst();
}

QStringList RecipeStatePath::candidatesForOs(OsFamily os) const {
    return candidatesByOs.value(osKey(os));
}

bool RecipeMoveStep::appliesTo(OsFamily from, OsFamily to) const {
    return osMatches(fromOs, from) && osMatches(toOs, to);
}

ContinuityGrade RecipePortability::gradeFor(OsFamily from, OsFamily to,
                                            ContinuityGrade fallback) const {
    // The most specific entry wins: an exact pair beats one with a wildcard,
    // which is what lets a recipe say "adapted everywhere, except to Windows,
    // where it is manual" without repeating itself for every other pair.
    const Pair* best = nullptr;
    int bestScore = -1;
    for (const Pair& pair : pairs) {
        const bool fromAny = pair.from == OsFamily::Unknown;
        const bool toAny = pair.to == OsFamily::Unknown;
        if ((!fromAny && pair.from != from) || (!toAny && pair.to != to)) {
            continue;
        }
        const int score = (fromAny ? 0 : 2) + (toAny ? 0 : 1);
        if (score > bestScore) {
            bestScore = score;
            best = &pair;
        }
    }
    return best != nullptr ? best->grade : fallback;
}

QString RecipePortability::reasonFor(OsFamily from, OsFamily to) const {
    for (const Pair& pair : pairs) {
        const bool fromAny = pair.from == OsFamily::Unknown;
        const bool toAny = pair.to == OsFamily::Unknown;
        if ((fromAny || pair.from == from) && (toAny || pair.to == to) && !pair.why.isEmpty()) {
            return pair.why;
        }
    }
    return {};
}

const RecipeStatePath* AppRecipe::rootById(const QString& rootId) const {
    for (const RecipeStatePath& root : state) {
        if (root.id == rootId) {
            return &root;
        }
    }
    return nullptr;
}

QList<RecipeMoveStep> AppRecipe::movesFor(OsFamily from, OsFamily to) const {
    QList<RecipeMoveStep> applicable;
    for (const RecipeMoveStep& step : moves) {
        if (step.appliesTo(from, to)) {
            applicable.push_back(step);
        }
    }
    return applicable;
}

}  // namespace transmit::core
