#pragma once

#include <QList>

#include "core/recipe/AppInventoryPayload.h"
#include "format/PathToken.h"

namespace transmit::core {

/// Moves an application's state to where that application looks for it on the
/// operating system being restored onto.
///
/// This is the difference between a Firefox profile merely arriving on the new
/// machine and Firefox actually opening with it. Linux keeps the profile in
/// ~/.mozilla/firefox, Windows in %APPDATA%\Mozilla\Firefox, macOS in
/// ~/Library/Application Support/Firefox. Restoring the captured location
/// verbatim would leave the profile somewhere the program never looks.
///
/// Only paths the archive's own recipes describe are moved. Anything else is
/// left exactly where it was captured, because guessing would be worse than
/// doing nothing.
class StateRelocator {
public:
    StateRelocator(const QList<InventoryEntry>& inventory, OsFamily sourceOs, OsFamily targetOs);

    /// Returns the path to restore to, which is the input unless a recipe says
    /// this application keeps its state somewhere else here.
    [[nodiscard]] format::TokenizedPath relocate(const format::TokenizedPath& captured) const;

    /// True when at least one application would move.
    [[nodiscard]] bool hasRelocations() const noexcept { return !rules_.isEmpty(); }

    struct Rule {
        QString appId;
        QString displayName;
        format::TokenizedPath from;
        format::TokenizedPath to;
    };

    [[nodiscard]] const QList<Rule>& rules() const noexcept { return rules_; }

    /// The rule that applies to a path, or nullptr. Used by the report to name
    /// the application responsible for a move.
    [[nodiscard]] const Rule* ruleFor(const format::TokenizedPath& captured) const;

private:
    QList<Rule> rules_;
};

}  // namespace transmit::core
