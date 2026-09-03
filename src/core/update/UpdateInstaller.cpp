#include "core/update/UpdateInstaller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include "core/utils/Logging.h"
#include "format/hash/Blake2b.h"

namespace transmit::core {
namespace {

/// The first bytes of a program of a given kind, so a staged file that is not
/// one can be refused before it is put where a working program used to be.
///
/// An AppImage is an ELF with a filesystem bolted onto the end; every Windows
/// executable still starts with the two letters Mark Zbikowski put there in
/// 1983. Nothing else here is started as a program, so nothing else has bytes
/// to name.
constexpr char kElfMagic[] =
    "\x7f"
    "ELF";
constexpr char kDosMagic[] = "MZ";

/// Whether the staged file begins the way the published one would.
///
/// The caller says what the file is supposed to be; this does not ask the
/// machine. The version before it did, and its macOS answer was
/// `head.size() == 4` - true of every file with four bytes in it - so on that
/// platform the guard was not a guard. What has to be refused is a staged
/// file that is not the published program, and that does not change with the
/// host doing the asking.
bool startsWith(const QString& path, const char* magic) {
    const auto length = static_cast<qsizetype>(qstrlen(magic));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    return file.read(length) == QByteArray(magic, length);
}

bool makeExecutable(const QString& path) {
#if defined(Q_OS_WIN)
    Q_UNUSED(path);
    return true;
#else
    return QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                           QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther |
                                           QFile::ExeOther);
#endif
}

/// BLAKE2b-256 of a file on disk, read back rather than remembered.
QByteArray digestOf(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    format::Blake2b digest(32);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1 << 20);
        if (chunk.isEmpty()) {
            break;
        }
        digest.update({reinterpret_cast<const format::Byte*>(chunk.constData()),
                       static_cast<std::size_t>(chunk.size())});
    }
    const auto computed = digest.finish256();
    return {reinterpret_cast<const char*>(computed.data()),
            static_cast<qsizetype>(computed.size())};
}

InstallOutcome refuse(const QString& problem) {
    InstallOutcome outcome;
    outcome.problem = problem;
    return outcome;
}

/// Replaces one file with another, in the same directory, keeping the old one.
///
/// Two renames rather than a copy over the top: a rename within a filesystem is
/// atomic, so at no point is there a half-written program on disk. If the
/// second fails the first is undone, which leaves exactly what was there
/// before rather than nothing at all.
InstallOutcome replaceFile(const QString& staged, const QString& target) {
    const QString previous = target + QStringLiteral(".previous");

    QFile::remove(previous);

    const QString incoming = target + QStringLiteral(".incoming");
    QFile::remove(incoming);
    if (!QFile::copy(staged, incoming)) {
        return refuse(
            QStringLiteral("could not put the new version beside the old one at %1").arg(incoming));
    }
    if (!makeExecutable(incoming)) {
        QFile::remove(incoming);
        return refuse(QStringLiteral("the new version could not be made runnable"));
    }

    if (!QFile::rename(target, previous)) {
        QFile::remove(incoming);
        return refuse(QStringLiteral("could not move the running version aside - %1 may be "
                                     "read-only or owned by somebody else")
                          .arg(target));
    }
    if (!QFile::rename(incoming, target)) {
        // Put back exactly what was there. This is the one failure that would
        // otherwise leave the machine with no program at all.
        const bool restored = QFile::rename(previous, target);
        QFile::remove(incoming);
        return refuse(restored
                          ? QStringLiteral("the new version could not be moved into place, and "
                                           "the old one has been put back")
                          : QStringLiteral("the new version could not be moved into place and "
                                           "the old one could not be put back - it is at %1")
                                .arg(previous));
    }

    InstallOutcome outcome;
    outcome.applied = true;
    outcome.needsRestart = true;
    outcome.previous = previous;
    return outcome;
}

}  // namespace

InstallOutcome UpdateInstaller::apply(const QString& staged, const QString& target,
                                      InstallKind kind, const QByteArray& expected) {
    const QFileInfo stagedInfo(staged);
    if (!stagedInfo.isFile() || stagedInfo.size() == 0) {
        return refuse(QStringLiteral("there is no staged update at %1").arg(staged));
    }
    if (!expected.isEmpty() && digestOf(staged) != expected) {
        // Between the download being checked and this moment the file has been
        // sitting in a directory anything running as this user could write to.
        // A file that has changed since is not the one that was published,
        // whatever it was when it arrived.
        QFile::remove(staged);
        return refuse(
            QStringLiteral("the staged update is not the file that was downloaded, "
                           "so it has been thrown away rather than installed"));
    }
    if (!canReplaceItself(kind)) {
        return refuse(
            QStringLiteral("this is %1, which is not updated this way").arg(describe(kind)));
    }
    if (target.isEmpty()) {
        return refuse(QStringLiteral("there is nothing to replace"));
    }
    if (QFileInfo(target).canonicalFilePath() == stagedInfo.canonicalFilePath()) {
        return refuse(QStringLiteral("the staged update is the running program"));
    }

    switch (kind) {
        case InstallKind::AppImage: {
            if (!startsWith(staged, kElfMagic)) {
                return refuse(QStringLiteral("the staged file is not a program"));
            }
            const QFileInfo targetInfo(target);
            if (!targetInfo.exists()) {
                return refuse(QStringLiteral("%1 is not there any more").arg(target));
            }
            if (!QFileInfo(targetInfo.absolutePath()).isWritable()) {
                return refuse(QStringLiteral("%1 cannot be written to, so the AppImage there "
                                             "cannot be replaced")
                                  .arg(targetInfo.absolutePath()));
            }
            InstallOutcome outcome = replaceFile(staged, target);
            if (outcome.applied) {
                qCInfo(logApp) << "replaced the AppImage at" << target;
            }
            return outcome;
        }

        case InstallKind::WindowsInstaller: {
            if (!startsWith(staged, kDosMagic)) {
                return refuse(QStringLiteral("the staged file is not a program"));
            }
            // The installer replaces the installed copy; this program is asked
            // to stop rather than trying to overwrite itself while running.
            // /S is the silent switch the NSIS script is built with.
            const bool started = QProcess::startDetached(staged, {QStringLiteral("/S")});
            if (!started) {
                return refuse(QStringLiteral("the installer could not be started"));
            }
            InstallOutcome outcome;
            outcome.applied = true;
            outcome.needsRestart = true;
            qCInfo(logApp) << "handed the update to the installer";
            return outcome;
        }

        case InstallKind::WindowsPortable:
        case InstallKind::MacBundle: {
            // A portable copy is a directory somebody unpacked and a bundle is
            // a directory somebody dragged; replacing either from inside the
            // program that lives in it is how half-copied installs happen. The
            // download is verified and left where they can open it.
            InstallOutcome outcome;
            outcome.handedOver = staged;
            outcome.problem = QStringLiteral(
                                  "this copy is %1, so the update has been downloaded "
                                  "and checked but has to be put in place by hand")
                                  .arg(describe(kind));
            return outcome;
        }

        case InstallKind::Unknown:
        case InstallKind::PackageManaged:
        case InstallKind::Development:
            return refuse(
                QStringLiteral("this is %1, which is not updated this way").arg(describe(kind)));
    }

    return refuse(QStringLiteral("this installation is of an unrecognised shape"));
}

bool UpdateInstaller::undo(const InstallOutcome& outcome, const QString& target) {
    if (outcome.previous.isEmpty() || !QFileInfo::exists(outcome.previous)) {
        return true;
    }
    QFile::remove(target);
    return QFile::rename(outcome.previous, target);
}

}  // namespace transmit::core
