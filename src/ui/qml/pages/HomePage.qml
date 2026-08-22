import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Transmit.Backend
import Transmit.Components
import Transmit.Theme

/// The starting point: what this machine is, and the two things the user can
/// do from here.
ScrollView {
    id: page
    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: page.availableWidth
        spacing: Theme.spacingXl

        AppSectionHeader {
            title: qsTr("Move this computer's setup to another one")
            subtitle: qsTr("Transmit copies your files, the data and settings your programs keep, "
                         + "and the list of what you have installed onto a USB drive - then puts "
                         + "them back on a computer running a different operating system.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLg

            AppCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 168
                interactive: true
                onClicked: AppController.currentPage = "export"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingXl
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Save this computer")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeHeading
                        font.weight: Theme.weightSemiBold
                    }

                    Text {
                        text: qsTr("Pick what to take, choose a drive, and Transmit writes a "
                                 + "single compressed archive you can carry away.")
                        Layout.fillWidth: true
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        wrapMode: Text.WordWrap
                        lineHeight: 1.35
                    }

                    Item { Layout.fillHeight: true }

                    AppButton {
                        text: qsTr("Start")
                        variant: "primary"
                        onClicked: AppController.currentPage = "export"
                    }
                }
            }

            AppCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 168
                interactive: true
                onClicked: AppController.currentPage = "import"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingXl
                    spacing: Theme.spacingSm

                    Text {
                        text: qsTr("Bring a computer here")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeHeading
                        font.weight: Theme.weightSemiBold
                    }

                    Text {
                        text: qsTr("Open an archive from a drive. Transmit shows you exactly "
                                 + "what it would do before it touches anything.")
                        Layout.fillWidth: true
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        wrapMode: Text.WordWrap
                        lineHeight: 1.35
                    }

                    Item { Layout.fillHeight: true }

                    AppButton {
                        text: qsTr("Open an archive")
                        onClicked: AppController.currentPage = "import"
                    }
                }
            }
        }

        AppCard {
            Layout.fillWidth: true
            implicitHeight: machineFacts.implicitHeight + Theme.spacingXl * 2

            ColumnLayout {
                id: machineFacts
                anchors.fill: parent
                anchors.margins: Theme.spacingXl
                spacing: Theme.spacingSm

                Text {
                    text: qsTr("This computer")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeHeading
                    font.weight: Theme.weightSemiBold
                }

                AppKeyValue { label: qsTr("System");  value: AppController.osName }
                AppKeyValue { label: qsTr("Computer"); value: AppController.hostName }
                AppKeyValue { label: qsTr("Account");  value: AppController.userName }
                AppKeyValue { label: qsTr("Home folder"); value: AppController.homeDirectory }
                AppKeyValue {
                    label: qsTr("Desktop")
                    value: AppController.desktopEnvironment
                    visible: AppController.desktopEnvironment !== ""
                }
                AppKeyValue {
                    label: qsTr("Installs programs with")
                    value: AppController.packageManager
                }
            }
        }

        AppInlineMessage {
            visible: !AppController.encryptionAvailable
            tone: "warning"
            title: qsTr("This build cannot encrypt archives")
            body: qsTr("It was compiled without OpenSSL. Archives can still be written and read, "
                     + "but they cannot be protected with a passphrase and saved passwords "
                     + "cannot be carried across.")
        }
    }
}
