#pragma once

#include <QHash>
#include <QString>

#include <optional>

#include "core/continuity/ContinuityTypes.h"
#include "core/recipe/StateRelocator.h"
#include "format/Manifest.h"
#include "format/NameSanitizer.h"
#include "format/PathToken.h"

namespace transmit::core {

/// Turns a path written on the source machine into the equivalent path here.
///
/// Configuration files are full of absolute paths - a download directory, a
/// profile location, a bookmarked folder - and restoring them verbatim leaves
/// an application pointing at a directory that does not exist on this
/// operating system. This class is the single place that conversion happens,
/// so the rules are stated once and tested once.
///
/// It handles four separate problems at the same time:
///   - the known folder has moved (%APPDATA% is not ~/.config)
///   - the separator has changed (backslash to slash, and back)
///   - the drive letter has gone away (or needs inventing)
///   - the file was renamed on arrival, because its name was not legal here
class PathTranslator {
public:
    PathTranslator(const format::SourceEnvironment& source, format::PathTokenMap targetFolders,
                   OsFamily targetOs);

    /// Records what the restore renamed, so a reference to the old name inside
    /// a configuration file is repointed at the file that actually landed.
    void setRenames(const QList<QPair<QString, QString>>& renames);

    /// Teaches the translator about application state that moved to a
    /// different known folder on this system. Without this, a profile that was
    /// relocated would still be described by its old address in the very files
    /// that have to find it.
    void setRelocator(const StateRelocator* relocator) noexcept { relocator_ = relocator; }

    /// Translates one path. Returns nothing when the text does not look like a
    /// path from the source machine, which is the common case and must be left
    /// strictly alone.
    [[nodiscard]] std::optional<QString> translate(const QString& sourcePath) const;

    /// Translates and falls back to the input, for callers that always want a
    /// string.
    [[nodiscard]] QString translateOr(const QString& sourcePath) const;

    /// True when the text is recognisably an absolute path from the source
    /// machine. Used to decide whether a value is worth looking at.
    [[nodiscard]] bool looksLikeSourcePath(const QString& text) const;

    /// Rewrites every recognisable path inside a longer string, leaving the
    /// rest untouched. Used for values that embed a path in a URI or a list.
    [[nodiscard]] QString translateWithin(const QString& text, int* replacements = nullptr) const;

    [[nodiscard]] OsFamily sourceOs() const noexcept { return sourceOs_; }
    [[nodiscard]] OsFamily targetOs() const noexcept { return targetOs_; }

private:
    [[nodiscard]] QString applyRenames(format::PathTokenId token, const QString& relative) const;

    OsFamily sourceOs_ = OsFamily::Unknown;
    OsFamily targetOs_ = OsFamily::Unknown;
    format::PathTokenMap sourceFolders_;
    format::PathTokenMap targetFolders_;
    QHash<QString, QString> renames_;  ///< original token path -> applied token path
    const StateRelocator* relocator_ = nullptr;
};

}  // namespace transmit::core
