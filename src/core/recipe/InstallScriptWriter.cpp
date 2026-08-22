#include "core/recipe/InstallScriptWriter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include "core/utils/Logging.h"

namespace transmit::core {
namespace {

/// The catalog names install identifiers by package manager, using the same
/// keys PlatformService reports.
QString managerKey(platform::PackageSource source) {
    return platform::packageSourceName(source);
}

/// Managers that are available alongside the native one, in preference order.
/// Flatpak is offered when the native manager has nothing, because it works on
/// every Linux distribution Transmit targets.
QStringList fallbackManagers(platform::PackageSource native) {
    switch (native) {
        case platform::PackageSource::Winget:
        case platform::PackageSource::Homebrew:
            return {};
        default:
            return {QStringLiteral("flatpak"), QStringLiteral("snap")};
    }
}

QString escapeForShell(const QString& text) {
    QString escaped = text;
    escaped.replace(u'\'', QStringLiteral("'\\''"));
    return u'\'' + escaped + u'\'';
}

QString escapeForPowerShell(const QString& text) {
    QString escaped = text;
    escaped.replace(u'\'', QStringLiteral("''"));
    return u'\'' + escaped + u'\'';
}

}  // namespace

InstallScriptWriter::InstallScriptWriter(const platform::PlatformService& platformService)
    : platform_(platformService) {}

QString InstallScriptWriter::scriptFileName() const {
    return platform_.environment().os == OsFamily::Windows ? QStringLiteral("install-apps.ps1")
                                                           : QStringLiteral("install-apps.sh");
}

InstallPlan InstallScriptWriter::plan(const QList<InventoryEntry>& inventory) const {
    InstallPlan result;
    const platform::PackageSource native = platform_.nativePackageSource();
    const QString nativeKey = managerKey(native);
    const QStringList fallbacks = fallbackManagers(native);

    for (const InventoryEntry& entry : inventory) {
        QString identifier = entry.installIds.value(nativeKey);

        if (identifier.isEmpty()) {
            for (const QString& manager : fallbacks) {
                const QString candidate = entry.installIds.value(manager);
                if (!candidate.isEmpty()) {
                    // Prefixed so the script can route it to the right tool.
                    identifier = manager + u':' + candidate;
                    break;
                }
            }
        }

        if (identifier.isEmpty()) {
            result.manual << entry.displayName;
        } else {
            result.installable.append({entry.displayName, identifier});
        }
    }

    std::sort(result.installable.begin(), result.installable.end(),
              [](const auto& a, const auto& b) { return a.first.localeAwareCompare(b.first) < 0; });
    result.manual.sort();
    return result;
}

QString InstallScriptWriter::buildShellScript(const InstallPlan& plan) const {
    const QString nativeCommand = platform_.packageInstallCommand();

    QString script;
    script += QStringLiteral("#!/bin/sh\n");
    script += QStringLiteral("# Reinstalls the programs that were on your previous computer.\n");
    script += QStringLiteral("#\n");
    script += QStringLiteral("# Written by Transmit on %1. Nothing here has been run: read it,\n")
                  .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    script += QStringLiteral("# remove anything you do not want, then run it yourself.\n");
    script += QStringLiteral("#\n");
    script += QStringLiteral("# Your files and settings are already restored. This only installs\n");
    script += QStringLiteral("# the programs themselves, which cannot be copied between systems.\n\n");
    script += QStringLiteral("set -e\n\n");

    if (nativeCommand.isEmpty()) {
        script += QStringLiteral(
            "echo 'Transmit could not work out which package manager this system uses.'\n");
        script += QStringLiteral("echo 'The programs to install are listed below.'\n\n");
    }

    QStringList nativePackages;
    QStringList flatpakPackages;
    QStringList snapPackages;

    for (const auto& [name, identifier] : plan.installable) {
        if (identifier.startsWith(QLatin1String("flatpak:"))) {
            flatpakPackages << identifier.mid(8);
        } else if (identifier.startsWith(QLatin1String("snap:"))) {
            snapPackages << identifier.mid(5);
        } else {
            nativePackages << identifier;
        }
    }

    if (!nativePackages.isEmpty() && !nativeCommand.isEmpty()) {
        script += QStringLiteral("# %1\n").arg(QStringLiteral("From your distribution"));
        script += nativeCommand;
        for (const QString& package : nativePackages) {
            script += u' ' + escapeForShell(package);
        }
        script += QStringLiteral("\n\n");
    } else if (!nativePackages.isEmpty()) {
        script += QStringLiteral("# Install these with your package manager:\n");
        for (const QString& package : nativePackages) {
            script += QStringLiteral("#   %1\n").arg(package);
        }
        script += u'\n';
    }

    if (!flatpakPackages.isEmpty()) {
        script += QStringLiteral("# Not in your distribution's repositories; available as Flatpaks\n");
        script += QStringLiteral("if command -v flatpak >/dev/null 2>&1; then\n");
        script += QStringLiteral("    flatpak install -y flathub");
        for (const QString& package : flatpakPackages) {
            script += u' ' + escapeForShell(package);
        }
        script += QStringLiteral("\nelse\n");
        script += QStringLiteral("    echo 'Install flatpak first, or fetch these yourself:%1'\n")
                      .arg(flatpakPackages.join(QStringLiteral(", ")));
        script += QStringLiteral("fi\n\n");
    }

    if (!snapPackages.isEmpty()) {
        script += QStringLiteral("if command -v snap >/dev/null 2>&1; then\n");
        for (const QString& package : snapPackages) {
            script += QStringLiteral("    sudo snap install %1\n").arg(escapeForShell(package));
        }
        script += QStringLiteral("fi\n\n");
    }

    if (!plan.manual.isEmpty()) {
        script += QStringLiteral("# These were on your old computer but no package manager here\n");
        script += QStringLiteral("# offers them. Their settings have already been restored, so\n");
        script += QStringLiteral("# installing them by hand should pick up where you left off.\n");
        for (const QString& name : plan.manual) {
            script += QStringLiteral("#   %1\n").arg(name);
        }
        script += u'\n';
    }

    script += QStringLiteral("echo 'Done.'\n");
    return script;
}

QString InstallScriptWriter::buildPowerShellScript(const InstallPlan& plan) const {
    QString script;
    script += QStringLiteral("# Reinstalls the programs that were on your previous computer.\n");
    script += QStringLiteral("#\n");
    script += QStringLiteral("# Written by Transmit on %1. Nothing here has been run: read it,\n")
                  .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    script += QStringLiteral("# remove anything you do not want, then run it yourself.\n");
    script += QStringLiteral("#\n");
    script += QStringLiteral("# Your files and settings are already restored. This only installs\n");
    script += QStringLiteral("# the programs themselves, which cannot be copied between systems.\n\n");
    script += QStringLiteral("$ErrorActionPreference = 'Continue'\n\n");

    if (!plan.installable.isEmpty()) {
        script += QStringLiteral("$packages = @(\n");
        for (const auto& [name, identifier] : plan.installable) {
            QString bare = identifier;
            const qsizetype colon = bare.indexOf(u':');
            if (colon > 0) {
                bare = bare.mid(colon + 1);
            }
            script += QStringLiteral("    @{ Name = %1; Id = %2 }\n")
                          .arg(escapeForPowerShell(name), escapeForPowerShell(bare));
        }
        script += QStringLiteral(")\n\n");
        script += QStringLiteral("foreach ($package in $packages) {\n");
        script += QStringLiteral("    Write-Host \"Installing $($package.Name)...\"\n");
        script += QStringLiteral(
            "    winget install --exact --id $package.Id --accept-package-agreements "
            "--accept-source-agreements\n");
        script += QStringLiteral("}\n\n");
    }

    if (!plan.manual.isEmpty()) {
        script += QStringLiteral("Write-Host ''\n");
        script += QStringLiteral(
            "Write-Host 'These were on your old computer but winget does not offer them.'\n");
        script += QStringLiteral(
            "Write-Host 'Their settings are already restored, so installing them by hand'\n");
        script += QStringLiteral("Write-Host 'should pick up where you left off:'\n");
        for (const QString& name : plan.manual) {
            script += QStringLiteral("Write-Host '  %1'\n").arg(name);
        }
    }
    return script;
}

QString InstallScriptWriter::write(const InstallPlan& plan, const QString& directory) const {
    if (plan.isEmpty()) {
        return {};
    }

    QDir().mkpath(directory);
    const QString path = QDir(directory).filePath(scriptFileName());

    const bool windows = platform_.environment().os == OsFamily::Windows;
    const QString content = windows ? buildPowerShellScript(plan) : buildShellScript(plan);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(logRecipe) << "could not write the install script" << path << file.errorString();
        return {};
    }
    file.write(content.toUtf8());
    if (!file.commit()) {
        return {};
    }

    if (!windows) {
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                        QFile::ReadGroup | QFile::ExeGroup);
    }
    qCInfo(logRecipe) << "wrote an install script for" << plan.installable.size() << "programs";
    return path;
}

}  // namespace transmit::core
