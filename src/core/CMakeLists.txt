# The application layer: capture and restore services, the continuity model and
# the Qt models the UI binds to. No QML and no widgets, so everything here can
# be driven from the command line and from tests.

add_library(transmit_core STATIC
    continuity/ContinuityTypes.cpp
    recipe/RecipeCatalog.cpp
    recipe/AppInventoryPayload.cpp
    recipe/StateRelocator.cpp
    recipe/InstallScriptWriter.cpp
    settings/SettingsDomain.cpp
    secrets/SecretsDomain.cpp
    rewrite/PathTranslator.cpp
    rewrite/RewritePlan.cpp
    rewrite/PathRewriter.cpp
    rewrite/formats/TextRewriter.cpp
    rewrite/formats/IniRewriter.cpp
    rewrite/formats/JsonRewriter.cpp
    rewrite/formats/PlistRewriter.cpp
    rewrite/formats/SqliteRewriter.cpp
    services/ScanService.cpp
    services/ConsistentCopy.cpp
    services/ExportService.cpp
    services/ImportService.cpp
    services/RollbackWriter.cpp
    services/ProfileService.cpp
    tasks/BlockPipeline.cpp)

add_library(Transmit::Core ALIAS transmit_core)

target_include_directories(transmit_core PUBLIC "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(transmit_core
    PUBLIC  Transmit::Common Transmit::Format Transmit::Platform Transmit::Warnings
            Qt6::Core Qt6::Concurrent
    PRIVATE SQLite::SQLite3)

set_target_properties(transmit_core PROPERTIES AUTOMOC ON)

# The application catalog is compiled in rather than installed alongside, so a
# single binary always knows how to recognise and place application state.
qt_add_resources(transmit_core "transmit_catalog"
    PREFIX "/catalog"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES "${CMAKE_SOURCE_DIR}/resources/app-catalog.json")
