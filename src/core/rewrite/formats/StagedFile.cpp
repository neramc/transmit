#include <QFile>

#include "core/rewrite/formats/Rewriters.h"
#include "core/utils/Logging.h"

namespace transmit::core::rewriters {

bool writeStaged(const QString& stagedPath, const QByteArray& contents) {
    QFile staged(stagedPath);
    if (!staged.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(logRewrite) << "could not stage" << stagedPath << staged.errorString();
        return false;
    }

    const qint64 written = staged.write(contents);
    const bool whole = written == contents.size() && staged.flush();
    staged.close();

    if (!whole) {
        qCWarning(logRewrite) << "only" << written << "of" << contents.size() << "bytes reached"
                              << stagedPath << staged.errorString();
        // Removed rather than left behind: apply() takes the existence of this
        // file as the answer to "was a new version produced", so a truncated
        // one that outlives the attempt is a truncated settings file installed
        // on the next run.
        QFile::remove(stagedPath);
        return false;
    }
    return true;
}

}  // namespace transmit::core::rewriters
