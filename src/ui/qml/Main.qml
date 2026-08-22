import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Transmit.Backend
import Transmit.Components
import Transmit.Pages
import Transmit.Theme

ApplicationWindow {
    id: window

    width: 1040
    height: 720
    minimumWidth: 820
    minimumHeight: 560
    visible: true
    title: qsTr("Transmit")
    color: Theme.background

    /// The report is filled by whichever run finished last, so both wizards
    /// hand off to the same view.
    property alias reportModel: report

    ContinuityReportModel { id: report }

    Component.onCompleted: Theme.mode = AppController.themeMode

    Connections {
        target: AppController
        function onThemeModeChanged() { Theme.mode = AppController.themeMode }
    }

    // Qt's palette follows the desktop's colour scheme, which is the most
    // portable way to tell whether the user is in dark mode.
    Binding {
        target: Theme
        property: "systemPrefersDark"
        value: window.palette.window.hslLightness < 0.5
    }

    // Platform conventions are respected by using StandardKey rather than
    // hard-coded modifiers: this is Command on macOS and Control elsewhere.
    Shortcut {
        sequences: [StandardKey.Open]
        onActivated: AppController.currentPage = "import"
    }
    Shortcut {
        sequences: [StandardKey.Save]
        onActivated: AppController.currentPage = "export"
    }
    Shortcut {
        sequences: [StandardKey.Preferences, "Ctrl+,"]
        onActivated: AppController.currentPage = "settings"
    }
    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: if (AppController.currentPage !== "home")
                         AppController.currentPage = "home"
    }
    Shortcut {
        sequence: "F6"
        onActivated: contentArea.forceActiveFocus()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        AppSidebar {
            Layout.fillHeight: true
            currentPage: AppController.currentPage
            entries: [
                { page: "home",     label: qsTr("Start") },
                { page: "export",   label: qsTr("Save this computer") },
                { page: "import",   label: qsTr("Bring one here") },
                { page: "report",   label: qsTr("Report") },
                { page: "settings", label: qsTr("Settings") }
            ]
            onNavigate: (page) => AppController.currentPage = page
        }

        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            StackLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacing2xl
                currentIndex: {
                    switch (AppController.currentPage) {
                    case "export":   return 1
                    case "import":   return 2
                    case "report":   return 3
                    case "settings": return 4
                    default:         return 0
                    }
                }

                HomePage {}
                ExportPage {}
                ImportPage {}
                ReportPage {}
                SettingsPage {}
            }

            // A gentle cross-fade makes navigation feel deliberate without
            // making the user wait for it.
            Behavior on opacity {
                NumberAnimation { duration: Theme.durationPanel; easing.type: Theme.easing }
            }
        }
    }
}
