pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/qml/components" as UI
Item {
    id: root
    required property var client
    required property var translator
    property var draft: ({})
    property bool saving: false
    property bool failed: false
    property bool advanced: false
    property string message: ""
    readonly property bool dirty: JSON.stringify(draft) !== JSON.stringify(client.status)
    function t(key) { return translator.text(key, translator.language) }
    function set(key, value) { const next = Object.assign({}, draft); next[key] = value; draft = next }
    function sync() { draft = Object.assign({}, client.status) }
    Connections {
        target: root.client
        function onReadyChanged() { if (root.client.ready) root.sync() }
    }
    Component.onCompleted: sync()
    ScrollView {
        id: scroll; anchors.fill: parent; contentWidth: availableWidth; clip: true
        ColumnLayout {
            width: scroll.availableWidth; spacing: 12
            Label { text: root.t("settings"); color: UI.Theme.textPrimary; font.pixelSize: 24; font.bold: true }
            Label { Layout.fillWidth: true; text: root.t("settings_help"); wrapMode: Text.WordWrap; color: UI.Theme.textMuted }
            GridLayout {
                Layout.fillWidth: true; columns: 2; columnSpacing: 12; rowSpacing: 8
                Label { text: root.t("default_region"); color: UI.Theme.textSecondary }
                SelectBox {
                    objectName: "sekaiDefaultRegion"; Layout.fillWidth: true
                    model: ["jp", "en", "cn", "tw", "kr"]
                    currentIndex: Math.max(0, model.indexOf(root.draft.region))
                    enabled: root.client.ready && !root.saving
                    displayText: root.t("region_" + currentText)
                    onActivated: root.set("region", currentText)
                    Accessible.name: root.t("default_region")
                }
                Label { text: root.t("format"); color: UI.Theme.textSecondary }
                SelectBox {
                    objectName: "sekaiFormat"; Layout.fillWidth: true
                    model: ["mp3", "flac", "wav"]; currentIndex: Math.max(0, model.indexOf(root.draft.format))
                    enabled: root.client.ready && !root.saving; onActivated: root.set("format", currentText)
                    Accessible.name: root.t("format")
                }
                Label { text: root.t("length"); color: UI.Theme.textSecondary }
                SelectBox {
                    objectName: "sekaiLength"; Layout.fillWidth: true
                    model: ["long", "short"]; currentIndex: Math.max(0, model.indexOf(root.draft.length))
                    displayText: root.t("length_" + currentText)
                    delegate: ItemDelegate {
                        required property string modelData
                        palette.windowText: UI.Theme.textPrimary
                        text: root.t("length_" + modelData)
                    }
                    enabled: root.client.ready && !root.saving; onActivated: root.set("length", currentText)
                    Accessible.name: root.t("length")
                }
            }
            CheckField {
                Layout.fillWidth: true
                objectName: "sekaiFallback"; text: root.t("fallback_jp")
                checked: root.draft.fallback_jp === true; enabled: root.client.ready && !root.saving
                onToggled: root.set("fallback_jp", checked)
            }
            Label { Layout.fillWidth: true; text: root.t("audio_help"); wrapMode: Text.WordWrap; color: UI.Theme.textMuted }
            CheckField {
                Layout.fillWidth: true
                objectName: "sekaiTrimDownloads"; text: root.t("trim_downloads")
                checked: root.draft.trim_downloads !== false; enabled: root.client.ready && !root.saving
                onToggled: root.set("trim_downloads", checked)
            }
            Label { Layout.fillWidth: true; text: root.t("trim_help"); wrapMode: Text.WordWrap; color: UI.Theme.textMuted }
            UI.TextButton {
                objectName: "sekaiAdvanced"; text: root.t(root.advanced ? "hide_advanced" : "advanced"); subtle: true
                onClicked: root.advanced = !root.advanced
            }
            ColumnLayout {
                Layout.fillWidth: true; visible: root.advanced; spacing: 12
                Label { Layout.fillWidth: true; text: root.t("advanced_help"); wrapMode: Text.WordWrap; color: UI.Theme.textMuted }
                Repeater {
                    model: ["master_url", "asset_url", "cover_url"]
                    ColumnLayout {
                        required property string modelData
                        id: urlRow
                        Layout.fillWidth: true; spacing: 4
                        Label { text: root.t(urlRow.modelData); color: UI.Theme.textSecondary }
                        UI.InputField {
                            objectName: "sekai_" + urlRow.modelData
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            text: root.draft[urlRow.modelData] || ""; placeholderText: root.t("default_url")
                            enabled: root.client.ready && !root.saving; selectByMouse: true
                            Accessible.name: root.t(urlRow.modelData)
                            onTextEdited: root.set(urlRow.modelData, text)
                        }
                    }
                }
                UI.TextButton {
                    objectName: "sekaiDefaults"; text: root.t("use_defaults"); subtle: true
                    enabled: root.client.ready && !root.saving
                    onClicked: root.client.api.invoke("defaults").then(value => {
                        root.draft = Object.assign({}, root.draft, value)
                    }).catch(error => { root.failed = true; root.message = root.client.errorKey(error) })
                }
            }
            UI.TextButton {
                objectName: "sekaiSave"; text: root.t(root.saving ? "saving" : "save")
                enabled: root.client.ready && !root.saving && root.dirty
                onClicked: {
                    root.saving = true; root.failed = false; root.message = ""
                    root.client.api.invoke("config.set", root.draft).then(value => {
                        root.client.acceptStatus(value); root.sync(); root.message = "saved"; root.saving = false
                    }).catch(error => {
                        root.message = root.client.errorKey(error); root.failed = true; root.saving = false
                    })
                }
            }
            Label {
                Layout.fillWidth: true; visible: Boolean(root.message || root.client.message)
                text: root.t(root.message || root.client.message); textFormat: Text.PlainText; wrapMode: Text.WordWrap
                color: root.failed || root.client.failed ? UI.Theme.statusError : UI.Theme.statusSuccess
            }
        }
    }
}
