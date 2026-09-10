#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>

#include "core/recipe/AppInventoryPayload.h"
#include "format/PathToken.h"

namespace transmit::core {

/// What a recipe says should not be put down at all on this system.
///
/// Some of an application's state is worth carrying and some of it is worth
/// carrying only as far as the archive. A workspace cache keyed by folder
/// paths that no longer exist is the shape of it: taking it was cheap and
/// harmless, and putting it back gives the application a set of stale entries
/// to trip over on a machine where none of those folders are.
///
/// This is the restore's half of "how this file moves". The rest of a move
/// step decides what goes on inside a file once it has arrived; a skip decides
/// that it does not arrive, which cannot be done by editing it afterwards.
///
/// Only what the archive's own recipes name is left behind. Everything else is
/// restored, because a guess here loses somebody's data silently.
class RestoreSkips {
public:
    RestoreSkips(const QList<InventoryEntry>& inventory, OsFamily sourceOs, OsFamily targetOs);

    struct Rule {
        QString appId;
        QString displayName;
        format::TokenizedPath root;  ///< where that state lands on this system
        QString pattern;             ///< relative to the root, wildcards allowed
        QString note;                ///< the catalogue's reason, shown in the report
        QRegularExpression matcher;
    };

    /// True when at least one thing would be left behind.
    [[nodiscard]] bool hasAny() const noexcept { return !rules_.isEmpty(); }

    [[nodiscard]] const QList<Rule>& rules() const noexcept { return rules_; }

    /// The rule that says this path stays behind, or nullptr. The path is the
    /// one the restore would use, after any relocation - a skip is about where
    /// the file would land, not where it was taken from.
    [[nodiscard]] const Rule* ruleFor(const format::TokenizedPath& placed) const;

private:
    QList<Rule> rules_;
};

}  // namespace transmit::core
