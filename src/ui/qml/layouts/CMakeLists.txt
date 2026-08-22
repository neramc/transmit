# The window's furniture. These arrange the pages but hold none of their
# content, so the shell can be rearranged without touching a single page.

qt_add_qml_module(transmit_qml_layouts
    URI Transmit.Layouts
    VERSION 1.0
    STATIC
    QML_FILES
        AppShell.qml
        AppToolBar.qml
        AppStatusBar.qml
        ContentView.qml
    RESOURCE_PREFIX /qt/qml)

target_link_libraries(transmit_qml_layouts
    PRIVATE transmit_qml_themeplugin transmit_qml_componentsplugin
            transmit_qml_dialogsplugin transmit_qml_pagesplugin
            transmit_backendplugin)
