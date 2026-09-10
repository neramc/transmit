#include "core/recipe/RestoreSkips.h"

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"

namespace transmit::core {
namespace {

/// Splits "{APPCONFIG}/Code/User" into a tokenised path.
std::optional<format::TokenizedPath> parseTokenised(const QString& text) {
    const qsizetype close = text.indexOf(u'}');
    if (!text.startsWith(u'{') || close < 0) {
        return std::nullopt;
    }
    const auto token = format::tokenFromName(toUtf8(text.left(close + 1)));
    if (!token) {
        return std::nullopt;
    }

    QString relative = text.mid(close + 1);
    while (relative.startsWith(u'/')) {
        relative.remove(0, 1);
    }
    return format::TokenizedPath{*token, toUtf8(relative)};
}

/// The same wildcard rules the rewrite pass reads, plus one thing: a pattern
/// that names a directory takes everything inside it. "workspaceStorage" is
/// written to mean the folder and all of it, and a person writing that in a
/// recipe is not going to add a second rule for its contents.
QRegularExpression matcherFor(const QString& pattern) {
    QString expression = QRegularExpression::escape(pattern);
    expression.replace(QStringLiteral("\\*\\*\\/"), QStringLiteral("(?:.*/)?"));
    expression.replace(QStringLiteral("\\*\\*"), QStringLiteral(".*"));
    expression.replace(QStringLiteral("\\*"), QStringLiteral("[^/]*"));
    expression.replace(QStringLiteral("\\?"), QStringLiteral("[^/]"));
    return QRegularExpression(QStringLiteral("\\A%1(?:/.*)?\\z").arg(expression),
                              QRegularExpression::CaseInsensitiveOption);
}

/// What is left of `candidate` once `root` is taken off the front, or nothing
/// when it is not under that root. Whole components, so "{HOME}/.mozillax" is
/// not inside "{HOME}/.mozilla".
std::optional<QString> below(const format::TokenizedPath& candidate,
                             const format::TokenizedPath& root) {
    if (candidate.token != root.token) {
        return std::nullopt;
    }
    if (root.relative.empty()) {
        return fromUtf8(candidate.relative);
    }
    if (candidate.relative.size() <= root.relative.size() ||
        candidate.relative.compare(0, root.relative.size(), root.relative) != 0 ||
        candidate.relative[root.relative.size()] != '/') {
        return std::nullopt;
    }
    return fromUtf8(candidate.relative.substr(root.relative.size() + 1));
}

}  // namespace

RestoreSkips::RestoreSkips(const QList<InventoryEntry>& inventory, OsFamily sourceOs,
                           OsFamily targetOs) {
    for (const InventoryEntry& entry : inventory) {
        const AppRecipe recipe = entry.toRecipe();

        for (const RecipeMoveStep& step : recipe.moves) {
            if (step.action != MoveAction::Skip || step.file.isEmpty() ||
                !step.appliesTo(sourceOs, targetOs)) {
                continue;
            }

            for (const RecipeStatePath& state : recipe.state) {
                if (!step.rootId.isEmpty() && state.id != step.rootId) {
                    continue;
                }
                const auto root = parseTokenised(state.forOs(targetOs));
                if (!root.has_value()) {
                    continue;
                }

                rules_.push_back(Rule{entry.recipeId, entry.displayName, *root, step.file,
                                      step.note, matcherFor(step.file)});
                qCDebug(logRestore) << entry.displayName << "leaves behind" << step.file << "under"
                                    << fromUtf8(root->toDisplayString());
            }
        }
    }
}

const RestoreSkips::Rule* RestoreSkips::ruleFor(const format::TokenizedPath& placed) const {
    for (const Rule& rule : rules_) {
        const auto relative = below(placed, rule.root);
        if (relative.has_value() && rule.matcher.match(*relative).hasMatch()) {
            return &rule;
        }
    }
    return nullptr;
}

}  // namespace transmit::core
