import QtQml
import QtQuick
import QtQuick.Controls.Basic
import Transmit.Backend
import Transmit.Layouts
import Transmit.Theme

/// The window itself: its size, the keyboard shortcuts, and the two pieces of
/// theme state the design system reads. Everything inside the frame is
/// arranged by AppShell.
ApplicationWindow {
    id: window

    width: 1040
    height: 720
    minimumWidth: 820
    minimumHeight: 560
    visible: true
    title: qsTr("Transmit")
    color: Colors.background

    /// The report is filled by whichever run finished last, so both wizards
    /// hand off to the same view.
    property alias reportModel: report

    ContinuityReportModel { id: report }

    // ThemeState is written from exactly one place - here - so no component
    // has to know how the preference is stored.
    Component.onCompleted: {
        ThemeState.mode = AppController.themeMode
        Motion.reduced = AppController.reduceMotion

        // Asked once a day at most, and not at all when the preference says
        // so. A critical fix found here installs itself; everything else
        // waits in the settings page until somebody looks.
        UpdateController.checkQuietly()
    }

    Connections {
        target: AppController
        function onThemeModeChanged() { ThemeState.mode = AppController.themeMode }
        function onReduceMotionChanged() { Motion.reduced = AppController.reduceMotion }
    }

    // Qt's palette follows the desktop's colour scheme, which is the most
    // portable way to tell whether the user is in dark mode.
    Binding {
        target: ThemeState
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
    // The palette. StandardKey has no entry for it, and every application
    // that has one uses the same chord on every platform.
    Shortcut {
        sequences: ["Ctrl+K"]
        onActivated: shell.openCommandPalette()
    }

    // Ctrl+B on Windows and Linux, Cmd+B on macOS - the convention every
    // editor with a sidebar uses. There is no StandardKey for it.
    Shortcut {
        sequences: ["Ctrl+B"]
        onActivated: shell.toggleSidebar()
    }
    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: if (AppController.currentPage !== "home")
                         AppController.currentPage = "home"
    }
    Shortcut {
        sequence: "F6"
        onActivated: shell.forceActiveFocus()
    }
    // Ctrl+1..5 jumps straight to a destination, the way a tabbed application
    // does - navigation should never need the mouse.
    Instantiator {
        model: shell.navigation
        delegate: Shortcut {
            required property var modelData
            required property int index
            sequence: "Ctrl+" + (index + 1)
            onActivated: AppController.currentPage = modelData.page
        }
    }

    AppShell {
        id: shell
        anchors.fill: parent
        reportModel: window.reportModel
    }
}
