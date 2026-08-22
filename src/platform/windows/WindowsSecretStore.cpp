#include "platform/windows/WindowsSecretStore.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include "core/utils/Logging.h"

#ifdef Q_OS_WIN
#include <windows.h>

#include <wincred.h>
#endif

namespace transmit::platform {
namespace {

QString run(const QString& program, const QStringList& arguments, int timeoutMs = 15000) {
    if (QStandardPaths::findExecutable(program).isEmpty()) {
        return {};
    }
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(timeoutMs) || process.exitCode() != 0) {
        return {};
    }
    return QString::fromLocal8Bit(process.readAllStandardOutput());
}

}  // namespace

bool WindowsSecretStore::isAvailable() const {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

QString WindowsSecretStore::describe() const {
    return QStringLiteral("Windows Credential Manager");
}

QList<SecretRecord> WindowsSecretStore::read(bool includeWifi, bool includeApplications) const {
    QList<SecretRecord> records;

#ifdef Q_OS_WIN
    if (includeApplications) {
        DWORD count = 0;
        PCREDENTIALW* credentials = nullptr;

        if (CredEnumerateW(nullptr, CRED_ENUMERATE_ALL_CREDENTIALS, &count, &credentials) !=
            FALSE) {
            for (DWORD i = 0; i < count; ++i) {
                const CREDENTIALW* entry = credentials[i];
                if (entry == nullptr || entry->CredentialBlobSize == 0) {
                    continue;
                }
                // Only generic credentials carry something re-storable; domain
                // ones are tickets bound to this machine's session.
                if (entry->Type != CRED_TYPE_GENERIC) {
                    continue;
                }

                SecretRecord record;
                record.kind = SecretKind::NetworkCredential;
                record.service = QString::fromWCharArray(entry->TargetName);
                record.label = entry->Comment != nullptr ? QString::fromWCharArray(entry->Comment)
                                                         : record.service;
                if (entry->UserName != nullptr) {
                    record.account = QString::fromWCharArray(entry->UserName);
                }
                // The blob is UTF-16 for anything a person typed.
                record.secret = QString::fromUtf16(
                    reinterpret_cast<const char16_t*>(entry->CredentialBlob),
                    static_cast<qsizetype>(entry->CredentialBlobSize / sizeof(char16_t)));
                records.push_back(std::move(record));
            }
            CredFree(credentials);
        }
    }
#else
    Q_UNUSED(includeApplications);
#endif

    if (includeWifi) {
        const QString profiles =
            run(QStringLiteral("netsh"),
                {QStringLiteral("wlan"), QStringLiteral("show"), QStringLiteral("profiles")});

        static const QRegularExpression namePattern(
            QStringLiteral("All User Profile\\s*:\\s*(.+)"));
        auto matches = namePattern.globalMatch(profiles);

        while (matches.hasNext()) {
            const QString name = matches.next().captured(1).trimmed();

            // key=clear only produces the passphrase for an administrator;
            // otherwise the field is simply absent and the network is reported
            // as needing the user's attention.
            const QString detail =
                run(QStringLiteral("netsh"),
                    {QStringLiteral("wlan"), QStringLiteral("show"), QStringLiteral("profile"),
                     QStringLiteral("name=%1").arg(name), QStringLiteral("key=clear")});

            static const QRegularExpression keyPattern(QStringLiteral("Key Content\\s*:\\s*(.+)"));
            const auto key = keyPattern.match(detail);

            SecretRecord record;
            record.kind = SecretKind::WifiNetwork;
            record.service = name;
            record.label = name;
            record.secret = key.hasMatch() ? key.captured(1).trimmed() : QString();
            records.push_back(std::move(record));
        }
    }

    return records;
}

ApplyResult WindowsSecretStore::store(const SecretRecord& record) const {
    if (record.kind == SecretKind::WifiNetwork) {
        return {ApplyOutcome::NeedsPrivilege,
                QStringLiteral("adding a wireless profile needs administrator rights"),
                QStringLiteral("netsh wlan connect name=\"%1\"").arg(record.service)};
    }

#ifdef Q_OS_WIN
    if (record.secret.isEmpty()) {
        return {ApplyOutcome::Failed, QStringLiteral("nothing was captured for this entry"), {}};
    }

    std::wstring target = record.service.toStdWString();
    std::wstring user = record.account.toStdWString();
    std::wstring comment = record.label.toStdWString();
    std::wstring secret = record.secret.toStdWString();

    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.Comment = comment.data();
    credential.UserName = user.empty() ? nullptr : user.data();
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(secret.data());
    credential.CredentialBlobSize = static_cast<DWORD>(secret.size() * sizeof(wchar_t));

    const bool written = CredWriteW(&credential, 0) != FALSE;

    // Windows has taken its own copy; ours should not linger.
    SecureZeroMemory(secret.data(), secret.size() * sizeof(wchar_t));

    return written ? ApplyResult{ApplyOutcome::Applied, {}, {}}
                   : ApplyResult{ApplyOutcome::Failed,
                                 QStringLiteral("Credential Manager refused the entry"),
                                 {}};
#else
    return {ApplyOutcome::Unsupported, {}, {}};
#endif
}

}  // namespace transmit::platform
