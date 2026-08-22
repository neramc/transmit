#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/continuity/ContinuityTypes.h"
#include "platform/PlatformService.h"

namespace transmit::core {

/// One directory an application keeps its state in, named per operating system
/// because the same application puts it somewhere different on each.
struct RecipeStatePath {
    QString role;                  ///< "profile", "config", "data" - shown in the report
    QHash<QString, QString> byOs;  ///< "windows"/"macos"/"linux" -> tokenised path
    QStringList excludePatterns;   ///< caches and lock files inside the state directory

    [[nodiscard]] QString forOs(OsFamily os) const;
};

/// A file inside an application's state whose contents refer to locations on
/// the old machine, and how to find those references safely.
///
/// This is deliberately narrow. A blanket search-and-replace across restored
/// files would eventually corrupt something; naming the file and the field
/// means the rewriter only touches what a human has confirmed is a path.
struct RecipeRewriteRule {
    QString filePattern;  ///< wildcard, relative to the state directory
    QString format;       ///< "json", "ini", "text", "plist" or "sqlite"
    QStringList keys;     ///< json pointer-ish paths, or ini "section/key" names
    QString pattern;      ///< regular expression, for the text format
    int captureGroup = 1;
    QString table;   ///< sqlite
    QString column;  ///< sqlite
};

/// Everything Transmit knows about one application.
struct AppRecipe {
    QString id;  ///< reverse-DNS, stable across versions and platforms
    QString displayName;

    /// Names this application is known by to each platform's package manager
    /// or application registry, used to recognise it on the source machine.
    QHash<QString, QStringList> detectNames;

    /// How to install it on each package manager, for the generated script.
    QHash<QString, QString> installIds;

    QList<RecipeStatePath> state;
    QList<RecipeRewriteRule> rewrites;

    /// Processes to ask the user to close, so the state is captured whole.
    QStringList quiesceProcesses;

    ContinuityGrade expectedGrade = ContinuityGrade::Full;
    QString note;  ///< shown in the report when this app takes part

    [[nodiscard]] bool isValid() const { return !id.isEmpty(); }
};

/// A recipe paired with the installation that matched it on this machine.
struct MatchedApp {
    AppRecipe recipe;
    platform::InstalledApp installation;
    bool hasState = false;  ///< at least one of its state directories exists here
};

}  // namespace transmit::core
