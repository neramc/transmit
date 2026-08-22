import QtQuick
import Transmit.Theme

/// A small status chip. Used for continuity grades, where colour alone would
/// not be enough - the text always says which grade it is.
Rectangle {
    id: badge

    /// "neutral", "info", "success", "warning" or "error"
    property string tone: "neutral"
    property string text: ""

    implicitWidth: label.implicitWidth + Spacing.md * 2
    implicitHeight: 22
    radius: Radius.pill

    color: tone === "success" ? Colors.successSubtle
         : tone === "warning" ? Colors.warningSubtle
         : tone === "error"   ? Colors.errorSubtle
         : tone === "info"    ? Colors.infoSubtle
                              : Colors.surfaceSunken

    Text {
        id: label
        anchors.centerIn: parent
        text: badge.text
        font.family: Typography.family
        font.pixelSize: Typography.caption
        font.weight: Typography.medium
        color: badge.tone === "success" ? Colors.success
             : badge.tone === "warning" ? Colors.warning
             : badge.tone === "error"   ? Colors.error
             : badge.tone === "info"    ? Colors.info
                                        : Colors.textSecondary
    }
}
