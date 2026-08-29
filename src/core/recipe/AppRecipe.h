#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/continuity/ContinuityTypes.h"
#include "platform/PlatformService.h"

namespace transmit::core {

/// What a file inside an application's state is for.
///
/// The point of naming it is that the answer decides what happens to it. An
/// index has to be rewritten because it names paths; a cache is regenerable and
/// carrying it wastes the user's drive; a credential must not travel unless the
/// archive is encrypted and the user asked for it. Without this the catalog can
/// only say "copy this folder", which is why the old schema could not describe
/// a browser profile without either dragging half a gigabyte of cache along or
/// leaving the bookmarks behind.
enum class ContentRole {
    Unknown,
    Index,        ///< names other files, so it has to be rewritten
    Settings,     ///< preferences a person set
    Profile,      ///< a whole named profile directory
    Database,     ///< sqlite or similar; may be open while running
    Credentials,  ///< passwords, tokens, keys
    Extension,    ///< add-ons and their state
    Content,      ///< the user's own documents kept inside the application
    State,        ///< window positions, recent files - nice to have
    Cache,        ///< regenerable; never worth carrying
    Log,
    Lock,  ///< meaningless anywhere but the machine that made it
};

QString contentRoleName(ContentRole role);
ContentRole contentRoleFromName(const QString& name);

/// Whether a file survives the journey.
enum class Portability {
    Always,   ///< copies as-is and works
    Rewrite,  ///< works once the paths inside it are corrected
    SameOs,   ///< only meaningful on the same operating system
    Never,    ///< regenerable, machine-specific, or meaningless elsewhere
};

QString portabilityName(Portability portability);
Portability portabilityFromName(const QString& name);

/// What to do with one file when it moves between two systems.
enum class MoveAction {
    Copy,        ///< the default: take it as it is
    Skip,        ///< leave it behind
    Rename,      ///< the same content lives under a different name there
    Merge,       ///< combine with whatever is already at the target
    Regenerate,  ///< delete it and let the application rebuild it
    Rewrite,     ///< correct the paths or values inside it
    DropKeys,    ///< remove named settings that would confuse the application
};

QString moveActionName(MoveAction action);
MoveAction moveActionFromName(const QString& name);

/// One thing inside an application's state directory.
///
/// Nested, because that is how state is actually arranged: a profile holds
/// settings, a database, a cache and an extensions folder, and each of those
/// answers differently.
struct RecipeContent {
    QString path;  ///< literal or glob, relative to the state root
    ContentRole role = ContentRole::Unknown;
    QString format;  ///< "ini", "json", "sqlite", "text", "plist", "binary"
    Portability portable = Portability::Always;

    /// A credential. Refused outright unless the Secrets domain was selected
    /// and the archive is encrypted.
    bool sensitive = false;

    /// May be open and being written while the capture runs, so it has to be
    /// read through the consistent-copy path rather than straight off disk.
    bool live = false;

    QString note;
    QList<RecipeContent> children;
};

/// One directory an application keeps its state in, named per operating system
/// because the same application puts it somewhere different on each.
struct RecipeStatePath {
    /// Stable within the recipe, so a move step can say which root it means.
    /// Defaults to the role when the catalog does not give one.
    QString id;

    QString role;  ///< "profile", "config", "data" - shown in the report

    /// Candidates per OS, in order of preference: the first that exists is
    /// used. A single path in the old schema becomes a list of one. This is
    /// what lets a Flatpak or Snap installation be found, which the old schema
    /// could not express at all - it had one path per system and a
    /// sandboxed install simply looked like an application with no state.
    QHash<QString, QStringList> candidatesByOs;

    QStringList excludePatterns;  ///< caches and lock files inside the state directory
    QList<RecipeContent> contents;

    [[nodiscard]] QString forOs(OsFamily os) const;
    [[nodiscard]] QStringList candidatesForOs(OsFamily os) const;
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

/// "This file, moving from here to there, is handled like this."
///
/// The old schema could say where an application's state lives and which paths
/// inside it to correct. It could not say that a file has to be deleted so the
/// application rebuilds it, or that a setting has to be removed or the
/// application will believe it has already migrated. Both are needed to move a
/// Firefox profile between systems, and neither could be written down.
struct RecipeMoveStep {
    QString fromOs;  ///< an OS name, or "*" for any
    QString toOs;
    QString rootId;  ///< which state root this is relative to; empty means all
    QString file;    ///< literal or glob within that root
    QString format;
    MoveAction action = MoveAction::Copy;

    /// For Rewrite: which paths inside the file to correct, using the same
    /// narrow rules as the recipe's own rewrite list.
    QList<RecipeRewriteRule> rewrites;

    /// For Rewrite: settings to force to a fixed value, which is a different
    /// job from correcting a path. Firefox needs both in the same file -
    /// profiles.ini has a Path to correct and an IsRelative to force - and a
    /// rule that could only do one of them could not describe it.
    struct Assignment {
        QString key;
        QString value;
    };
    QList<Assignment> assignments;

    /// For DropKeys: the settings to remove. A trailing "." removes a family.
    QStringList keys;

    /// For Rename: where it goes instead.
    QString target;

    QString note;  ///< shown in the report when this step fires

    [[nodiscard]] bool appliesTo(OsFamily from, OsFamily to) const;
};

/// How well this application survives each journey, and whether its data
/// travels at all.
struct RecipePortability {
    /// The basis for "this application's data can come with you" in the
    /// interface. False means Transmit can record that it was installed and
    /// nothing more.
    bool carriesData = false;

    struct Pair {
        OsFamily from = OsFamily::Unknown;
        OsFamily to = OsFamily::Unknown;
        ContinuityGrade grade = ContinuityGrade::Full;
        QString why;
    };
    QList<Pair> pairs;

    /// The grade for one journey, or `fallback` when the catalog says nothing
    /// about that particular pair.
    [[nodiscard]] ContinuityGrade gradeFor(OsFamily from, OsFamily to,
                                           ContinuityGrade fallback) const;
    [[nodiscard]] QString reasonFor(OsFamily from, OsFamily to) const;
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
    QList<RecipeMoveStep> moves;
    RecipePortability portability;

    /// Processes to ask the user to close, so the state is captured whole.
    QStringList quiesceProcesses;

    ContinuityGrade expectedGrade = ContinuityGrade::Full;
    QString note;  ///< shown in the report when this app takes part

    [[nodiscard]] bool isValid() const { return !id.isEmpty(); }

    /// The state root with this id, or nullptr.
    [[nodiscard]] const RecipeStatePath* rootById(const QString& rootId) const;

    /// Every move step that applies to this journey, in catalog order.
    [[nodiscard]] QList<RecipeMoveStep> movesFor(OsFamily from, OsFamily to) const;
};

/// A recipe paired with the installation that matched it on this machine.
struct MatchedApp {
    AppRecipe recipe;
    platform::InstalledApp installation;
    bool hasState = false;  ///< at least one of its state directories exists here
};

}  // namespace transmit::core
