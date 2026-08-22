#pragma once

#include <QList>
#include <QString>

#include "core/recipe/AppRecipe.h"
#include "core/rewrite/PathTranslator.h"
#include "core/rewrite/RewritePlan.h"

namespace transmit::core {

/// Applies a recipe's rewrite rules to the files a restore just put down.
///
/// Each rule names a file and the fields within it that hold paths. Nothing
/// outside those fields is examined, which is what keeps the pass from
/// corrupting a configuration file by guessing.
class PathRewriter {
public:
    PathRewriter(const PathTranslator& translator);

    /// Builds the plan for one application's restored state directory.
    /// `stateRoot` is where that directory landed on this machine.
    void planFor(const AppRecipe& recipe, const QString& stateRoot, RewritePlan& plan) const;

    /// The optional broad pass: scan any text-like restored file for paths from
    /// the source machine. Off by default because a heuristic that touches
    /// files no one described will eventually damage one.
    void planHeuristic(const QString& root, const QString& appId, RewritePlan& plan) const;

    /// Formats this build can rewrite.
    [[nodiscard]] static QStringList supportedFormats();

private:
    void planForRule(const RecipeRewriteRule& rule, const QString& stateRoot, const QString& appId,
                     RewritePlan& plan) const;

    const PathTranslator& translator_;
};

}  // namespace transmit::core
