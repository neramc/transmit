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

}  // namespace transmit::core::rewriters
