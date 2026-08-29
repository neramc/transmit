import QtQuick
import QtQuick.Layouts
import Transmit.Backend
import Transmit.Components
import Transmit.Dialogs
import Transmit.Pages
import Transmit.Theme

/// The window's furniture: navigation down the left, a bar naming where you
/// are, the page itself, and a status line that keeps reporting a capture even
/// after you have wandered off to look at something else.
///
/// Main.qml owns the window and the shortcuts; everything inside the frame is
/// arranged here.
Item {
    id: shell
    objectName: "appShell"

    property var reportModel: null

    /// Whether the sidebar is showing icons only. Section 32 asks for the
    /// navigation to collapse on its own once the window is small; below that
    /// it is the user's choice, remembered by Ctrl+B.
    property bool sidebarCollapsed: false
    readonly property bool sidebarCompact: sidebarCollapsed || width < Sizing.smallWindow

    function toggleSidebar() {
        // A window narrow enough to force the collapse cannot be expanded out
        // of it; saying so is better than a shortcut that silently does
        // nothing.
        if (width >= Sizing.smallWindow)
            sidebarCollapsed = !sidebarCollapsed;
    }

    /// Exposed so the window can put a dialog in front of the whole shell.
    readonly property alias dialogs: dialogHost

    readonly property var navigation: [
        { page: "home",     icon: "home",     label: qsTr("Start"),
          title: qsTr("Transmit"),
          subtitle: qsTr("Carry this computer's setup to one running another system") },
        { page: "export",   icon: "upload",   label: qsTr("Save this computer"),
          title: qsTr("Save this computer"),
          subtitle: qsTr("Pack your files, settings and program data onto a drive") },
        { page: "import",   icon: "download", label: qsTr("Bring one here"),
          title: qsTr("Bring a computer here"),
          subtitle: qsTr("Open an archive and put it back on this machine") },
        { page: "report",   icon: "list",     label: qsTr("Report"),
          title: qsTr("Report"),
          subtitle: qsTr("What came across, what was adapted, and what could not") },
        { page: "settings", icon: "sliders",  label: qsTr("Settings"),
          title: qsTr("Settings"), subtitle: "" }
    ]

    readonly property var currentEntry: {
        for (let i = 0; i < navigation.length; ++i)
            if (navigation[i].page === AppController.currentPage)
                return navigation[i];
        return navigation[0];
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        AppSidebar {
            id: sidebar
            // Named so the interface tests can measure it: whether the
            // sidebar really got narrower is not something a property called
            // "compact" can prove on its own.
            objectName: "sidebar"
            Layout.fillHeight: true
            currentPage: AppController.currentPage
            compact: shell.sidebarCompact
            entries: shell.navigation
            onNavigate: (page) => AppController.currentPage = page
            onToggleRequested: shell.toggleSidebar()
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            AppToolBar {
                Layout.fillWidth: true
                title: shell.currentEntry.title
                subtitle: shell.currentEntry.subtitle
                actions: [
                    AppIconButton {
                        glyph: Colors.dark ? "sun" : "moon"
                        text: Colors.dark ? qsTr("Switch to the light theme")
                                          : qsTr("Switch to the dark theme")
                        tooltip: text
                        onClicked: AppController.themeMode = Colors.dark ? "light" : "dark"
                    }
                ]
            }

            ContentView {
                id: content
                objectName: "contentView"
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentPage: AppController.currentPage
                pages: [
                    { page: "home",     component: homePage },
                    { page: "export",   component: exportPage },
                    { page: "import",   component: importPage },
                    { page: "report",   component: reportPage },
                    { page: "settings", component: settingsPage }
                ]
            }

            AppStatusBar {
                Layout.fillWidth: true
                busy: ExportController.running || ImportController.running
                progress: ExportController.running ? ExportController.byteProgress
                        : ImportController.running ? ImportController.byteProgress : 0
                detail: ExportController.running ? ExportController.currentItem
                      : ImportController.running ? ImportController.currentItem : ""
                message: ExportController.running ? ExportController.stage
                       : ImportController.running ? ImportController.stage
                                                  : qsTr("Ready")
            }
        }
    }

    DialogHost { id: dialogHost; objectName: "dialogHost" }

    // Components rather than instances: ContentView builds each one the first
    // time its page is opened.
    Component { id: homePage;     HomePage {} }
    Component { id: exportPage;   ExportPage { dialogs: shell.dialogs; reportModel: shell.reportModel } }
    Component { id: importPage;   ImportPage { dialogs: shell.dialogs; reportModel: shell.reportModel } }
    Component { id: reportPage;   ReportPage { reportModel: shell.reportModel } }
    Component { id: settingsPage; SettingsPage {} }
}
