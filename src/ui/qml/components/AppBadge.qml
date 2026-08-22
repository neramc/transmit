import QtQuick
import Transmit.Theme

/// A small status chip. Used for continuity grades, where colour alone would
/// not be enough - the text always says which grade it is.
Rectangle {
    id: badge

    /// "neutral", "info", "success", "warning" or "error"
    property string tone: "neutral"
    property string text: ""

    implicitWidth: label.implicitWidth + Theme.spacingMd * 2
    implicitHeight: 22
    radius: Theme.radiusPill

    color: tone === "success" ? Theme.successSubtle
         : tone === "warning" ? Theme.warningSubtle
         : tone === "error"   ? Theme.errorSubtle
         : tone === "info"    ? Theme.infoSubtle
                              : Theme.surfaceSunken

    Text {
        id: label
        anchors.centerIn: parent
        text: badge.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCaption
        font.weight: Theme.weightMedium
        color: badge.tone === "success" ? Theme.success
             : badge.tone === "warning" ? Theme.warning
             : badge.tone === "error"   ? Theme.error
             : badge.tone === "info"    ? Theme.info
                                        : Theme.textSecondary
    }
}
