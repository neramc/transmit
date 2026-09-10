#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/rewrite/PathTranslator.h"
#include "core/rewrite/RewritePlan.h"

namespace transmit::core::rewriters {

/// Each rewriter reads one file, produces the edits it would make, and writes
/// the new contents to `<path>.transmit-staged`. Nothing replaces the original
/// until RewritePlan::apply runs, so a plan can always be shown first and
/// abandoned without consequence.
///
/// Every one of them must satisfy the same contract, which the tests check
/// with golden files: change only the values that are genuinely paths, and
/// leave every other byte - including comments, ordering and encoding -
/// exactly as it was.

/// Writes the staged copy the plan will swap in, and says whether all of it
/// landed.
///
/// A staged file is not scratch: RewritePlan::apply renames it over somebody's
/// settings. Every rewriter here used to open it, call write() and let the
/// destructor close it, so a write that stopped early - a full disk, a stick
/// pulled out - left a truncated file that the swap then installed, while the
/// plan reported the change as made. The write is checked and flushed while
/// the file can still say it failed, and a staged file that did not come out
/// whole is removed rather than left where apply() would find it.
[[nodiscard]] bool writeStaged(const QString& stagedPath, const QByteArray& contents);

/// What the next pass over this file should read.
///
/// Two rules can name one file and each answer a different question about it.
/// A Firefox profiles.ini has a Path to correct and an IsRelative to force,
/// and they are separate rules in the recipe; both stage their result to the
/// same "<path>.transmit-staged", so a pass that read the original would write
/// a staged file with its own change and none of the one before it. Whichever
/// ran last would be the only one that landed, while the plan reported both.
///
/// So a pass reads what is staged when there is something staged, and the
/// original otherwise, and the changes compose.
[[nodiscard]] QByteArray readForStaging(const QString& path);

/// Text with a regular expression naming the path in a capture group.
QList<RewriteEdit> rewriteText(const QString& path, const QString& pattern, int captureGroup,
                               const PathTranslator& translator, const QString& appId);

/// JSON, with dotted key paths ("download.default_directory"). A key naming an
/// array or object has every string inside it considered.
QList<RewriteEdit> rewriteJson(const QString& path, const QStringList& keys,
                               const PathTranslator& translator, const QString& appId);

/// INI, with "section/key" names. Comments, blank lines and ordering survive
/// because the file is edited line by line rather than parsed and re-emitted.
QList<RewriteEdit> rewriteIni(const QString& path, const QStringList& keys,
                              const PathTranslator& translator, const QString& appId);

/// Apple property lists, binary or XML. Needed for anything restored onto
/// macOS, where preferences are plists rather than text.
QList<RewriteEdit> rewritePlist(const QString& path, const QStringList& keys,
                                const PathTranslator& translator, const QString& appId);

/// One column of one table in a SQLite database.
QList<RewriteEdit> rewriteSqlite(const QString& path, const QString& table, const QString& column,
                                 const PathTranslator& translator, const QString& appId);

/// Removing a setting, rather than correcting it.
///
/// Some settings describe the machine the capture came from and nothing else -
/// the build the profile last ran, the graphics hardware it was checked
/// against, a key sealed to the old account. Corrected they would be wrong;
/// carried across they are worse than absent, because the application reads
/// them and believes it has already dealt with this profile. So they come out,
/// and the application writes what is true of this machine on first start.
///
/// A name ending in "." means the family beneath it: "gfx.blacklist." removes
/// every setting under that prefix, which is how these are actually written.
QList<RewriteEdit> dropKeysJson(const QString& path, const QStringList& keys, const QString& appId);
QList<RewriteEdit> dropKeysIni(const QString& path, const QStringList& keys, const QString& appId);

/// The line-per-setting shape, which is what a Firefox prefs.js is:
///   user_pref("browser.startup.homepage", "about:home");
QList<RewriteEdit> dropKeysText(const QString& path, const QStringList& keys, const QString& appId);

/// Forcing a setting to a value, which is a different job from correcting a
/// path and is needed in the same file: a Firefox profiles.ini has a Path to
/// correct and an IsRelative to force, and one rule cannot do both. The key is
/// "section/key", and the section may be "*" for every section in the file.
QList<RewriteEdit> assignIni(const QString& path, const QList<QPair<QString, QString>>& assignments,
                             const QString& appId);

}  // namespace transmit::core::rewriters
