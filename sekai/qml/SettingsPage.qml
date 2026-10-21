import QtQuick
Item {
    objectName: "sekaiPluginSettings"
    Controller { id: controller }
    Translator { id: pluginTranslator }
    SettingsPanel { anchors.fill: parent; client: controller; translator: pluginTranslator }
}
