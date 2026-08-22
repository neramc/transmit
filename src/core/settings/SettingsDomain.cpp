#include "core/settings/SettingsDomain.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "format/Serialization.h"

namespace transmit::core {
namespace {

using format::ByteReader;
using format::ByteWriter;

namespace setting_field {
constexpr std::uint32_t kKey = 1;
constexpr std::uint32_t kValue = 2;
constexpr std::uint32_t kSourceEnvironment = 3;
}  // namespace setting_field

QString privilegedScriptName(OsFamily os) {
    return os == OsFamily::Windows ? QStringLiteral("apply-settings.ps1")
                                   : QStringLiteral("apply-settings.sh");
}

/// Writes the commands a system would not let Transmit run itself.
QString writePrivilegedScript(const QStringList& commands, const QString& directory, OsFamily os) {
    if (commands.isEmpty() || directory.isEmpty()) {
        return {};
    }

    QDir().mkpath(directory);
    const QString path = QDir(directory).filePath(privilegedScriptName(os));
    const bool windows = os == OsFamily::Windows;

    QString content;
    if (!windows) {
        content += QStringLiteral("#!/bin/sh\n");
    }
    content += QStringLiteral("# Settings from your old computer that this system will not let\n");
    content += QStringLiteral("# a program change on your behalf. Read this, then run it.\n");
    content += QStringLiteral("#\n");
    content +=
        QStringLiteral("# Some of these need an administrator password. Transmit does not\n");
    content +=
        QStringLiteral("# ask for one, which is why they are here rather than already done.\n\n");
    for (const QString& command : commands) {
        content += command + u'\n';
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {};
    }
    file.write(content.toUtf8());
    if (!file.commit()) {
        return {};
    }
    if (!windows) {
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    return path;
}

}  // namespace

SettingsDomain::SettingsDomain(const platform::PlatformService& platformService)
    : platform_(platformService) {}

QList<CapturedSetting> SettingsDomain::capture() const {
    const auto provider = platform_.settingsProvider();
    if (!provider) {
        return {};
    }

    const QString environment = provider->describeEnvironment();
    QList<CapturedSetting> captured;

    for (const SettingValue& value : provider->readAll()) {
        if (!value.present || value.value.isEmpty()) {
            continue;  // nothing to carry, and an empty value is not a setting
        }
        captured.push_back(CapturedSetting{value.key, value.value, environment});
    }

    qCInfo(logSettings) << "captured" << captured.size() << "settings from" << environment;
    return captured;
}

QString SettingsDomain::wallpaperPath(const QList<CapturedSetting>& settings) {
    for (const CapturedSetting& setting : settings) {
        if (setting.key == SettingKey::DesktopWallpaper) {
            return setting.value;
        }
    }
    return {};
}

QList<ContinuityNote> SettingsDomain::restore(const QList<CapturedSetting>& settings,
                                              const QString& scriptDirectory, bool dryRun) const {
    QList<ContinuityNote> notes;
    const auto provider = platform_.settingsProvider();
    if (!provider || settings.isEmpty()) {
        return notes;
    }

    QStringList privilegedCommands;
    int applied = 0;
    int approximated = 0;

    for (const CapturedSetting& setting : settings) {
        const SettingValue value{setting.key, setting.value, true};
        const QString name = platform::settingKeyDescription(setting.key);

        // A dry run must not change a single preference, so the outcome is
        // described from the value rather than by trying it.
        const platform::ApplyResult result =
            dryRun ? platform::ApplyResult{ApplyOutcome::Applied, {}, {}} : provider->apply(value);

        switch (result.outcome) {
            case ApplyOutcome::Applied:
                ++applied;
                break;

            case ApplyOutcome::Approximated:
                ++approximated;
                notes.push_back(ContinuityNote{
                    ContinuityGrade::Adapted, DomainId::SystemSettings, name,
                    QCoreApplication::translate("Settings",
                                                "Set to the closest match this system offers (%1).")
                        .arg(result.detail)});
                break;

            case ApplyOutcome::NeedsPrivilege:
                if (!result.privilegedCommand.isEmpty()) {
                    privilegedCommands << result.privilegedCommand;
                }
                notes.push_back(ContinuityNote{
                    ContinuityGrade::Manual, DomainId::SystemSettings, name,
                    QCoreApplication::translate("Settings", "%1. It was \"%2\" before.")
                        .arg(result.detail.isEmpty()
                                 ? QCoreApplication::translate("Settings",
                                                               "This needs to be changed by you")
                                 : result.detail,
                             setting.value)});
                break;

            case ApplyOutcome::Unsupported:
                notes.push_back(ContinuityNote{
                    ContinuityGrade::Impossible, DomainId::SystemSettings, name,
                    QCoreApplication::translate(
                        "Settings", "This system has no equivalent. It was \"%1\" on %2.")
                        .arg(setting.value, setting.sourceEnvironment)});
                break;

            case ApplyOutcome::Failed:
                notes.push_back(ContinuityNote{
                    ContinuityGrade::Manual, DomainId::SystemSettings, name,
                    QCoreApplication::translate("Settings", "Could not be set: %1. It was \"%2\".")
                        .arg(result.detail, setting.value)});
                break;
        }
    }

    if (applied > 0) {
        notes.push_back(ContinuityNote{
            ContinuityGrade::Full, DomainId::SystemSettings,
            QCoreApplication::translate("Settings", "Preferences"),
            dryRun ? QCoreApplication::translate(
                         "Settings", "%n setting(s) would be carried across as they were.", nullptr,
                         applied)
                   : QCoreApplication::translate("Settings",
                                                 "%n setting(s) were carried across as they were.",
                                                 nullptr, applied)});
    }

    if (!privilegedCommands.isEmpty() && !dryRun) {
        const QString path =
            writePrivilegedScript(privilegedCommands, scriptDirectory, platform_.environment().os);
        if (!path.isEmpty()) {
            notes.push_back(ContinuityNote{
                ContinuityGrade::Manual, DomainId::SystemSettings,
                QCoreApplication::translate("Settings", "Settings that need your permission"),
                QCoreApplication::translate(
                    "Settings",
                    "%n of them need rights Transmit does not ask for. The commands that would "
                    "make the changes are in \"%1\" - read it before running it.",
                    nullptr, static_cast<int>(privilegedCommands.size()))
                    .arg(path)});
        }
    }

    qCInfo(logSettings) << "applied" << applied << "settings," << approximated << "approximated,"
                        << privilegedCommands.size() << "left for the user";
    return notes;
}

format::ByteBuffer SettingsDomain::encode(const QList<CapturedSetting>& settings) {
    format::ByteBuffer buffer;
    ByteWriter writer(buffer);

    for (const CapturedSetting& setting : settings) {
        writer.putRecord(1, [&](ByteWriter& nested) {
            nested.putUInt(setting_field::kKey, static_cast<std::uint64_t>(setting.key));
            nested.putString(setting_field::kValue, toUtf8(setting.value));
            nested.putString(setting_field::kSourceEnvironment, toUtf8(setting.sourceEnvironment));
        });
    }
    return buffer;
}

QList<CapturedSetting> SettingsDomain::decode(format::ByteView data) {
    QList<CapturedSetting> settings;
    ByteReader reader(data);

    const QList<SettingKey> known = platform::allSettingKeys();

    while (!reader.atEnd()) {
        const auto tag = reader.getTag();
        if (!tag) {
            break;
        }
        if (tag->field != 1) {
            if (!reader.skip(tag->type)) {
                break;
            }
            continue;
        }

        const auto payload = reader.getBytes();
        if (!payload) {
            break;
        }

        CapturedSetting setting;
        bool haveKey = false;
        ByteReader nested(*payload);

        while (!nested.atEnd()) {
            const auto nestedTag = nested.getTag();
            if (!nestedTag) {
                break;
            }
            if (nestedTag->field == setting_field::kKey) {
                const auto value = nested.getVarint();
                if (!value) {
                    break;
                }
                // An archive from a newer Transmit may name a setting this
                // build has never heard of; skip it rather than guessing.
                const auto candidate = static_cast<SettingKey>(*value);
                if (known.contains(candidate)) {
                    setting.key = candidate;
                    haveKey = true;
                }
            } else if (nestedTag->field == setting_field::kValue) {
                if (const auto value = nested.getString()) {
                    setting.value = fromUtf8(*value);
                }
            } else if (nestedTag->field == setting_field::kSourceEnvironment) {
                if (const auto value = nested.getString()) {
                    setting.sourceEnvironment = fromUtf8(*value);
                }
            } else if (!nested.skip(nestedTag->type)) {
                break;
            }
        }

        if (haveKey && !setting.value.isEmpty()) {
            settings.push_back(std::move(setting));
        }
    }
    return settings;
}

}  // namespace transmit::core
