import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Transmit.Backend
import Transmit.Components
import Transmit.Theme

/// The capture wizard. Each step asks one thing, and the consequences of every
/// choice are stated next to it rather than hidden behind a help link.
Item {
    id: page

    /// Handed down by the shell rather than found through the scope chain, so
    /// moving a page cannot quietly break its bindings.
    required property var reportModel
    required property var dialogs

    property int step: 0
    property string profileId: "full"
    property string destinationFolder: ""
    property bool destinationRequiresSplit: false
    property string destinationLabel: ""
    property string preset: "maximum"
    property bool encrypt: false
    property string passphrase: ""
    property string passphraseConfirm: ""
    property string label: ""

    readonly property bool encryptionRequired: includeSecrets
    readonly property bool encryptionOn: encrypt || encryptionRequired
    readonly property bool passphraseValid:
        !encryptionOn || (passphrase.length >= 8 && passphrase === passphraseConfirm)

    property bool includeUserData: true
    property bool includeAppState: true
    property bool includeSettings: true
    property bool includeAppList: true
    property bool includeSecrets: false

    readonly property var selectedDomains: {
        const domains = []
        if (includeUserData) domains.push("userdata")
        if (includeAppState) domains.push("appstate")
        if (includeSettings)  domains.push("settings")
        if (includeAppList)   domains.push("apps")
        return domains
    }

    readonly property var stepTitles: [qsTr("What to take"), qsTr("Where to put it"),
                                       qsTr("Options"), qsTr("Progress")]

    function goTo(next) {
        step = next

        // The answer goes stale the moment the user closes something, so it is
        // asked afresh each time they arrive at the last step before Start.
        if (next === 2 && page.includeAppState) {
            ExportController.forgetRunningPrograms()
            ExportController.checkForRunningPrograms(page.profileId, page.selectedDomains)
        }
    }

    /// Makes the tick boxes agree with the profile just chosen. Without this,
    /// picking "Files only" would leave the other options still ticked.
    function adoptProfileDomains(id) {
        const domains = ExportController.domainsForProfile(id)
        includeUserData = domains.indexOf("userdata") >= 0
        includeAppState = domains.indexOf("appstate") >= 0
        includeSettings = domains.indexOf("settings") >= 0
        includeAppList  = domains.indexOf("apps") >= 0
    }

    Component.onCompleted: adoptProfileDomains(profileId)

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
            AppScrollView {
                id: profileScroller

                ColumnLayout {
                    width: profileScroller.availableWidth
                    spacing: Spacing.lg

                    AppSectionHeader {
                        title: qsTr("What should travel with you?")
                        subtitle: qsTr("Program files themselves cannot move between operating "
                                     + "systems, so Transmit carries their data and settings and "
                                     + "writes you a script that reinstalls the programs on the "
                                     + "other side.")
                    }

                    Repeater {
                        model: ProfileListModel {}

                        AppSelectableCard {
                            Layout.fillWidth: true
                            title: model.displayName
                            // Read through `model` rather than the injected
                            // context property: this card has a `description`
                            // of its own, and the bare name would bind it to
                            // itself.
                            description: model.description + "\n"
                                         + qsTr("Includes: %1").arg(model.domainSummary)
                            trailingText: model.sizeHint
                            selected: page.profileId === model.profileId
                            onClicked: {
                                page.profileId = model.profileId
                                page.adoptProfileDomains(model.profileId)
                            }
                        }
                    }
                    AppCard {
                        Layout.fillWidth: true
                        Layout.topMargin: Spacing.sm
                        implicitHeight: domainList.implicitHeight + Spacing.xl * 2

                        ColumnLayout {
                            id: domainList
                            anchors.fill: parent
                            anchors.margins: Spacing.xl
                            spacing: Spacing.md

                            Text {
                                text: qsTr("Adjust what that includes")
                                color: Colors.textPrimary
                                font.family: Typography.family
                                font.pixelSize: Typography.body
                                font.weight: Typography.semiBold
                            }

                            AppCheckRow {
                                label: qsTr("Your files")
                                description: qsTr("Documents, pictures, music, video, downloads.")
                                checked: page.includeUserData
                                onCheckedChanged: page.includeUserData = checked
                            }

                            AppCheckRow {
                                label: qsTr("Program data and settings")
                                description: qsTr("Browser profiles, editor configuration and the "
                                                + "like. Transmit puts each one where that program "
                                                + "looks for it on the new system, and corrects "
                                                + "the folder names recorded inside.")
                                checked: page.includeAppState
                                onCheckedChanged: page.includeAppState = checked
                            }

                            AppCheckRow {
                                label: qsTr("Desktop preferences")
                                description: qsTr("Appearance, wallpaper, language, time zone, "
                                                + "keyboard layouts and accessibility settings.")
                                checked: page.includeSettings
                                onCheckedChanged: page.includeSettings = checked
                            }

                            AppCheckRow {
                                label: qsTr("The list of programs you have installed")
                                description: qsTr("Programs cannot be copied between systems. "
                                                + "Transmit writes you a script that reinstalls "
                                                + "them, and never runs it itself.")
                                checked: page.includeAppList
                                onCheckedChanged: page.includeAppList = checked
                            }

                            AppCheckRow {
                                label: qsTr("Saved passwords")
                                description: ExportController.secretsAvailable()
                                             ? qsTr("Taken from %1. This needs a passphrase, and "
                                                  + "it means the drive will hold your passwords.")
                                               .arg(ExportController.secretsStoreName())
                                             : qsTr("This computer has no password store Transmit "
                                                  + "can read.")
                                checked: page.includeSecrets
                                enabledControl: ExportController.secretsAvailable()
                                                && AppController.encryptionAvailable
                                onCheckedChanged: page.includeSecrets = checked
                            }
                        }
                    }

                    AppInlineMessage {
                        visible: page.includeSecrets
                        tone: "warning"
                        title: qsTr("The drive will contain your passwords")
                        body: qsTr("Anyone who has the drive and the passphrase can read them. "
                                 + "Carry the two separately, and erase the drive once you have "
                                 + "finished with it.")
                    }
                }
            }

            // ---------------------------------------------------- step 2
            ColumnLayout {
                spacing: Spacing.lg

                AppSectionHeader {
                    title: qsTr("Where should the archive go?")
                    subtitle: qsTr("A USB drive is the usual choice. Transmit never writes to the "
                                 + "drive until you press Start on the last step.")
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
                        width: driveList.width
                        title: displayName
                        description: subtitle
                        badgeText: removable ? qsTr("Removable") : ""
                        badgeTone: removable ? "info" : "neutral"
                        selected: page.destinationFolder === rootPath
                        enabled: !readOnly
                        opacity: readOnly ? 0.55 : 1.0
                        onClicked: {
                            page.destinationFolder = rootPath
                            page.destinationRequiresSplit = requiresSplitting
                            page.destinationLabel = displayName
                        }
                    }

                    AppEmptyState {
                        anchors.centerIn: parent
                        width: parent.width * 0.7
                        // "None" is only true once the first scan has answered;
                        // saying it before that is a lie that corrects itself.
                        visible: driveList.count === 0 && !drives.refreshing
                        title: qsTr("No drives found")
                        body: qsTr("Plug in a USB drive, or choose a folder below.")
                    }

                    AppSpinner {
                        anchors.centerIn: parent
                        running: driveList.count === 0 && drives.refreshing
                        size: Sizing.iconSizeLarge
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Spacing.md

                    AppButton {
                        text: qsTr("Choose a folder instead…")
                        onClicked: folderDialog.open()
                    }

                    Text {
                        Layout.fillWidth: true
                        text: page.destinationFolder === ""
                              ? qsTr("Nothing chosen yet")
                              : qsTr("Writing to %1").arg(page.destinationFolder)
                        color: Colors.textSecondary
                        font.family: Typography.family
                        font.pixelSize: Typography.small
                        elide: Text.ElideMiddle
                    }
                }

                AppInlineMessage {
                    visible: page.destinationRequiresSplit
                    tone: "info"
                    title: qsTr("This drive will get the archive in parts")
                    body: qsTr("It is formatted so that no single file can be 4 GB or larger, so "
                             + "Transmit writes numbered parts instead. Keep them together - the "
                             + "restore needs all of them.")
                }
            }

            // ---------------------------------------------------- step 3
            AppScrollView {
                id: optionScroller

                ColumnLayout {
                    width: optionScroller.availableWidth
                    spacing: Spacing.lg

                    AppSectionHeader {
                        title: qsTr("How should it be packed?")
                    }

                    // Asked here rather than reported afterwards: being told at
                    // the end of a long capture to close a program and start
                    // again is not a warning, it is a waste of the wait.
                    AppCard {
                        Layout.fillWidth: true
                        visible: ExportController.checkingPrograms
                                 || ExportController.programsToClose.length > 0
                        implicitHeight: quiesce.implicitHeight + Spacing.xl * 2

                        ColumnLayout {
                            id: quiesce
                            anchors.fill: parent
                            anchors.margins: Spacing.xl
                            spacing: Spacing.md

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Spacing.sm

                                AppSpinner {
                                    running: ExportController.checkingPrograms
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                AppIcon {
                                    name: "alert"
                                    color: Colors.warning
                                    visible: !ExportController.checkingPrograms
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: ExportController.checkingPrograms
                                          ? qsTr("Checking what is running…")
                                          : qsTr("Close these before you start")
                                    color: Colors.textPrimary
                                    font.family: Typography.family
                                    font.pixelSize: Typography.heading
                                    font.weight: Typography.semiBold
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: !ExportController.checkingPrograms
                                text: qsTr("These programs are running and have their data open. "
                                         + "Transmit can still copy it, but a file written while "
                                         + "it is being read may arrive half-finished.")
                                color: Colors.textSecondary
                                font.family: Typography.family
                                font.pixelSize: Typography.small
                                lineHeight: Typography.lineHeightNormal
                                wrapMode: Text.WordWrap
                            }

                            Repeater {
                                model: ExportController.programsToClose

                                RowLayout {
                                    id: programRow

                                    required property string modelData

                                    Layout.fillWidth: true
                                    spacing: Spacing.sm

                                    Text {
                                        text: "\u2022"
                                        color: Colors.textSecondary
                                        font.family: Typography.family
                                        font.pixelSize: Typography.body
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: programRow.modelData
                                        color: Colors.textPrimary
                                        font.family: Typography.family
                                        font.pixelSize: Typography.body
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            AppButton {
                                text: qsTr("Check again")
                                enabled: !ExportController.checkingPrograms
                                onClicked: {
                                    ExportController.forgetRunningPrograms()
                                    ExportController.checkForRunningPrograms(
                                        page.profileId, page.selectedDomains)
                                }
                            }
                        }
                    }

                    AppLabelledField {
                        Layout.fillWidth: true
                        label: qsTr("Name this archive")
                        helperText: qsTr("Shown when you open it later. Optional.")

                        AppTextField {
                            placeholderText: qsTr("for example: work laptop, before the move")
                            text: page.label
                            onTextChanged: page.label = text
                        }
                    }

                    AppLabelledField {
                        Layout.fillWidth: true
                        label: qsTr("Compression")
                        helperText: qsTr("Maximum is the default: close to the smallest possible "
                                       + "size, and still quick to restore. Extreme squeezes out "
                                       + "a few percent more but takes considerably longer.")

                        AppComboBox {
                            id: presetBox
                            model: [
                                { value: "fast",     label: qsTr("Fast - largest file, quickest to write") },
                                { value: "balanced", label: qsTr("Balanced") },
                                { value: "maximum",  label: qsTr("Maximum - recommended") },
                                { value: "extreme",  label: qsTr("Extreme - smallest file, slowest") }
                            ]
                            textRole: "label"
                            valueRole: "value"
                            currentIndex: 2
                            onActivated: page.preset = currentValue
                            Accessible.name: qsTr("Compression level")
                        }
                    }

                    AppCheckRow {
                        label: qsTr("Protect this archive with a passphrase")
                        description: page.includeSecrets
                                     ? qsTr("Required, because you chose to include saved "
                                          + "passwords.")
                                     : qsTr("Encrypts everything, including the file names. "
                                          + "Without the passphrase the archive cannot be opened - "
                                          + "there is no way to recover it.")
                        checked: page.encrypt || page.includeSecrets
                        enabledControl: AppController.encryptionAvailable && !page.includeSecrets
                        onCheckedChanged: page.encrypt = checked
                    }

                    AppLabelledField {
                        Layout.fillWidth: true
                        visible: page.encryptionOn
                        label: qsTr("Passphrase")
                        helperText: page.passphrase.length > 0 && page.passphrase.length < 8
                                    ? qsTr("Use at least 8 characters.")
                                    : qsTr("A memorable sentence works better than a short "
                                         + "complicated word.")
                        error: page.passphrase.length > 0 && page.passphrase.length < 8

                        AppTextField {
                            echoMode: TextInput.Password
                            text: page.passphrase
                            onTextChanged: page.passphrase = text
                        }
                    }

                    AppLabelledField {
                        Layout.fillWidth: true
                        visible: page.encryptionOn
                        label: qsTr("Passphrase again")
                        helperText: page.passphraseConfirm.length > 0
                                    && page.passphrase !== page.passphraseConfirm
                                    ? qsTr("These do not match.") : ""
                        error: page.passphraseConfirm.length > 0
                               && page.passphrase !== page.passphraseConfirm

                        AppTextField {
                            echoMode: TextInput.Password
                            text: page.passphraseConfirm
                            onTextChanged: page.passphraseConfirm = text
                        }
                    }
                }
            }

            // ---------------------------------------------------- step 4
            ColumnLayout {
                spacing: Spacing.lg

                AppSectionHeader {
                    title: ExportController.finished
                           ? (ExportController.succeeded ? qsTr("Done") : qsTr("It did not finish"))
                           : qsTr("Working")
                    subtitle: ExportController.finished ? "" : ExportController.stage
                }

                AppProgressBar {
                    Layout.fillWidth: true
                    visible: ExportController.running
                    value: ExportController.byteProgress
                    unknownTotal: ExportController.byteProgress === 0
                }

                Text {
                    visible: ExportController.running
                    Layout.fillWidth: true
                    text: ExportController.currentItem
                    color: Colors.textSecondary
                    font.family: Typography.monoFamily
                    font.pixelSize: Typography.caption
                    elide: Text.ElideMiddle
                }

                GridLayout {
                    visible: ExportController.running
                    columns: 2
                    columnSpacing: Spacing.xl
                    Layout.fillWidth: true

                    AppKeyValue { label: qsTr("Read");    value: ExportController.bytesReadText }
                    AppKeyValue { label: qsTr("Written"); value: ExportController.bytesWrittenText }
                    AppKeyValue {
                        label: qsTr("Compressed to")
                        value: ExportController.compressionText
                        visible: ExportController.compressionText !== ""
                    }
                    AppKeyValue {
                        label: qsTr("Time left")
                        value: ExportController.etaText
                        visible: ExportController.etaText !== ""
                    }
                }

                AppInlineMessage {
                    visible: ExportController.finished && ExportController.succeeded
                    tone: "success"
                    title: qsTr("Your computer is on the drive")
                    body: ExportController.summaryText
                }

                AppInlineMessage {
                    visible: ExportController.finished && ExportController.succeeded
                             && ExportController.incomplete
                    tone: "warning"
                    title: qsTr("Some folders could not be opened")
                    body: ExportController.incompleteText
                }

                AppInlineMessage {
                    visible: ExportController.finished && !ExportController.succeeded
                    tone: "error"
                    title: qsTr("The capture stopped")
                    body: ExportController.errorMessage
                }

                ColumnLayout {
                    visible: ExportController.finished && ExportController.succeeded
                    Layout.fillWidth: true
                    spacing: Spacing.xs

                    Text {
                        text: qsTr("Files written")
                        color: Colors.textSecondary
                        font.family: Typography.family
                        font.pixelSize: Typography.small
                        font.weight: Typography.medium
                    }

                    Repeater {
                        model: ExportController.archiveParts

                        Text {
                            text: modelData
                            Layout.fillWidth: true
                            color: Colors.textPrimary
                            font.family: Typography.monoFamily
                            font.pixelSize: Typography.caption
                            elide: Text.ElideMiddle
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
                visible: page.step > 0 && !ExportController.running
                          && !(page.step === 3 && ExportController.finished)
                onClicked: page.goTo(page.step - 1)
            }

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("Cancel")
                variant: "danger"
                visible: ExportController.running
                onClicked: page.dialogs.confirm({
                    heading: qsTr("Stop the capture?"),
                    body: qsTr("The partly written archive is removed - an archive that stops "
                             + "half way through cannot be restored from, so leaving it behind "
                             + "would only be misleading."),
                    confirmText: qsTr("Stop"),
                    cancelText: qsTr("Keep going"),
                    destructive: true
                }, () => ExportController.cancel())
            }

            AppButton {
                text: qsTr("Continue")
                variant: "primary"
                visible: page.step < 2
                enabled: page.step !== 1 || page.destinationFolder !== ""
                onClicked: page.goTo(page.step + 1)
            }

            AppButton {
                text: qsTr("Start")
                variant: "primary"
                visible: page.step === 2
                enabled: page.passphraseValid
                onClicked: {
                    page.goTo(3)
                    ExportController.start(page.profileId, page.destinationFolder, page.preset,
                                           page.encryptionOn ? page.passphrase : "",
                                           page.destinationRequiresSplit, page.label,
                                           page.selectedDomains, page.includeSecrets)
                }
            }

            AppButton {
                text: qsTr("Show the drive")
                visible: page.step === 3 && ExportController.finished
                         && ExportController.succeeded
                onClicked: AppController.revealInFileManager(page.destinationFolder)
            }

            AppButton {
                text: qsTr("See the full report")
                variant: "primary"
                visible: page.step === 3 && ExportController.finished
                onClicked: {
                    ExportController.populateReport(page.reportModel)
                    AppController.currentPage = "report"
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose where to write the archive")
        onAccepted: {
            page.destinationFolder = AppController.fromFileUrl(selectedFolder.toString())
            page.destinationRequiresSplit = false
            page.destinationLabel = page.destinationFolder
        }
    }
}
