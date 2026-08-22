# The desktop application.
#
# C++ types the interface binds to live in their own QML module, which the
# pages import. That keeps the dependency pointing one way: pages know about
# the backend, the backend knows nothing about the interface.

qt_add_qml_module(transmit_backend
    URI Transmit.Backend
    VERSION 1.0
    STATIC
    SOURCES
        AppController.h AppController.cpp
        ExportController.h ExportController.cpp
        ImportController.h ImportController.cpp
        models/DriveListModel.h models/DriveListModel.cpp
        models/ProfileListModel.h models/ProfileListModel.cpp
        models/ContinuityReportModel.h models/ContinuityReportModel.cpp
    RESOURCE_PREFIX /qt/qml)

target_link_libraries(transmit_backend
    PUBLIC Transmit::Core Transmit::Platform Transmit::Warnings
           Qt6::Quick Qt6::Concurrent Qt6::Gui)

# qmltyperegistrar emits includes by header basename, so the directories
# holding the registered types have to be on the include path.
target_include_directories(transmit_backend PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/models")

add_subdirectory("${CMAKE_SOURCE_DIR}/src/ui/qml" "${CMAKE_BINARY_DIR}/src/ui/qml")

qt_add_executable(transmit main.cpp)

qt_add_resources(transmit "transmit_icons"
    PREFIX "/icons"
    BASE "${CMAKE_SOURCE_DIR}/resources/icons"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/icons/transmit-256.png"
        "${CMAKE_SOURCE_DIR}/resources/icons/transmit.svg")

target_link_libraries(transmit PRIVATE
    Transmit::Core Transmit::Warnings
    Qt6::Quick Qt6::QuickControls2 Qt6::Gui
    transmit_qml_mainplugin
    transmit_qml_themeplugin
    transmit_qml_componentsplugin
    transmit_qml_dialogsplugin
    transmit_qml_pagesplugin
    transmit_qml_layoutsplugin
    transmit_backendplugin)

# Static QML modules only register their types once their plugins are imported
# into the executable; without this the modules exist but resolve as unknown.
qt_import_qml_plugins(transmit)

set_target_properties(transmit PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.transmit.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

# ------------------------------------------------------------------ install
include(GNUInstallDirs)

install(TARGETS transmit
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(TARGET transmit-cli)
    install(TARGETS transmit-cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(UNIX AND NOT APPLE)
    install(FILES "${CMAKE_SOURCE_DIR}/resources/transmit.desktop"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")

    install(FILES "${CMAKE_SOURCE_DIR}/resources/icons/transmit.svg"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps")

    foreach(size 16 32 48 64 128 256)
        install(FILES "${CMAKE_SOURCE_DIR}/resources/icons/transmit-${size}.png"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${size}x${size}/apps"
            RENAME transmit.png)
    endforeach()
endif()
