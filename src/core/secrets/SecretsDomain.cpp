#include "core/secrets/SecretsDomain.h"

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

namespace secret_field {
constexpr std::uint32_t kKind = 1;
constexpr std::uint32_t kService = 2;
constexpr std::uint32_t kAccount = 3;
constexpr std::uint32_t kSecret = 4;
constexpr std::uint32_t kLabel = 5;
}  // namespace secret_field

format::ByteBuffer encode(QList<SecretRecord>& records) {
    format::ByteBuffer buffer;
    ByteWriter writer(buffer);

    for (SecretRecord& record : records) {
        writer.putRecord(1, [&](ByteWriter& nested) {
            nested.putUInt(secret_field::kKind, static_cast<std::uint64_t>(record.kind));
            nested.putString(secret_field::kService, toUtf8(record.service));
            nested.putString(secret_field::kAccount, toUtf8(record.account));
            nested.putString(secret_field::kSecret, toUtf8(record.secret));
            nested.putString(secret_field::kLabel, toUtf8(record.label));
        });
        // The archive has it now; this copy should not outlive the loop.
        record.clear();
    }
    return buffer;
}

QList<SecretRecord> decode(format::ByteView data) {
    QList<SecretRecord> records;
    ByteReader reader(data);

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

        SecretRecord record;
        ByteReader nested(*payload);
        while (!nested.atEnd()) {
            const auto nestedTag = nested.getTag();
            if (!nestedTag) {
                break;
            }
            switch (nestedTag->field) {
                case secret_field::kKind:
                    if (const auto value = nested.getVarint()) {
                        record.kind = static_cast<SecretKind>(*value);
                    }
                    break;
                case secret_field::kService:
                    if (const auto value = nested.getString()) {
                        record.service = fromUtf8(*value);
                    }
                    break;
                case secret_field::kAccount:
                    if (const auto value = nested.getString()) {
                        record.account = fromUtf8(*value);
                    }
                    break;
                case secret_field::kSecret:
                    if (const auto value = nested.getString()) {
                        record.secret = fromUtf8(*value);
                    }
                    break;
                case secret_field::kLabel:
                    if (const auto value = nested.getString()) {
                        record.label = fromUtf8(*value);
                    }
                    break;
                default:
                    if (!nested.skip(nestedTag->type)) {
                        return records;
                    }
                    break;
            }
        }

        if (!record.service.isEmpty()) {
            records.push_back(std::move(record));
        }
    }
    return records;
}

