#pragma once

#include <QList>

#include "core/recipe/AppRecipe.h"
#include "format/Bytes.h"
#include "format/Manifest.h"

namespace transmit::core {

/// The application record carried inside the archive.
///
/// The recipe travels with the capture rather than being looked up again on
/// the far side. That keeps the archive self-describing: a machine with an
/// older catalog, or none at all, still knows where this application's state
/// belongs here and which files inside it hold paths.
struct InventoryEntry {
    QString recipeId;
    QString displayName;
    QString installedVersion;
    QString packageSource;  ///< how it was installed on the source machine
    QHash<QString, QString> installIds;
    QList<RecipeStatePath> state;
    QList<RecipeRewriteRule> rewrites;
    QList<RecipeMoveStep> moves;

    /// Whether this application's own data can travel, as the source
    /// machine's catalog understood it. Carried rather than looked up again,
    /// because the target's catalog may be older and would then quietly
    /// disagree with what the person was shown when they chose.
    bool carriesData = false;

    ContinuityGrade expectedGrade = ContinuityGrade::Full;
    QString note;

    [[nodiscard]] AppRecipe toRecipe() const;
};

format::ByteBuffer encodeAppInventory(const QList<MatchedApp>& matched);
QList<InventoryEntry> decodeAppInventory(format::ByteView data);

}  // namespace transmit::core
