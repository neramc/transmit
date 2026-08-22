#include "core/recipe/StateRelocator.h"

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"

namespace transmit::core {
namespace {

/// Splits "{APPCONFIG}/Mozilla/Firefox" into a tokenised path. Returns an
/// invalid path when the text is not in that form.
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

/// Whether `candidate` is at or below `root`, comparing whole components so
/// "{HOME}/.mozillax" does not count as being inside "{HOME}/.mozilla".
bool isWithin(const format::TokenizedPath& candidate, const format::TokenizedPath& root) {
    if (candidate.token != root.token) {
        return false;
    }
    if (root.relative.empty()) {
        return true;
    }
    if (candidate.relative == root.relative) {
        return true;
    }
    return candidate.relative.size() > root.relative.size() &&
           candidate.relative.compare(0, root.relative.size(), root.relative) == 0 &&
           candidate.relative[root.relative.size()] == '/';
}

}  // namespace

StateRelocator::StateRelocator(const QList<InventoryEntry>& inventory, OsFamily sourceOs,
                               OsFamily targetOs) {
    if (sourceOs == targetOs) {
        return;  // nothing moves within one operating system
    }

    for (const InventoryEntry& entry : inventory) {
        for (const RecipeStatePath& state : entry.state) {
            const auto from = parseTokenised(state.forOs(sourceOs));
            const auto to = parseTokenised(state.forOs(targetOs));
            if (!from.has_value() || !to.has_value() || *from == *to) {
                continue;
            }

            rules_.push_back(Rule{entry.recipeId, entry.displayName, *from, *to});
            qCDebug(logRestore) << entry.displayName << "moves from"
                                << fromUtf8(from->toDisplayString()) << "to"
                                << fromUtf8(to->toDisplayString());
        }
    }
}

const StateRelocator::Rule* StateRelocator::ruleFor(const format::TokenizedPath& captured) const {
    const Rule* best = nullptr;
    for (const Rule& rule : rules_) {
        // Longest match wins, so a nested state directory is not claimed by an
        // application that merely owns its parent.
        if (isWithin(captured, rule.from) &&
            (best == nullptr || rule.from.relative.size() > best->from.relative.size())) {
            best = &rule;
        }
    }
    return best;
}

format::TokenizedPath StateRelocator::relocate(const format::TokenizedPath& captured) const {
    const Rule* rule = ruleFor(captured);
    if (rule == nullptr) {
        return captured;
    }

    std::string below =
        captured.relative.substr(std::min(rule->from.relative.size(), captured.relative.size()));
    while (!below.empty() && below.front() == '/') {
        below.erase(0, 1);
    }

    format::TokenizedPath moved = rule->to;
    if (!below.empty()) {
        moved.relative = format::joinPath(moved.relative, below);
    }
    return moved;
}

}  // namespace transmit::core
