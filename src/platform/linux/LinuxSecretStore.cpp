#include "platform/linux/LinuxSecretStore.h"

#include <QProcess>
#include <QStandardPaths>

#include "core/utils/Logging.h"

namespace transmit::platform {
namespace {

/// Runs a command and returns its output. Secrets pass through this, so the
/// output is never logged and the arguments never carry a password - anything
/// sensitive goes in on standard input instead.
QString run(const QString& program, const QStringList& arguments, int timeoutMs = 10000) {
    if (QStandardPaths::findExecutable(program).isEmpty()) {
        return {};
    }

    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(timeoutMs) || process.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

bool runWithSecretOnStdin(const QString& program, const QStringList& arguments,
                          const QString& secret) {
    if (QStandardPaths::findExecutable(program).isEmpty()) {
        return false;
    }

    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        return false;
    }

    // Passing a password as an argument would expose it to every other process
    // on the machine through the process list.
    QByteArray payload = secret.toUtf8();
    process.write(payload);
    process.closeWriteChannel();
    payload.fill('\0');

    return process.waitForFinished(10000) && process.exitCode() == 0;
}

bool haveNetworkManager() {
    return !QStandardPaths::findExecutable(QStringLiteral("nmcli")).isEmpty();
}

bool haveSecretTool() {
    return !QStandardPaths::findExecutable(QStringLiteral("secret-tool")).isEmpty();
}

}  // namespace

bool LinuxSecretStore::isAvailable() const {
    return haveNetworkManager() || haveSecretTool();
}

QString LinuxSecretStore::describe() const {
    QStringList stores;
    if (haveNetworkManager()) {
        stores << QStringLiteral("NetworkManager");
    }
    if (haveSecretTool()) {
        stores << QStringLiteral("the login keyring");
    }
    return stores.isEmpty() ? QStringLiteral("no credential store found")
                            : stores.join(QStringLiteral(" and "));
}

QList<SecretRecord> LinuxSecretStore::read(bool includeWifi, bool includeApplications) const {
    QList<SecretRecord> records;

    if (includeWifi && haveNetworkManager()) {
        const QString connections =
            run(QStringLiteral("nmcli"),
                {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME,TYPE"),
                 QStringLiteral("connection"), QStringLiteral("show")});

        for (const QString& line : connections.split(u'\n', Qt::SkipEmptyParts)) {
            const QStringList columns = line.split(u':');
            if (columns.size() < 2 || !columns.at(1).contains(QLatin1String("wireless"))) {
                continue;
            }
            const QString name = columns.at(0);

            // -s asks NetworkManager to include secrets; without the rights to
            // read them it returns an empty value rather than failing, and the
            // network is then reported as needing the user's attention.
            const QString passphrase =
                run(QStringLiteral("nmcli"),
                    {QStringLiteral("-s"), QStringLiteral("-g"),
                     QStringLiteral("802-11-wireless-security.psk"), QStringLiteral("connection"),
                     QStringLiteral("show"), name});

            SecretRecord record;
            record.kind = SecretKind::WifiNetwork;
            record.service = name;
            record.label = name;
            record.secret = passphrase;
            records.push_back(std::move(record));
        }
    }

    if (includeApplications && haveSecretTool()) {
        // secret-tool can only look a secret up by attribute, never list the
        // keyring, so there is nothing to enumerate here. Application passwords
        // that live in an application's own profile travel with that profile
        // instead; the rest are reported as staying behind.
        qCInfo(logSecrets) << "the login keyring cannot be enumerated from the command line";
    }

    return records;
}

ApplyResult LinuxSecretStore::store(const SecretRecord& record) const {
    switch (record.kind) {
        case SecretKind::WifiNetwork: {
            if (!haveNetworkManager()) {
                return {ApplyOutcome::Unsupported,
                        QStringLiteral("NetworkManager is not installed here"),
                        {}};
            }
            if (record.secret.isEmpty()) {
                return {ApplyOutcome::Failed,
                        QStringLiteral("no passphrase was captured for this network"),
                        {}};
            }

            // Adding a system connection needs rights Transmit does not ask
            // for, so the command is handed to the user with the passphrase
            // left out of the process list.
            return {ApplyOutcome::NeedsPrivilege,
                    QStringLiteral("adding a wireless network needs administrator rights"),
                    QStringLiteral("nmcli device wifi connect %1 --ask").arg(record.service)};
        }

        case SecretKind::ApplicationPassword:
        case SecretKind::NetworkCredential:
        case SecretKind::BrowserLogin: {
            if (!haveSecretTool()) {
                return {ApplyOutcome::Unsupported,
                        QStringLiteral("no keyring tool is installed here"),
                        {}};
            }
            const bool stored =
                runWithSecretOnStdin(QStringLiteral("secret-tool"),
                                     {QStringLiteral("store"), QStringLiteral("--label"),
                                      record.label, QStringLiteral("service"), record.service,
                                      QStringLiteral("account"), record.account},
                                     record.secret);

            return stored ? ApplyResult{ApplyOutcome::Applied, {}, {}}
                          : ApplyResult{ApplyOutcome::Failed,
                                        QStringLiteral("the keyring refused it, or is locked"),
                                        {}};
        }
    }
    return {ApplyOutcome::Unsupported, {}, {}};
}

}  // namespace transmit::platform
