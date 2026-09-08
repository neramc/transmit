#pragma once

#include <QDateTime>
#include <QSet>
#include <QString>

#include <optional>

namespace transmit::core {

/// The three kinds of value somebody types rather than picks: a size, a point
/// in time, and a list of file types.
///
/// They lived inside the command line program, where nothing could reach them
/// to ask what they answered - and two of them answered wrongly. "18446744073709551G"
/// came back as a limit of fourteen exabytes because the multiplication wrapped
/// round without a word, and "400000000w" multiplied by seven inside an int,
/// which is undefined behaviour before it is anything else. Both are here now,
/// where the answers can be checked.
///
/// Every one of them refuses rather than guesses. A size that cannot be
/// represented, a date that cannot be reached, a word that is not a number:
/// the caller is told nothing came of it and says so, because a limit somebody
/// did not ask for is worse than an error message.

/// "512", "64K", "3584M", "2G", "8T". Case and surrounding space do not
/// matter. Nothing when it is not a size, including when it is one too large
/// to hold.
[[nodiscard]] std::optional<quint64> sizeFromText(const QString& text);

/// "30d", "6w", "6m", "2y", or an ISO 8601 date. Relative because that is how
/// people think about it - "anything I have touched this year" - and absolute
/// because a script wants a fixed boundary.
///
/// `now` is a parameter so the relative forms can be checked against a fixed
/// answer rather than against the clock.
[[nodiscard]] std::optional<QDateTime> timeFromText(const QString& text, const QDateTime& now);

/// The same, from the clock.
[[nodiscard]] std::optional<QDateTime> timeFromText(const QString& text);

/// The file types in a comma-separated list, lowercase and without their dots,
/// so ".TXT, md" means what it looks like it means.
[[nodiscard]] QSet<QString> extensionsFromText(const QString& text);

}  // namespace transmit::core
