# The application layer: capture and restore services, the continuity model and
# the Qt models the UI binds to. No QML and no widgets, so everything here can
# be driven from the command line and from tests.

add_library(transmit_core STATIC
    continuity/ContinuityTypes.cpp
    services/ScanService.cpp
    services/ConsistentCopy.cpp
    services/ExportService.cpp
    services/ImportService.cpp
    services/ProfileService.cpp
    tasks/BlockPipeline.cpp)

add_library(Transmit::Core ALIAS transmit_core)

target_include_directories(transmit_core PUBLIC "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(transmit_core
    PUBLIC  Transmit::Common Transmit::Format Transmit::Platform Transmit::Warnings
            Qt6::Core Qt6::Concurrent
    PRIVATE SQLite::SQLite3)

set_target_properties(transmit_core PROPERTIES AUTOMOC ON)
