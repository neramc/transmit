# The application layer: capture and restore services, the continuity model and
# the Qt models the UI binds to. No QML and no widgets, so everything here can
# be driven from the command line and from tests.

add_library(transmit_core STATIC
    continuity/ContinuityTypes.cpp
    continuity/SelectionCodec.cpp
    recipe/AppRecipe.cpp
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
    services/VerifyService.cpp
    services/RepairService.cpp
    services/ImportService.cpp
    services/RollbackWriter.cpp
    services/ProfileService.cpp
    tasks/BlockPipeline.cpp
    update/Version.cpp
    update/UpdateManifest.cpp
    update/UpdateSignature.cpp
    update/InstallKind.cpp
    update/UpdateDecision.cpp
    update/UpdateService.cpp
    update/UpdateInstaller.cpp)

add_library(Transmit::Core ALIAS transmit_core)

target_include_directories(transmit_core PUBLIC "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(transmit_core
    PUBLIC  Transmit::Common Transmit::Format Transmit::Platform Transmit::Warnings
            Qt6::Core Qt6::Concurrent Qt6::Network
    PRIVATE SQLite::SQLite3)

# Ed25519 for the update feed's signature. The format layer keeps OpenSSL
# private, so the updater asks for it here rather than reaching through.
if(TRANSMIT_OPENSSL_ENABLED)
    target_link_libraries(transmit_core PRIVATE OpenSSL::Crypto)
endif()

# The updater's three constants, compiled in rather than read from a file
# next to the program: where the feed is, where people are sent to fetch a
# release by hand, and which keys may sign one. A file could be edited by
# whatever is trying to feed this machine an update.
target_compile_definitions(transmit_core PUBLIC
    TRANSMIT_UPDATE_FEED="${TRANSMIT_UPDATE_FEED}"
    TRANSMIT_RELEASES_PAGE="${TRANSMIT_RELEASES_PAGE}")
if(TRANSMIT_WITH_UPDATER)
    target_compile_definitions(transmit_core PUBLIC TRANSMIT_UPDATER_ENABLED)
endif()
if(TRANSMIT_UPDATE_KEYS)
    target_compile_definitions(transmit_core PRIVATE
        TRANSMIT_UPDATE_KEYS="${TRANSMIT_UPDATE_KEYS}")
endif()

set_target_properties(transmit_core PROPERTIES AUTOMOC ON)

# The application catalog is compiled in rather than installed alongside, so a
# single binary always knows how to recognise and place application state.
qt_add_resources(transmit_core "transmit_catalog"
    PREFIX "/catalog"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES "${CMAKE_SOURCE_DIR}/resources/app-catalog.json")
