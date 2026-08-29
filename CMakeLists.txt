cmake_minimum_required(VERSION 3.21)

project(Transmit
    VERSION 0.1.0
    DESCRIPTION "Cross-platform OS environment continuity tool"
    HOMEPAGE_URL "https://github.com/neramc/transmit"
    LANGUAGES CXX)

# The macOS platform layer is Objective-C++. Without this CMake hands the .mm
# files to the C++ compiler and calls them C++ - clang works out what they
# really are from the extension, but CMake does not, so anything scoped to a
# language lands on the wrong set of files.
if(APPLE)
    enable_language(OBJCXX)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(TransmitOptions)
include(Dependencies)

add_subdirectory(src/format)

if(TRANSMIT_BUILD_APP OR TRANSMIT_BUILD_CLI)
    add_subdirectory(src/core/utils)
    add_subdirectory(src/platform)
    add_subdirectory(src/core)
endif()

if(TRANSMIT_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(TRANSMIT_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(TRANSMIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Not under tests/: the benchmark is not a test and measuring it should not
# mean building four hundred of them. TRANSMIT_BUILD_BENCH used to be reachable
# only with TRANSMIT_BUILD_TESTS also on, which made an option that said it
# built the harness quietly build nothing.
if(TRANSMIT_BUILD_BENCH AND NOT TRANSMIT_BUILD_TESTS)
    add_subdirectory(tests/bench)
endif()

include(TransmitSummary)
