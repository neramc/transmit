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
            implicitHeight: updates.implicitHeight + Spacing.xl * 2

            ColumnLayout {
                id: updates
                anchors.fill: parent
                anchors.margins: Spacing.xl
                spacing: Spacing.md

                Text {
                    text: qsTr("Updates")
                    color: Colors.textPrimary
                    font.family: Typography.family
                    font.pixelSize: Typography.heading
                    font.weight: Typography.semiBold
                }

                AppLabelledField {
                    Layout.fillWidth: true
                    label: qsTr("When there is a new version")
                    helperText: qsTr("A fix for something that loses your files or lets somebody "
                                   + "else read them is installed whatever this says. Everything "
                                   + "else waits for you.")

                    AppComboBox {
                        model: [
                            { value: "notify",    label: qsTr("Tell me about it") },
                            { value: "automatic", label: qsTr("Install it") },
                            { value: "manual",    label: qsTr("Only when I ask") }
                        ]
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: UpdateController.preference === "automatic" ? 1
                                    : UpdateController.preference === "manual" ? 2 : 0
                        onActivated: UpdateController.preference = currentValue
                        Accessible.name: qsTr("When there is a new version")
                    }
                }

                AppKeyValue {
                    label: qsTr("This copy")
                    value: UpdateController.installKind
                }

                AppKeyValue {
                    visible: UpdateController.lastChecked.length > 0
                    label: qsTr("Last looked")
                    value: UpdateController.lastChecked
                }

                AppInlineMessage {
                    objectName: "updateMessage"
                    Layout.fillWidth: true
                    visible: UpdateController.summary.length > 0
                    tone: UpdateController.mandatory ? "warning" : "info"
                    title: UpdateController.updateAvailable
                           ? qsTr("Transmit %1").arg(UpdateController.availableVersion)
                           : qsTr("Updates")
                    body: UpdateController.summary
                }

                AppProgressBar {
                    Layout.fillWidth: true
                    visible: UpdateController.downloading
                    value: UpdateController.progressPercent / 100
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Spacing.md

                    AppButton {
                        objectName: "checkForUpdates"
                        text: UpdateController.checking ? qsTr("Looking...") : qsTr("Check now")
                        enabled: !UpdateController.checking && !UpdateController.downloading
                        onClicked: UpdateController.checkNow()
                    }

                    AppButton {
                        objectName: "installUpdate"
                        text: qsTr("Install %1").arg(UpdateController.availableVersion)
                        variant: "primary"
                        visible: UpdateController.canInstall && !UpdateController.installed
                        enabled: !UpdateController.downloading
                        onClicked: UpdateController.installNow()
                    }

                    AppButton {
                        text: qsTr("Open the releases page")
                        visible: UpdateController.updateAvailable && !UpdateController.canInstall
                        onClicked: Qt.openUrlExternally(UpdateController.releasesPage)
                    }

                    Item { Layout.fillWidth: true }
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
