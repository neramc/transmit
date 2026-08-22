#include "core/continuity/ContinuityTypes.h"

#include <QCoreApplication>

namespace transmit::core {

QString continuityGradeName(ContinuityGrade grade) {
    switch (grade) {
        case ContinuityGrade::Full:
            return QCoreApplication::translate("Continuity", "Full");
        case ContinuityGrade::Adapted:
            return QCoreApplication::translate("Continuity", "Adapted");
        case ContinuityGrade::Manual:
            return QCoreApplication::translate("Continuity", "Needs you");
        case ContinuityGrade::Impossible:
            return QCoreApplication::translate("Continuity", "Not portable");
    }
    return {};
}

QString continuityGradeDescription(ContinuityGrade grade) {
    switch (grade) {
        case ContinuityGrade::Full:
            return QCoreApplication::translate("Continuity",
                                               "Carried across unchanged, byte for byte.");
        case ContinuityGrade::Adapted:
            return QCoreApplication::translate(
                "Continuity",
                "Carried across with a translation for this operating system, such as rewritten "
                "paths or the nearest matching setting.");
        case ContinuityGrade::Manual:
            return QCoreApplication::translate(
                "Continuity",
                "Ready for you to finish: run the generated script or change the "
                "setting the report points to.");
        case ContinuityGrade::Impossible:
            return QCoreApplication::translate(
                "Continuity",
                "Cannot cross this operating system boundary. Program binaries, drivers, "
                "hardware-sealed keys and licence activations stay behind.");
    }
    return {};
}

QString conflictPolicyName(ConflictPolicy policy) {
    switch (policy) {
        case ConflictPolicy::Skip:
            return QCoreApplication::translate("Conflict", "Skip");
        case ConflictPolicy::Overwrite:
            return QCoreApplication::translate("Conflict", "Overwrite");
        case ConflictPolicy::KeepBoth:
            return QCoreApplication::translate("Conflict", "Keep both");
        case ConflictPolicy::NewerWins:
            return QCoreApplication::translate("Conflict", "Keep the newer one");
    }
    return {};
}

QStringList CaptureSelection::defaultExcludes() {
    // Large, regenerable and of no value on the new machine. Every entry here
    // is something a user would be annoyed to spend USB space and minutes on.
    return {
        // caches and thumbnails
        QStringLiteral("**/Cache/**"),
        QStringLiteral("**/cache/**"),
        QStringLiteral("**/Caches/**"),
        QStringLiteral("**/cache2/**"),
        QStringLiteral("**/.cache/**"),
        QStringLiteral("**/GPUCache/**"),
        QStringLiteral("**/Code Cache/**"),
        QStringLiteral("**/ShaderCache/**"),
        QStringLiteral("**/thumbnails/**"),
        // trash and temporary files
        QStringLiteral("**/.Trash/**"),
        QStringLiteral("**/.local/share/Trash/**"),
        QStringLiteral("**/$RECYCLE.BIN/**"),
        QStringLiteral("**/*.tmp"),
        QStringLiteral("**/~$*"),
        QStringLiteral("**/Temp/**"),
        QStringLiteral("**/tmp/**"),
        // build output and dependency trees that a rebuild recreates
        QStringLiteral("**/node_modules/**"),
        QStringLiteral("**/.venv/**"),
        QStringLiteral("**/venv/**"),
        QStringLiteral("**/__pycache__/**"),
        QStringLiteral("**/target/debug/**"),
        QStringLiteral("**/target/release/**"),
        QStringLiteral("**/.gradle/**"),
        QStringLiteral("**/build/**"),
        QStringLiteral("**/.git/objects/**"),
        // virtual machine and container images, which are huge and rebuildable
        QStringLiteral("**/*.vdi"),
        QStringLiteral("**/*.vmdk"),
        QStringLiteral("**/*.qcow2"),
        QStringLiteral("**/*.vhdx"),
        QStringLiteral("**/.docker/**"),
        // OS scratch space
        QStringLiteral("**/pagefile.sys"),
        QStringLiteral("**/hiberfil.sys"),
        QStringLiteral("**/swapfile.sys"),
        QStringLiteral("**/.DS_Store"),
    };
}

double ExportReport::compressionRatio() const {
    if (rawBytes == 0) {
        return 1.0;
    }
    return static_cast<double>(storedBytes) / static_cast<double>(rawBytes);
}

int ImportReport::countOf(ContinuityGrade grade) const {
    int total = 0;
    for (const ContinuityNote& note : notes) {
        if (note.grade == grade) {
            ++total;
        }
    }
    return total;
}

}  // namespace transmit::core
