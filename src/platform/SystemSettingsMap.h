#pragma once

#include <QList>
#include <QString>

#include "SettingsProvider.h"

namespace transmit::platform {

/// Where each operating system keeps the settings that belong to the machine
/// rather than to the person using it: its name, the servers it trusts for the
/// time, whether its firewall is on.
///
/// The knowledge is a table - resources/system-map.json - rather than three
/// implementations of the same list, for a reason that has nothing to do with
/// tidiness: two of the three systems cannot be run by whoever is changing
/// this. A registry path written into Windows code is a line nobody here can
/// execute until it is in front of a user; the same path in a table is
/// something a test can check the shape of, and something a person can read
/// and correct without a Windows machine.
///
/// The table is read from the compiled-in resource and from nowhere else.
/// Every entry names a command, and a file that names commands and can be
/// replaced by anything able to write to a home directory is a way to run
/// whatever it likes - as that user for the reads, and as root for the applies.
class SystemSettingsMap {
public:
    /// The keys the table describes, in the order it lists them.
    [[nodiscard]] static QList<SettingKey> keys();

    /// Whether this key is one of the machine's rather than the user's.
    [[nodiscard]] static bool isSystemSetting(SettingKey key);

    /// Reads the setting on this machine. Absent when the table has no entry
    /// for this system, or when what it names is not there.
    [[nodiscard]] static SettingValue read(SettingKey key);

    /// What somebody with rights would run to set it. Empty when this system
    /// has no way to.
    ///
    /// Never run. Every one of these needs privileges Transmit does not ask
    /// for, so a restore writes them into the script it leaves behind and
    /// stops - which is the same rule the rest of the program follows for
    /// installing software.
    [[nodiscard]] static QString applyCommand(SettingKey key, const QString& value);

    /// Reads a table from somewhere else, for tests. Passing an empty path
    /// puts the built-in one back.
    static void useTableForTesting(const QString& path);

    /// Where the reads look, so a test can point them at a fixture instead of
    /// at the machine running the test. Empty means the real paths.
    static void useRootForTesting(const QString& path);
};

}  // namespace transmit::platform
