import QtQuick
// Overlay - the dimmed backdrop a modal sits on - lives in QtQuick.Controls
// rather than in a style, so this file imports the module itself. The style is
// still Basic: main.cpp sets it before the engine loads anything.
import QtQuick.Controls
import QtQuick.Layouts
import Transmit.Theme

/// Everything the application can do, one keystroke away.
///
/// docs/design.md section 16. It matters more here than in most applications
/// because the things Transmit does are buried two or three steps into a
/// wizard, and somebody who already knows what they want should not have to
/// walk there.
///
/// The match is a subsequence rather than a substring, which is what people
/// mean by fuzzy search: "svp" finds "Save this computer". Results are ordered
/// by how tightly the letters sit together, so an exact prefix beats letters
/// scattered across a sentence.
Popup {
    id: palette

    /// [{ id, label, category, shortcut, action }]
    property var commands: []

    /// Command ids, most recent first. Shown when nothing has been typed,
    /// because the thing somebody wants next is usually the thing they wanted
    /// last.
    property var recent: []

    readonly property int maximumResults: 8

    signal commandTriggered(string id)

    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    parent: Overlay.overlay
    width: Math.min(parent ? parent.width - Spacing.s48 * 2 : Sizing.maxTextWidth,
                    Sizing.maxTextWidth)
    x: parent ? (parent.width - width) / 2 : 0

    // Nearer the top than the middle. A palette that opens under the pointer's
    // resting place makes the list jump about as it filters; one anchored high
    // keeps the first result where the eye already is.
    y: parent ? Math.min(parent.height * 0.18, Spacing.s64 * 2) : 0

    Overlay.modal: Rectangle { color: Colors.overlay }

    background: Rectangle {
        radius: Radius.dialog
        color: Colors.surfaceElevated
        border.width: Elevation.borderWidth
        border.color: Colors.border
    }

    onOpened: {
        query.text = "";
        query.forceActiveFocus();
        results.currentIndex = 0;
    }

    /// Subsequence match. Returns a score, or -1 when the letters are not all
    /// there in order.
    function score(text, needle) {
        if (needle === "")
            return 0;
        const haystack = text.toLowerCase();
        const wanted = needle.toLowerCase();
        let at = 0;
        let previous = -1;
        let gaps = 0;
        for (let i = 0; i < wanted.length; ++i) {
            const found = haystack.indexOf(wanted[i], at);
            if (found < 0)
                return -1;
            if (previous >= 0)
                gaps += found - previous - 1;
            previous = found;
            at = found + 1;
        }
        // Earlier and tighter is better; a match at the very start best of all.
        return gaps * 2 + haystack.indexOf(wanted[0]);
    }

    readonly property var matches: {
        const text = query.text.trim();
        if (text === "") {
            const byId = {};
            for (const command of palette.commands)
                byId[command.id] = command;
            const recentOnes = [];
            for (const id of palette.recent)
                if (byId[id] !== undefined)
                    recentOnes.push(byId[id]);
            const rest = palette.commands.filter(c => palette.recent.indexOf(c.id) < 0);
            return recentOnes.concat(rest).slice(0, palette.maximumResults);
        }

        const scored = [];
        for (const command of palette.commands) {
            const value = palette.score(command.label + " " + (command.category || ""), text);
            if (value >= 0)
                scored.push({ command: command, value: value });
        }
        scored.sort((a, b) => a.value - b.value);
        return scored.slice(0, palette.maximumResults).map(entry => entry.command);
    }

    function activate(index) {
        const chosen = palette.matches[index];
        if (chosen === undefined)
            return;
        palette.close();
        palette.commandTriggered(chosen.id);
        if (chosen.action)
            chosen.action();
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Spacing.s16
            spacing: Spacing.s12

            AppIcon {
                name: "list"
                size: Sizing.iconSizeMedium
                color: Colors.textSecondary
            }

            TextInput {
                id: query
                Layout.fillWidth: true
                color: Colors.textPrimary
                font.family: Typography.family
                font.pixelSize: Typography.sectionTitle
                selectByMouse: true
                selectionColor: Colors.accent
                selectedTextColor: Colors.textOnAccent

                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Search commands")

                Text {
                    anchors.fill: parent
                    visible: query.text === ""
                    text: qsTr("Search commands…")
                    color: Colors.textDisabled
                    font: query.font
                    verticalAlignment: Text.AlignVCenter
                }

                Keys.onDownPressed: results.currentIndex =
                    Math.min(results.currentIndex + 1, palette.matches.length - 1)
                Keys.onUpPressed: results.currentIndex = Math.max(results.currentIndex - 1, 0)
                Keys.onReturnPressed: palette.activate(results.currentIndex)
                Keys.onEnterPressed: palette.activate(results.currentIndex)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Elevation.borderWidth
            color: Colors.border
        }

        ListView {
            id: results
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Sizing.rowHeightComfortable * 8)
            Layout.margins: Spacing.s8
            clip: true
            currentIndex: 0
            interactive: contentHeight > height

            model: palette.matches

            delegate: Rectangle {
                id: row

                required property int index
                required property var modelData

                width: results.width
                height: Sizing.rowHeightComfortable
                radius: Radius.control
                color: results.currentIndex === row.index ? Colors.accentSubtle
                     : rowHover.hovered ? Colors.surfaceHover : "transparent"

                Accessible.role: Accessible.ListItem
                Accessible.name: row.modelData.label

                HoverHandler {
                    id: rowHover
                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: if (hovered) results.currentIndex = row.index
                }
                TapHandler { onTapped: palette.activate(row.index) }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Spacing.s12
                    anchors.rightMargin: Spacing.s12
                    spacing: Spacing.s12

                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.label
                        elide: Text.ElideRight
                        color: Colors.textPrimary
                        font.family: Typography.family
                        font.pixelSize: Typography.body
                    }

                    Text {
                        visible: row.modelData.category !== undefined
                        text: row.modelData.category || ""
                        color: Colors.textSecondary
                        font.family: Typography.family
                        font.pixelSize: Typography.caption
                    }

                    Text {
                        visible: row.modelData.shortcut !== undefined
                        text: row.modelData.shortcut || ""
                        color: Colors.textDisabled
                        font.family: Typography.monoFamily
                        font.pixelSize: Typography.caption
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.margins: Spacing.s16
            visible: palette.matches.length === 0
            text: qsTr("Nothing matches “%1”.").arg(query.text)
            color: Colors.textSecondary
            font.family: Typography.family
            font.pixelSize: Typography.secondary
        }
    }
}
