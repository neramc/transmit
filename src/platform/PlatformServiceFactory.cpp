#include "platform/PlatformService.h"

#if defined(Q_OS_WIN)
#include "platform/windows/WindowsPlatformService.h"
#elif defined(Q_OS_MACOS)
#include "platform/macos/MacOsPlatformService.h"
#else
#include "platform/linux/LinuxPlatformService.h"
#endif

namespace transmit::platform {

std::unique_ptr<PlatformService> PlatformService::create() {
#if defined(Q_OS_WIN)
    return std::make_unique<WindowsPlatformService>();
#elif defined(Q_OS_MACOS)
    return std::make_unique<MacOsPlatformService>();
#else
    return std::make_unique<LinuxPlatformService>();
#endif
}

}  // namespace transmit::platform
