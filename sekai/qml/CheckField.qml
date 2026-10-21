import QtQuick
import QtQuick.Controls
import "qrc:/qml/components" as UI
CheckBox {
    id: control
    implicitHeight: Math.max(40, contentItem.implicitHeight + 12)
    palette.windowText: UI.Theme.textPrimary
    palette.text: UI.Theme.textPrimary
    palette.base: UI.Theme.bgSurface
    palette.highlight: UI.Theme.accentPrimary
    contentItem: Text {
        text: control.text; font: control.font; textFormat: Text.PlainText
        color: control.enabled ? UI.Theme.textPrimary : UI.Theme.textMuted
        verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap
        leftPadding: control.indicator.width + control.spacing
    }
}
