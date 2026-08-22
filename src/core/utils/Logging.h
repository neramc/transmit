#pragma once

#include <QLoggingCategory>

/// Categories keep the noise controllable through QT_LOGGING_RULES without
/// scattering ad-hoc flags through the code. Debug output is off by default in
/// release builds; see Application::configureLogging.
Q_DECLARE_LOGGING_CATEGORY(logApp)
Q_DECLARE_LOGGING_CATEGORY(logCore)
Q_DECLARE_LOGGING_CATEGORY(logUi)
Q_DECLARE_LOGGING_CATEGORY(logCapture)
Q_DECLARE_LOGGING_CATEGORY(logRestore)
Q_DECLARE_LOGGING_CATEGORY(logPlatform)
Q_DECLARE_LOGGING_CATEGORY(logRecipe)
Q_DECLARE_LOGGING_CATEGORY(logRewrite)
Q_DECLARE_LOGGING_CATEGORY(logSettings)
Q_DECLARE_LOGGING_CATEGORY(logSecrets)
Q_DECLARE_LOGGING_CATEGORY(logDatabase)
Q_DECLARE_LOGGING_CATEGORY(logPerformance)

namespace transmit::core {

/// Installs the default logging rules: warnings and errors only, unless the
/// caller asks for more. QT_LOGGING_RULES from the environment still wins, so
/// a user chasing a problem can turn any category back on.
void configureLogging(bool verbose);

}  // namespace transmit::core
