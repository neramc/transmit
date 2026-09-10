#pragma once

#include <QList>
#include <QSet>
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

    /// One "this file, moving from here to there, is handled like this" step.
    ///
    /// Correcting the paths inside a file is not the whole of moving it. An
    /// index keyed by a hash of the old installation directory has to go; a
    /// setting that records the build the profile last ran has to come out, or
    /// the application reads it and believes it has already dealt with this
    /// profile; a Firefox profiles.ini has to be forced to relative, or it
    /// goes on naming an address that only existed on the old machine. The
    /// catalogue has said all of this for a while and nothing read it.
    void planForMove(const RecipeMoveStep& step, const QString& stateRoot, const QString& appId,
                     RewritePlan& plan) const;

    /// Throws away anything a previous run left staged under this folder,
    /// once per folder.
    ///
    /// A pass now reads what is staged so that two rules over one file
    /// compose, which means a staged file left behind by a run that died
    /// between staging and applying would be read as though this pass had
    /// written it. Nothing but Transmit writes that suffix, and a restore has
    /// just put the real files down, so anything wearing it here is litter.
    /// Once per folder, because planning runs per recipe and per state root
    /// and two of those can name the same directory - clearing on every call
    /// would throw away the staging done by the call before.
    void forgetAnythingLeftStaged(const QString& stateRoot) const;

    const PathTranslator& translator_;
    mutable QSet<QString> cleared_;
};

}  // namespace transmit::core