/// Writes the commands for credentials this system will not let a program add.
/// The passwords themselves are never written into it: the commands prompt.
QString writePrivilegedScript(const QStringList& commands, const QString& directory, OsFamily os) {
    if (commands.isEmpty() || directory.isEmpty()) {
        return {};
    }

    QDir().mkpath(directory);
    const bool windows = os == OsFamily::Windows;
    const QString path = QDir(directory).filePath(windows ? QStringLiteral("connect-networks.ps1")
                                                          : QStringLiteral("connect-networks.sh"));

    QString content;
    if (!windows) {
        content += QStringLiteral("#!/bin/sh\n");
    }
    content += QStringLiteral("# Networks and logins from your old computer that this system\n");
    content += QStringLiteral("# will not let a program add on your behalf.\n");
    content += QStringLiteral("#\n");
    content += QStringLiteral("# These commands ask for the password when you run them. The\n");
    content += QStringLiteral("# passwords are NOT written in this file.\n\n");
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

SecretsDomain::SecretsDomain(const platform::PlatformService& platformService)
    : platform_(platformService) {}

bool SecretsDomain::isAvailable() const {
    const auto store = platform_.secretStore();
    return store && store->isAvailable();
}

QString SecretsDomain::describeStore() const {
    const auto store = platform_.secretStore();
    return store ? store->describe() : QString();
}

SecretsDomain::CaptureResult SecretsDomain::capture(const CaptureOptions& options) const {
    CaptureResult result;

    const auto store = platform_.secretStore();
    if (!store || !store->isAvailable()) {
        result.notes.push_back(ContinuityNote{
            ContinuityGrade::Impossible, DomainId::Secrets,
            QCoreApplication::translate("Secrets", "Saved passwords"),
            QCoreApplication::translate("Secrets",
                                        "This system has no credential store Transmit can read.")});
        return result;
    }

    QList<SecretRecord> records = store->read(options.includeWifi, options.includeApplications);
    QStringList unreadable;

    // A record with no value is one the system found but refused to reveal,
    // which is worth reporting rather than dropping.
    for (auto it = records.begin(); it != records.end();) {
        if (it->secret.isEmpty()) {
            unreadable << it->label;
            it = records.erase(it);
        } else {
            ++it;
        }
    }

    result.captured = static_cast<int>(records.size());
    result.unreadable = static_cast<int>(unreadable.size());
    result.payload = encode(records);
    records.clear();

    if (result.captured > 0) {
        result.notes.push_back(ContinuityNote{
            ContinuityGrade::Full, DomainId::Secrets,
            QCoreApplication::translate("Secrets", "Saved passwords"),
            QCoreApplication::translate(
                "Secrets",
                "%n password(s) were taken from %1. This archive now contains them. Anyone with "
                "the file and the passphrase can read them, so keep the drive and the passphrase "
                "apart.",
                nullptr, result.captured)
                .arg(store->describe())});
    }

    if (!unreadable.isEmpty()) {
        result.notes.push_back(ContinuityNote{
            ContinuityGrade::Manual, DomainId::Secrets,
            QCoreApplication::translate("Secrets", "Passwords this computer would not reveal"),
            QCoreApplication::translate(
                "Secrets",
                "These were found but not readable without administrator rights, so you will "
                "need to enter them again: %1")
                .arg(unreadable.join(QStringLiteral(", ")))});
    }

    qCInfo(logSecrets) << "captured" << result.captured << "credentials," << result.unreadable
                       << "unreadable";
    return result;
}

QList<ContinuityNote> SecretsDomain::restore(format::ByteView payload,
                                             const QString& scriptDirectory, bool dryRun) const {
    QList<ContinuityNote> notes;

    const auto store = platform_.secretStore();
    QList<SecretRecord> records = decode(payload);
    if (records.isEmpty()) {
        return notes;
    }

    if (!store || !store->isAvailable()) {
        notes.push_back(ContinuityNote{
            ContinuityGrade::Manual, DomainId::Secrets,
            QCoreApplication::translate("Secrets", "Saved passwords"),
            QCoreApplication::translate(
                "Secrets",
                "%n password(s) travelled with this archive, but this system has no credential "
                "store Transmit can write to. You will need to enter them again.",
                nullptr, static_cast<int>(records.size()))});
        for (SecretRecord& record : records) {
            record.clear();
        }
        return notes;
    }

    if (dryRun) {
        notes.push_back(ContinuityNote{
            ContinuityGrade::Adapted, DomainId::Secrets,
            QCoreApplication::translate("Secrets", "Saved passwords"),
            QCoreApplication::translate("Secrets", "%n password(s) would be added to %1.", nullptr,
                                        static_cast<int>(records.size()))
                .arg(store->describe())});
        for (SecretRecord& record : records) {
            record.clear();
        }
        return notes;
    }

    int applied = 0;
    QStringList failed;
    QStringList privilegedCommands;
    QStringList needingPermission;

    for (SecretRecord& record : records) {
        const platform::ApplyResult result = store->store(record);
        switch (result.outcome) {
            case platform::ApplyOutcome::Applied:
            case platform::ApplyOutcome::Approximated:
                ++applied;
                break;
            case platform::ApplyOutcome::NeedsPrivilege:
                needingPermission << record.label;
                if (!result.privilegedCommand.isEmpty()) {
                    privilegedCommands << result.privilegedCommand;
                }
                break;
            case platform::ApplyOutcome::Unsupported:
            case platform::ApplyOutcome::Failed:
                failed << record.label;
                break;
        }
        record.clear();
    }
    records.clear();

    if (applied > 0) {
        notes.push_back(
            ContinuityNote{ContinuityGrade::Full, DomainId::Secrets,
                           QCoreApplication::translate("Secrets", "Saved passwords"),
                           QCoreApplication::translate(
                               "Secrets", "%n password(s) were added to %1.", nullptr, applied)
                               .arg(store->describe())});
    }

    if (!needingPermission.isEmpty()) {
        const QString path =
            writePrivilegedScript(privilegedCommands, scriptDirectory, platform_.environment().os);
        notes.push_back(ContinuityNote{
            ContinuityGrade::Manual, DomainId::Secrets,
            QCoreApplication::translate("Secrets", "Networks you need to join yourself"),
            path.isEmpty()
                ? QCoreApplication::translate("Secrets",
                                              "These need administrator rights to add: %1")
                      .arg(needingPermission.join(QStringLiteral(", ")))
                : QCoreApplication::translate(
                      "Secrets",
                      "These need administrator rights to add. The commands are in \"%1\"; they "
                      "ask for the password when you run them, and it is not written in the file.")
                      .arg(path)});
    }

    if (!failed.isEmpty()) {
        notes.push_back(ContinuityNote{
            ContinuityGrade::Manual, DomainId::Secrets,
            QCoreApplication::translate("Secrets", "Passwords that could not be stored"),
            QCoreApplication::translate("Secrets", "You will need to enter these again: %1")
                .arg(failed.join(QStringLiteral(", ")))});
    }

    return notes;
}

}  // namespace transmit::core
