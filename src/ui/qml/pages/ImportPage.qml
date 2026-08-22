import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Transmit.Backend
import Transmit.Components
import Transmit.Theme

/// The restore wizard. Nothing is written until the last step, and the dry run
/// is offered first so the user can see the plan before agreeing to it.
Item {
    id: page

    /// Handed down by the shell rather than found through the scope chain, so
    /// moving a page cannot quietly break its bindings.
    required property var reportModel
    required property var dialogs

    property int step: 0
    property string archivePath: ""
    property string passphrase: ""
    property string conflictPolicy: "keep-both"
    property bool verifyFirst: false
    property bool restoreToFolder: false
    property string destinationOverride: ""

    readonly property var stepTitles: [qsTr("Choose an archive"), qsTr("What is inside"),
                                       qsTr("How to restore"), qsTr("Progress")]

    ColumnLayout {
        anchors.fill: parent
        spacing: Spacing.xl

        AppStepper {
            steps: page.stepTitles
            currentStep: page.step
            Layout.fillWidth: true
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: page.step

            // ---------------------------------------------------- step 1
            ColumnLayout {
                spacing: Spacing.lg

                AppSectionHeader {
                    title: qsTr("Which archive should be restored?")
                    subtitle: qsTr("Pick the drive it is on, or open the file directly. For an "
                                 + "archive written in parts, choose the first one.")
                }

                ListView {
                    id: driveList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: Spacing.sm
                    model: DriveListModel { id: drives }

                    Component.onCompleted: drives.setWatching(true)
                    Component.onDestruction: drives.setWatching(false)

                    delegate: AppSelectableCard {
                        id: driveCard
                        width: driveList.width
                        title: displayName

                        // Reading the drive happens on a worker thread; until
                        // it answers, the card says so rather than claiming
                        // there is nothing there.
                        readonly property var archiveCount:
                            ImportController.archiveCounts[rootPath]

                        description: archiveCount === undefined
                                     ? qsTr("Looking for archives…")
                                     : archiveCount === 0
                                       ? qsTr("No archives here - %1").arg(subtitle)
                                       : qsTr("%n archive(s) found", "", archiveCount)

                        badgeText: removable ? qsTr("Removable") : ""
                        badgeTone: removable ? "info" : "neutral"
                        selected: false
                        enabled: archiveCount !== undefined && archiveCount > 0
                        opacity: enabled ? 1.0 : 0.6

                        Component.onCompleted: ImportController.scanForArchives(rootPath)

                        onClicked: {
                            const found = ImportController.archivesOn(rootPath)
                            if (found.length > 0) {
                                page.archivePath = found[0]
                                ImportController.inspect(found[0], "")
                                page.step = 1
                            }
                        }
                    }

                    AppEmptyState {
                        anchors.centerIn: parent
                        width: parent.width * 0.7
                        visible: driveList.count === 0 && !drives.refreshing
                        title: qsTr("No drives found")
                        body: qsTr("Plug in the drive you captured onto, or open the archive "
                                 + "file directly.")
                    }

                    AppSpinner {
                        anchors.centerIn: parent
                        running: driveList.count === 0 && drives.refreshing
                        size: Sizing.iconSizeLarge
                    }
                }

                AppButton {
                    text: qsTr("Open an archive file…")
                    onClicked: fileDialog.open()
                }
            }

            // ---------------------------------------------------- step 2
            AppScrollView {
                id: overviewScroller

                ColumnLayout {
                    width: overviewScroller.availableWidth
                    spacing: Spacing.lg

                    AppSectionHeader {
                        title: qsTr("What is inside this archive")
                    }

                    AppInlineMessage {
                        visible: !ImportController.archiveValid
                                 && ImportController.archiveError !== ""
                        tone: "error"
                        title: qsTr("This file could not be opened")
                        body: ImportController.archiveError
                    }

                    AppLabelledField {
                        Layout.fillWidth: true
                        visible: ImportController.archiveEncrypted
                                 && !ImportController.archiveUnlocked
                        label: qsTr("Passphrase")
                        helperText: qsTr("This archive is encrypted. Its contents, including the "
                                       + "file names, stay hidden until it is unlocked.")

                        AppTextField {
                            echoMode: TextInput.Password
                            text: page.passphrase
                            onTextChanged: page.passphrase = text
                            onAccepted: ImportController.inspect(page.archivePath, page.passphrase)
                        }
                    }

                    AppButton {
                        text: qsTr("Unlock")
                        variant: "primary"
                        visible: ImportController.archiveEncrypted
                                 && !ImportController.archiveUnlocked
                        enabled: page.passphrase.length > 0
                        onClicked: ImportController.inspect(page.archivePath, page.passphrase)
                    }

                    AppCard {
                        Layout.fillWidth: true
                        visible: ImportController.archiveUnlocked
                        implicitHeight: facts.implicitHeight + Spacing.xl * 2

                        ColumnLayout {
                            id: facts
                            anchors.fill: parent
                            anchors.margins: Spacing.xl
                            spacing: Spacing.sm

                            AppKeyValue {
                                label: qsTr("Captured from")
                                value: ImportController.sourceDescription
                            }
                            AppKeyValue {
                                label: qsTr("Captured on")
                                value: ImportController.capturedAtText
                            }
                            AppKeyValue {
                                label: qsTr("Contents")
                                value: ImportController.contentsText
                            }
                            AppKeyValue {
                                label: qsTr("Protected")
                                value: ImportController.archiveEncrypted ? qsTr("Yes, encrypted")
                                                                         : qsTr("No")
                            }
                        }
                    }

                    AppInlineMessage {
                        visible: ImportController.crossPlatform
                        tone: "info"
                        title: qsTr("This came from a different operating system")
                        body: qsTr("Transmit will put each folder where it belongs here, and "
                                 + "rename anything whose name is not allowed on this system. "
                                 + "The report afterwards lists every change it made.")
                    }
                }
            }

            // ---------------------------------------------------- step 3
            AppScrollView {
                id: optionScroller

                ColumnLayout {
                    width: optionScroller.availableWidth
                    spacing: Spacing.lg

                    AppSectionHeader {
                        title: qsTr("How should it be restored?")
                        subtitle: qsTr("Start with a preview: it reports exactly what would happen "
                                     + "without changing a single file.")
                    }

                    AppLabelledField {
                        Layout.fillWidth: true
                        label: qsTr("If a file is already there")

                        AppComboBox {
                            model: [
                                { value: "keep-both",  label: qsTr("Keep both - recommended") },
                                { value: "skip",       label: qsTr("Leave the existing file alone") },
                                { value: "newer",      label: qsTr("Keep whichever is newer") },
                                { value: "overwrite",  label: qsTr("Replace it") }
                            ]
                            textRole: "label"
                            valueRole: "value"
                            currentIndex: 0
                            onActivated: page.conflictPolicy = currentValue
                            Accessible.name: qsTr("What to do when a file already exists")
                        }
                    }

                    AppCheckRow {
                        label: qsTr("Check the whole archive first")
                        description: qsTr("Reads every block and confirms it matches what was "
                                        + "written. Worth doing for a drive you are unsure of.")
                        checked: page.verifyFirst
                        onCheckedChanged: page.verifyFirst = checked
                    }

                    AppCheckRow {
                        label: qsTr("Restore into a folder I choose")
                        description: qsTr("Instead of putting things back where they belong, "
                                        + "everything goes into one folder you pick. Useful for "
                                        + "looking through an archive before committing to it.")
                        checked: page.restoreToFolder
                        onCheckedChanged: page.restoreToFolder = checked
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: page.restoreToFolder
                        spacing: Spacing.md

                        AppButton {
                            text: qsTr("Choose folder…")
                            onClicked: destinationDialog.open()
                        }

                        Text {
                            Layout.fillWidth: true
                            text: page.destinationOverride === "" ? qsTr("Nothing chosen yet")
                                                                  : page.destinationOverride
                            color: Colors.textSecondary
                            font.family: Typography.family
                            font.pixelSize: Typography.small
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            // ---------------------------------------------------- step 4
            ColumnLayout {
                spacing: Spacing.lg

                AppSectionHeader {
                    title: ImportController.finished
                           ? (ImportController.succeeded
                              ? (ImportController.wasDryRun ? qsTr("Preview finished")
                                                            : qsTr("Restored"))
                              : qsTr("It did not finish"))
                           : qsTr("Working")
                    subtitle: ImportController.finished ? "" : ImportController.stage
                }

                AppProgressBar {
                    Layout.fillWidth: true
                    visible: ImportController.running
                    value: ImportController.byteProgress
                    unknownTotal: ImportController.byteProgress === 0
                }

                Text {
                    visible: ImportController.running
                    Layout.fillWidth: true
                    text: ImportController.currentItem
                    color: Colors.textSecondary
                    font.family: Typography.monoFamily
                    font.pixelSize: Typography.caption
                    elide: Text.ElideMiddle
                }

                AppInlineMessage {
                    visible: ImportController.finished && ImportController.succeeded
                    tone: ImportController.wasDryRun ? "info" : "success"
                    title: ImportController.wasDryRun
                           ? qsTr("Nothing was changed")
                           : qsTr("Everything that could be restored, was")
                    body: ImportController.summaryText
                }

                AppInlineMessage {
                    visible: ImportController.finished && !ImportController.succeeded
                    tone: "error"
                    title: qsTr("The restore stopped")
                    body: ImportController.errorMessage
                }

                // Every restore leaves an undo point behind. Until now nothing
                // in the window said so, which meant Transmit was writing an
                // archive into someone's home that they had no way to use.
                AppCard {
                    Layout.fillWidth: true
                    visible: ImportController.canUndo || ImportController.undoing
                             || ImportController.undoSummary !== ""
                    implicitHeight: undoBox.implicitHeight + Spacing.xl * 2

                    ColumnLayout {
                        id: undoBox
                        anchors.fill: parent
                        anchors.margins: Spacing.xl
                        spacing: Spacing.md

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Spacing.sm

                            AppIcon {
                                name: "refresh"
                                color: Colors.textSecondary
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("This can be undone")
                                color: Colors.textPrimary
                                font.family: Typography.family
                                font.pixelSize: Typography.heading
                                font.weight: Typography.semiBold
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: ImportController.undoSummary === ""
                            text: ImportController.undoDescription
                            color: Colors.textSecondary
                            font.family: Typography.family
                            font.pixelSize: Typography.small
                            lineHeight: Typography.lineHeightNormal
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: ImportController.undoSummary !== ""
                            text: ImportController.undoSummary
                            color: Colors.textPrimary
                            font.family: Typography.family
                            font.pixelSize: Typography.small
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: ImportController.canUndo || ImportController.undoing
                            spacing: Spacing.md

                            AppSpinner {
                                running: ImportController.undoing
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Item { Layout.fillWidth: true }

                            AppButton {
                                text: qsTr("Undo this restore")
                                variant: "danger"
                                enabled: ImportController.canUndo
                                onClicked: page.dialogs.confirm({
                                    heading: qsTr("Undo the restore?"),
                                    body: ImportController.undoDescription,
                                    confirmText: qsTr("Undo"),
                                    cancelText: qsTr("Leave it"),
                                    destructive: true
                                }, () => ImportController.undoLastRestore())
                            }

                            AppButton {
                                text: qsTr("Keep it")
                                enabled: ImportController.canUndo
                                onClicked: ImportController.keepLastRestore()
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ------------------------------------------------------ footer
        RowLayout {
            Layout.fillWidth: true
            spacing: Spacing.md

            AppButton {
                text: qsTr("Back")
                variant: "ghost"
                visible: page.step > 0 && !ImportController.running
                onClicked: page.step = page.step - 1
            }

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("Cancel")
                variant: "danger"
                visible: ImportController.running
                onClicked: page.dialogs.confirm({
                    heading: qsTr("Stop the restore?"),
                    body: qsTr("Whatever has already been written stays where it is. You can "
                             + "start again, and files that are already back will be left "
                             + "alone or renamed according to the choice you made."),
                    confirmText: qsTr("Stop"),
                    cancelText: qsTr("Keep going"),
                    destructive: true
                }, () => ImportController.cancel())
            }

            AppButton {
                text: qsTr("Continue")
                variant: "primary"
                visible: page.step === 1
                enabled: ImportController.archiveUnlocked
                onClicked: page.step = 2
            }

            AppButton {
                text: qsTr("Preview")
                visible: page.step === 2
                onClicked: {
                    page.step = 3
                    ImportController.start(page.passphrase, page.conflictPolicy, true,
                                           page.verifyFirst,
                                           page.restoreToFolder ? page.destinationOverride : "")
                }
            }

            AppButton {
                text: qsTr("Restore")
                variant: "primary"
                visible: page.step === 2
                enabled: !page.restoreToFolder || page.destinationOverride !== ""

                // The one irreversible thing this application does, so it is
                // the one thing it asks about first.
                onClicked: page.dialogs.confirm({
                    heading: page.restoreToFolder
                             ? qsTr("Restore into the folder you chose?")
                             : qsTr("Restore onto this computer?"),
                    subheading: page.restoreToFolder
                                ? page.destinationOverride
                                : AppController.homeDirectory,
                    body: page.conflictPolicy === "overwrite"
                          ? qsTr("Files already there with the same name will be replaced, and "
                               + "what they contained now cannot be recovered.")
                          : qsTr("Transmit writes your files, program data and settings into "
                               + "place. Anything already there is handled the way you chose "
                               + "on the previous step."),
                    confirmText: qsTr("Restore"),
                    destructive: page.conflictPolicy === "overwrite"
                }, function () {
                    page.step = 3
                    ImportController.start(page.passphrase, page.conflictPolicy, false,
                                           page.verifyFirst,
                                           page.restoreToFolder ? page.destinationOverride : "")
                })
            }

            AppButton {
                text: qsTr("Go back and restore")
                variant: "primary"
                visible: page.step === 3 && ImportController.finished
                         && ImportController.wasDryRun && ImportController.succeeded
                onClicked: {
                    ImportController.reset()
                    page.step = 2
                }
            }

            AppButton {
                text: qsTr("See the full report")
                variant: ImportController.wasDryRun ? "secondary" : "primary"
                visible: page.step === 3 && ImportController.finished
                onClicked: {
                    ImportController.populateReport(page.reportModel)
                    AppController.currentPage = "report"
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Open a Transmit archive")
        nameFilters: [qsTr("Transmit archives (*.txa *.txa.001)"), qsTr("All files (*)")]
        onAccepted: {
            page.archivePath = AppController.fromFileUrl(selectedFile.toString())
            ImportController.inspect(page.archivePath, "")
            page.step = 1
        }
    }

    FolderDialog {
        id: destinationDialog
        title: qsTr("Choose where to restore")
        onAccepted: page.destinationOverride =
                        AppController.fromFileUrl(selectedFolder.toString())
    }
}
