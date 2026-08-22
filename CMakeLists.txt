cmake_minimum_required(VERSION 3.21)

project(Transmit
    VERSION 0.1.0
    DESCRIPTION "Cross-platform OS environment continuity tool"
    HOMEPAGE_URL "https://github.com/neramc/transmit"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(TransmitOptions)
include(Dependencies)

add_subdirectory(src/format)

if(TRANSMIT_BUILD_APP)
    add_subdirectory(src/core)
    add_subdirectory(src/platform)
    add_subdirectory(src/app)
endif()

if(TRANSMIT_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(TRANSMIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(TransmitSummary)
