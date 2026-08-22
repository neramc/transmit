add_subdirectory(theme)
add_subdirectory(components)
add_subdirectory(dialogs)
add_subdirectory(pages)
add_subdirectory(layouts)

# The application shell. Kept as its own module so the window can be replaced
# without disturbing the pages it hosts.
qt_add_qml_module(transmit_qml_main
    URI Transmit
    VERSION 1.0
    STATIC
    QML_FILES Main.qml
    RESOURCE_PREFIX /qt/qml)

target_link_libraries(transmit_qml_main
    PRIVATE transmit_qml_themeplugin transmit_qml_layoutsplugin
            transmit_backendplugin)
