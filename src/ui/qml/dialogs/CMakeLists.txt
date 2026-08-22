# Modal dialogs. They are kept out of the pages so that a page being unloaded
# can never leave one stranded on screen, and so the shell owns them.

qt_add_qml_module(transmit_qml_dialogs
    URI Transmit.Dialogs
    VERSION 1.0
    STATIC
    QML_FILES
        AppDialog.qml
        ConfirmDialog.qml
        DialogHost.qml
    RESOURCE_PREFIX /qt/qml)

target_link_libraries(transmit_qml_dialogs
    PRIVATE transmit_qml_themeplugin transmit_qml_componentsplugin)
