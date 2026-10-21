import QtQuick
import NekoTune 1.0
Item {
    objectName: "sekaiPluginPage"
    Controller { id: controller }
    Translator { id: pluginTranslator }
    ExtensionApi { id: api }
    MusicPanel {
        anchors.fill: parent; anchors.margins: 20
        client: controller; translator: pluginTranslator
        onSettingsRequested: api.window.navigate(api.ownerId + "/settings")
    }
}
