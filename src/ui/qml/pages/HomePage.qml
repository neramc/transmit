import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Transmit.Backend
import Transmit.Components
import Transmit.Theme

/// The starting point: what this machine is, and the two things the user can
/// do from here.
AppScrollView {
    id: page

    ColumnLayout {
        width: page.availableWidth
        spacing: Spacing.xl

        AppSectionHeader {
            title: qsTr("Move this computer's setup to another one")
            subtitle: qsTr("Transmit copies your files, the data and settings your programs keep, "
                         + "and the list of what you have installed onto a USB drive - then puts "
                         + "them back on a computer running a different operating system.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Spacing.lg

            AppCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Sized by what is in it. A fixed height here was right at one
                // type scale and eight pixels short at the next, and the button
                // simply hung out of the bottom of the card.
                implicitHeight: saveContent.implicitHeight + Spacing.s24 * 2
                interactive: true
                onClicked: AppController.currentPage = "export"

                ColumnLayout {
                    id: saveContent
                    anchors.fill: parent
                    anchors.margins: Spacing.xl
                    spacing: Spacing.sm

                    Text {
                        text: qsTr("Save this computer")
                        color: Colors.textPrimary
                        font.family: Typography.family
                        font.pixelSize: Typography.heading
                        font.weight: Typography.semiBold
                    }

                    Text {
                        text: qsTr("Pick what to take, choose a drive, and Transmit writes a "
                                 + "single compressed archive you can carry away.")
                        Layout.fillWidth: true
                        color: Colors.textSecondary
                        font.family: Typography.family
                        font.pixelSize: Typography.small
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
                Layout.fillHeight: true
                implicitHeight: openContent.implicitHeight + Spacing.s24 * 2
                interactive: true
                onClicked: AppController.currentPage = "import"

                ColumnLayout {
                    id: openContent
                    anchors.fill: parent
                    anchors.margins: Spacing.xl
                    spacing: Spacing.sm

                    Text {
                        text: qsTr("Bring a computer here")
                        color: Colors.textPrimary
                        font.family: Typography.family
                        font.pixelSize: Typography.heading
                        font.weight: Typography.semiBold
                    }

                    Text {
                        text: qsTr("Open an archive from a drive. Transmit shows you exactly "
                                 + "what it would do before it touches anything.")
                        Layout.fillWidth: true
                        color: Colors.textSecondary
                        font.family: Typography.family
                        font.pixelSize: Typography.small
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
            implicitHeight: machineFacts.implicitHeight + Spacing.xl * 2

            ColumnLayout {
                id: machineFacts
                anchors.fill: parent
                anchors.margins: Spacing.xl
                spacing: Spacing.sm

                Text {
                    text: qsTr("This computer")
                    color: Colors.textPrimary
                    font.family: Typography.family
                    font.pixelSize: Typography.heading
                    font.weight: Typography.semiBold
                }

                AppKeyValue { label: qsTr("System");  value: AppController.osName }
                AppKeyValue { label: qsTr("Computer"); value: AppController.hostName }
                AppKeyValue { label: qsTr("Account");  value: AppController.userName }
                AppKeyValue { label: qsTr("Home folder"); value: AppController.homeDirectory }
                AppKeyValue { label: qsTr("Desktop"); value: AppController.desktopEnvironment }
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
