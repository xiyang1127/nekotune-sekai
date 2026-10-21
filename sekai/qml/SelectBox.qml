import QtQuick
import QtQuick.Controls
import "qrc:/qml/components" as UI
ComboBox {
    id: control
    implicitHeight: 40
    palette.button: UI.Theme.bgSurface
    palette.base: UI.Theme.bgSurface
    palette.text: UI.Theme.textPrimary
    palette.buttonText: UI.Theme.textPrimary
    palette.window: UI.Theme.bgSurface
    palette.windowText: UI.Theme.textPrimary
    palette.highlight: UI.Theme.bgSelected
    palette.highlightedText: UI.Theme.textPrimary
    background: Rectangle {
        radius: UI.Theme.radiusSm; color: UI.Theme.bgSurface
        border.color: control.activeFocus ? UI.Theme.accentPrimary : UI.Theme.borderControl
        opacity: control.enabled ? 1 : 0.5
    }
}
