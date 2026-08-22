# Central place for build options and shared compile settings.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(TRANSMIT_BUILD_APP     "Build the Qt Quick desktop application" ON)
option(TRANSMIT_BUILD_CLI     "Build the headless command line tool"   ON)
option(TRANSMIT_BUILD_TESTS   "Build unit and integration tests"       OFF)
option(TRANSMIT_WITH_LZMA     "Enable the xz/LZMA2 maximum codec"      ON)
option(TRANSMIT_WITH_OPENSSL  "Enable archive encryption and secrets"  ON)
option(TRANSMIT_WERROR        "Treat compiler warnings as errors"      OFF)

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
        -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wdouble-promotion)
    if(TRANSMIT_WERROR)
        target_compile_options(transmit_warnings INTERFACE -Werror)
    endif()
endif()
