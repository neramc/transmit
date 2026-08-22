#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include "core/recipe/AppRecipe.h"
#include "platform/PlatformService.h"

namespace transmit::core {

/// Loads the application recipes and matches them against what is installed.
///
/// The catalog ships with the application and can be extended by dropping JSON
/// files into the user's configuration directory, so an application Transmit
/// has never heard of can still be carried across without a code change.
class RecipeCatalog {
public:
    RecipeCatalog();

    /// Loads the built-in catalog followed by any user overlays. Later
    /// definitions replace earlier ones with the same id, which is what makes
    /// an overlay able to correct a built-in entry.
    void loadDefaults();

    /// Adds recipes from a JSON document. Returns the number accepted; a
    /// malformed entry is skipped and logged rather than failing the load.
    int loadFromJson(const QByteArray& json, QString* errorMessage = nullptr);
    int loadFromFile(const QString& path);
    int loadFromDirectory(const QString& directory);

    [[nodiscard]] QList<AppRecipe> recipes() const { return recipes_.values(); }
    [[nodiscard]] AppRecipe recipeById(const QString& id) const { return recipes_.value(id); }
    [[nodiscard]] int count() const { return static_cast<int>(recipes_.size()); }

    /// Pairs installed applications with the recipes that describe them.
    [[nodiscard]] QList<MatchedApp> match(const QList<platform::InstalledApp>& installed,
                                          OsFamily os) const;

    /// Recipes whose state directory exists here even though no installation
    /// was detected. Covers portable installs and applications the package
    /// manager does not know about.
    [[nodiscard]] QList<MatchedApp> matchByStateOnly(const QList<MatchedApp>& alreadyMatched,
                                                     OsFamily os,
                                                     const format::PathTokenMap& folders) const;

    /// Turns matched applications into capture roots. Directories that do not
    /// exist here are dropped, so a recipe listing three possible locations
    /// contributes only the ones that are real.
    [[nodiscard]] QList<CaptureRoot> captureRootsFor(const QList<MatchedApp>& matched, OsFamily os,
                                                     const format::PathTokenMap& folders) const;

    /// The directory user overlays are read from.
    [[nodiscard]] static QString userCatalogDirectory();

    /// Turns a tokenised recipe path such as "{APPCONFIG}/Mozilla/Firefox" into
    /// an absolute path on the machine described by `folders`. Empty when the
    /// token is unknown or that machine has no such folder.
    [[nodiscard]] static QString resolveStatePath(const QString& tokenised,
                                                  const format::PathTokenMap& folders);

private:
    QHash<QString, AppRecipe> recipes_;
};

}  // namespace transmit::core
