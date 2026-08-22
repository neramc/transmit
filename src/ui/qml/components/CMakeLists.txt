# Reusable controls. These know about the theme and nothing else - a component
# that reached for a page or a controller would stop being reusable.

qt_add_qml_module(transmit_qml_components
    URI Transmit.Components
    VERSION 1.0
    STATIC
    QML_FILES
        AppBadge.qml
        AppButton.qml
        AppCard.qml
        AppCheckRow.qml
        AppEmptyState.qml
        AppInlineMessage.qml
        AppKeyValue.qml
        AppLabelledField.qml
        AppProgressBar.qml
        AppSectionHeader.qml
        AppSelectableCard.qml
        AppSidebar.qml
        AppStepper.qml
        AppTextField.qml
    RESOURCE_PREFIX /qt/qml)

target_link_libraries(transmit_qml_components PRIVATE transmit_qml_themeplugin)
