#pragma once

#include <QString>
#include <QStringList>

#include "core/recipe/AppInventoryPayload.h"
#include "platform/PlatformService.h"

namespace transmit::core {

/// What a generated install script would do, before it is written.
struct InstallPlan {
    /// Applications this system's package manager can install, with the
    /// identifier it knows them by.
    QList<QPair<QString, QString>> installable;   ///< display name, package identifier

    /// Applications the catalog knows but this package manager cannot supply.
    QStringList manual;

    [[nodiscard]] bool isEmpty() const { return installable.isEmpty() && manual.isEmpty(); }
};

/// Turns the archive's application list into a script for the target system.
///
/// Transmit never runs this itself. Installing software is the user's decision
/// and their password, and a migration tool that quietly started installing
/// packages would be doing something nobody asked for. The script is written
/// next to the restored files with a header explaining what it is.
class InstallScriptWriter {
public:
    InstallScriptWriter(const platform::PlatformService& platformService);

    [[nodiscard]] InstallPlan plan(const QList<InventoryEntry>& inventory) const;

    /// Writes the script and returns its path, or an empty string when there
    /// was nothing to install. The file is marked executable where that means
    /// something.
    [[nodiscard]] QString write(const InstallPlan& plan, const QString& directory) const;

    /// The file name used for this platform's script.
    [[nodiscard]] QString scriptFileName() const;

private:
    [[nodiscard]] QString buildShellScript(const InstallPlan& plan) const;
    [[nodiscard]] QString buildPowerShellScript(const InstallPlan& plan) const;

    const platform::PlatformService& platform_;
};

}  // namespace transmit::core
