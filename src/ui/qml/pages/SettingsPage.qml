import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Transmit.Backend
import Transmit.Components
import Transmit.Theme

AppScrollView {
    id: page

    ColumnLayout {
        width: page.availableWidth
        spacing: Spacing.xl

        // No heading: the toolbar above already says where you are, and
        // saying it twice just pushes the first setting down the page.
        AppLabelledField {
            Layout.fillWidth: true
            label: qsTr("Appearance")
            helperText: qsTr("Following the system means Transmit changes with it.")

            AppComboBox {
                model: [
                    { value: "system", label: qsTr("Follow the system") },
                    { value: "light",  label: qsTr("Light") },
                    { value: "dark",   label: qsTr("Dark") }
                ]
                textRole: "label"
                valueRole: "value"
                currentIndex: AppController.themeMode === "light" ? 1
                            : AppController.themeMode === "dark"  ? 2 : 0
                onActivated: AppController.themeMode = currentValue
                Accessible.name: qsTr("Appearance")
            }
        }

        AppCard {
            Layout.fillWidth: true
            implicitHeight: accessibility.implicitHeight + Spacing.xl * 2

            ColumnLayout {
                id: accessibility
                anchors.fill: parent
                anchors.margins: Spacing.xl
                spacing: Spacing.md

                Text {
                    text: qsTr("Motion")
                    color: Colors.textPrimary
                    font.family: Typography.family
                    font.pixelSize: Typography.heading
                    font.weight: Typography.semiBold
                }

                AppCheckRow {
                    label: qsTr("Reduce motion")
                    description: qsTr("Turns off the fades and the sliding. Nothing changes "
                                    + "about what the interface does - only how it gets there.")
                    checked: AppController.reduceMotion
                    onCheckedChanged: AppController.reduceMotion = checked
                }
            }
        }

        AppCard {
            Layout.fillWidth: true
            implicitHeight: about.implicitHeight + Spacing.xl * 2

            ColumnLayout {
                id: about
                anchors.fill: parent
                anchors.margins: Spacing.xl
                spacing: Spacing.sm

                Text {
                    text: qsTr("About Transmit")
                    color: Colors.textPrimary
                    font.family: Typography.family
                    font.pixelSize: Typography.heading
                    font.weight: Typography.semiBold
                }

                AppKeyValue { label: qsTr("Version"); value: AppController.appVersion }
                AppKeyValue {
                    label: qsTr("Encryption")
                    value: AppController.encryptionAvailable
                           ? qsTr("Available (AES-256-GCM)")
                           : qsTr("Not built in")
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: Spacing.sm
                    text: qsTr("Transmit does not send anything anywhere. Everything it reads "
                             + "goes onto the drive you choose and nowhere else.")
                    color: Colors.textSecondary
                    font.family: Typography.family
                    font.pixelSize: Typography.small
                    wrapMode: Text.WordWrap
                    lineHeight: 1.35
                }
            }
        }
    }
}
