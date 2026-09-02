# Central place for build options and shared compile settings.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# All QML modules build into one tree so qmlimportscanner - and qmllint, and
# Qt Creator - can find them from a single import path.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml" CACHE PATH "QML module output root")

option(TRANSMIT_BUILD_APP     "Build the Qt Quick desktop application" ON)
option(TRANSMIT_BUILD_CLI     "Build the headless command line tool"   ON)
option(TRANSMIT_BUILD_TESTS   "Build unit and integration tests"       OFF)
option(TRANSMIT_WITH_LZMA     "Enable the xz/LZMA2 maximum codec"      ON)
option(TRANSMIT_WITH_OPENSSL  "Enable archive encryption and secrets"  ON)
option(TRANSMIT_WITH_LIBSECRET "Read the Linux login keyring directly"  ON)
option(TRANSMIT_WERROR        "Treat compiler warnings as errors"      OFF)

# The updater.
#
# TRANSMIT_UPDATE_KEYS is the whole trust model in one string: a semicolon
# separated list of base64 Ed25519 public keys, and a build given none of them
# will tell people a new version exists and will refuse to download or install
# anything. That is the safe default rather than a limitation - an updater
# that installs what it cannot authenticate is a way to run somebody else's
# code on every machine that has this program.
#
# Several keys are allowed so a key can be rotated: publish feeds signed with
# the new one while builds that only know the old still accept theirs.
option(TRANSMIT_WITH_UPDATER   "Check for and install updates"           ON)
set(TRANSMIT_UPDATE_KEYS "" CACHE STRING
    "Base64 Ed25519 public keys that may sign the update feed, separated by ';'")
set(TRANSMIT_UPDATE_FEED "https://github.com/neramc/transmit/releases/latest/download/updates.json"
    CACHE STRING "Where the signed update feed is published")
set(TRANSMIT_RELEASES_PAGE "https://github.com/neramc/transmit/releases"
    CACHE STRING "Where people are sent to fetch a release by hand")

# Needs clang and its sanitiser runtime. Off by default so an ordinary
# build still produces the replay binaries, which is what keeps a crasher
# fixed on a machine that cannot fuzz.
option(TRANSMIT_BUILD_FUZZERS "Build the fuzz targets with libFuzzer"   OFF)
option(TRANSMIT_BUILD_BENCH   "Build the benchmark harness"             OFF)

# Interface target carrying the warning set every Transmit target compiles with.
add_library(transmit_warnings INTERFACE)
add_library(Transmit::Warnings ALIAS transmit_warnings)

if(MSVC)
    target_compile_options(transmit_warnings INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    if(TRANSMIT_WERROR)
        target_compile_options(transmit_warnings INTERFACE /WX)
    endif()
    target_compile_definitions(transmit_warnings INTERFACE
        NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
    target_compile_options(transmit_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Woverloaded-virtual -Wdouble-promotion)

    # Not for Objective-C++: reaching a Core Foundation type from ARC is
    # spelled `(__bridge CFTypeRef)object` and there is no C++-style form of
    # it, so the rule would forbid the only way to write the call.
    target_compile_options(transmit_warnings INTERFACE
        "$<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>")
    if(TRANSMIT_WERROR)
        target_compile_options(transmit_warnings INTERFACE -Werror)
    endif()
endif()
