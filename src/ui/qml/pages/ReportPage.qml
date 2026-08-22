import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Transmit.Backend
import Transmit.Components
import Transmit.Theme

/// The continuity report: the honest account of how much of the old machine
/// actually made it, and what is left for the user to do.
ColumnLayout {
    id: page
    spacing: Theme.spacingLg

    AppSectionHeader {
        title: qsTr("How much came across")
        subtitle: qsTr("Some things move byte for byte. Some have to be translated for this "
                     + "system. A few cannot cross at all - program binaries, drivers and "
                     + "anything a machine sealed to its own hardware.")
    }

    // Grade summary doubles as the filter, so a user who wants to know "what
    // do I still have to do" is one click away from exactly that list.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        Repeater {
            model: [
                { grade: -1, label: qsTr("Everything"), tone: "neutral",
                  count: reportModel.fullCount + reportModel.adaptedCount
                         + reportModel.manualCount + reportModel.impossibleCount },
                { grade: 0, label: qsTr("Full"), tone: "success", count: reportModel.fullCount },
                { grade: 1, label: qsTr("Adapted"), tone: "info", count: reportModel.adaptedCount },
                { grade: 2, label: qsTr("Needs you"), tone: "warning", count: reportModel.manualCount },
                { grade: 3, label: qsTr("Not portable"), tone: "error", count: reportModel.impossibleCount }
            ]

            AppCard {
                Layout.fillWidth: true
                implicitHeight: 66
                interactive: true
                selected: reportModel.gradeFilter === modelData.grade
                onClicked: reportModel.gradeFilter = modelData.grade

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("%1: %2 items").arg(modelData.label).arg(modelData.count)

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 2

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: modelData.count
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeTitle
                        font.weight: Theme.weightSemiBold
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: modelData.label
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeCaption
                    }
                }
            }
        }
    }

    ListView {
        id: noteList
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: Theme.spacingSm
        model: reportModel

        delegate: AppCard {
            width: noteList.width
            implicitHeight: noteLayout.implicitHeight + Theme.spacingLg * 2

            ColumnLayout {
                id: noteLayout
                anchors.fill: parent
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingXs

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    AppBadge {
                        text: gradeName
                        tone: grade === 0 ? "success"
                            : grade === 1 ? "info"
                            : grade === 2 ? "warning" : "error"
                    }

                    Text {
                        Layout.fillWidth: true
                        text: subject
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeBody
                        font.weight: Theme.weightMedium
                        elide: Text.ElideMiddle
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: detail
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                    lineHeight: 1.3
                }
            }
        }

        AppEmptyState {
            anchors.centerIn: parent
            width: parent.width * 0.7
            visible: noteList.count === 0
            title: qsTr("Nothing to report")
            body: qsTr("Everything in this run came across without needing a note. Run a capture "
                     + "or a restore to see a report here.")
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingMd

        AppButton {
            text: qsTr("Back to start")
            variant: "ghost"
            onClicked: AppController.currentPage = "home"
        }

        Item { Layout.fillWidth: true }

        AppButton {
            text: qsTr("Save this report…")
            onClicked: saveDialog.open()
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save the report")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("Report files (*.json)")]
        onAccepted: reportModel.saveTo(AppController.fromFileUrl(selectedFile.toString()))
    }
}
