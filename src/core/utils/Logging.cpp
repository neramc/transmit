#include "core/utils/Logging.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(logApp, "transmit.app")
Q_LOGGING_CATEGORY(logCore, "transmit.core")
Q_LOGGING_CATEGORY(logUi, "transmit.ui")
Q_LOGGING_CATEGORY(logCapture, "transmit.capture")
Q_LOGGING_CATEGORY(logRestore, "transmit.restore")
Q_LOGGING_CATEGORY(logPlatform, "transmit.platform")
Q_LOGGING_CATEGORY(logRecipe, "transmit.recipe")
Q_LOGGING_CATEGORY(logRewrite, "transmit.rewrite")
Q_LOGGING_CATEGORY(logSettings, "transmit.settings")
Q_LOGGING_CATEGORY(logSecrets, "transmit.secrets")
Q_LOGGING_CATEGORY(logDatabase, "transmit.database")
Q_LOGGING_CATEGORY(logPerformance, "transmit.performance")

namespace transmit::core {

void configureLogging(bool verbose) {
    // Progress belongs on screen, not in the log, so informational output is
    // off by default; a capture of a million files would otherwise bury the
    // messages that matter.
    QLoggingCategory::setFilterRules(verbose ? QStringLiteral("transmit.*.debug=true\n"
                                                              "transmit.*.info=true")
                                             : QStringLiteral("transmit.*.debug=false\n"
                                                              "transmit.*.info=false"));
}

}  // namespace transmit::core
