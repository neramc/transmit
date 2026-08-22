import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Transmit.Backend
import Transmit.Components
import Transmit.Theme

ScrollView {
    id: page
    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: page.availableWidth
        spacing: Theme.spacingXl

        AppSectionHeader { title: qsTr("Settings") }

        AppLabelledField {
            Layout.fillWidth: true
            label: qsTr("Appearance")
            helperText: qsTr("Following the system means Transmit changes with it.")

            ComboBox {
                model: [
                    { value: "system", label: qsTr("Follow the system") },
                    { value: "light",  label: qsTr("Light") },
                    { value: "dark",   label: qsTr("Dark") }
                ]
                textRole: "label"
                valueRole: "value"
                currentIndex: AppController.themeMode === "light" ? 1
                            : AppController.themeMode === "dark"  ? 2 : 0
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBody
                onActivated: AppController.themeMode = currentValue
                Accessible.name: qsTr("Appearance")
            }
        }

        AppCard {
            Layout.fillWidth: true
            implicitHeight: about.implicitHeight + Theme.spacingXl * 2

            ColumnLayout {
                id: about
                anchors.fill: parent
                anchors.margins: Theme.spacingXl
                spacing: Theme.spacingSm

                Text {
                    text: qsTr("About Transmit")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeHeading
                    font.weight: Theme.weightSemiBold
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
                    Layout.topMargin: Theme.spacingSm
                    text: qsTr("Transmit does not send anything anywhere. Everything it reads "
                             + "goes onto the drive you choose and nowhere else.")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                    lineHeight: 1.35
                }
            }
        }
    }
}
