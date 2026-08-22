# Platform differences live behind PlatformService; nothing above this
# directory contains an operating system conditional.

add_library(transmit_platform STATIC
    PlatformService.cpp
    PlatformServiceFactory.cpp
    SettingsProvider.cpp
    SecretStore.cpp)

add_library(Transmit::Platform ALIAS transmit_platform)

target_include_directories(transmit_platform PUBLIC "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(transmit_platform
    PUBLIC  Transmit::Common Transmit::Format Transmit::Warnings Qt6::Core
    PRIVATE Qt6::Network)

if(WIN32)
    # Credential Manager lives in advapi32; wincred.h needs it at link time.
    target_link_libraries(transmit_platform PRIVATE advapi32)
    target_sources(transmit_platform PRIVATE windows/WindowsPlatformService.cpp
                                                windows/WindowsSettingsProvider.cpp
                                                windows/WindowsSecretStore.cpp)
elseif(APPLE)
    target_sources(transmit_platform PRIVATE macos/MacOsPlatformService.mm
                                                macos/MacOsSettingsProvider.mm
                                                macos/MacOsSecretStore.mm)
    target_link_libraries(transmit_platform PRIVATE
        "-framework Foundation" "-framework AppKit" "-framework Security")
else()
    target_sources(transmit_platform PRIVATE linux/LinuxPlatformService.cpp
                                                linux/LinuxSettingsProvider.cpp
                                                linux/LinuxSecretStore.cpp)
    if(TRANSMIT_LIBSECRET_ENABLED)
        # SYSTEM, because the GLib headers are full of C casts and this project
        # builds with -Wold-style-cast -Werror.
        target_link_libraries(transmit_platform PRIVATE PkgConfig::LIBSECRET)
        target_include_directories(transmit_platform SYSTEM PRIVATE ${LIBSECRET_INCLUDE_DIRS})
        target_compile_definitions(transmit_platform PRIVATE TRANSMIT_HAVE_LIBSECRET=1)
    endif()
endif()
