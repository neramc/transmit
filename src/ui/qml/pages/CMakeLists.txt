# One page per screen. Pages compose components and call into the backend;
# they hold no business logic of their own.

qt_add_qml_module(transmit_qml_pages
    URI Transmit.Pages
    VERSION 1.0
    STATIC
    QML_FILES
        HomePage.qml
        ExportPage.qml
        ImportPage.qml
        ReportPage.qml
        SettingsPage.qml
    RESOURCE_PREFIX /qt/qml)

target_link_libraries(transmit_qml_pages
    PRIVATE transmit_qml_themeplugin transmit_qml_componentsplugin transmit_backendplugin)
