pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/qml/components" as UI
Item {
    id: root
    required property var client
    required property var translator
    property string region: client.status.region || "jp"
    property string vocalType: ""
    readonly property bool available: client.ready
    signal settingsRequested()
    function t(key) { return translator.text(key, translator.language) }
    function search() { client.browse(region, query.text.trim(), 1, vocalType) }
    ColumnLayout {
        anchors.fill: parent; spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: root.t("title"); color: UI.Theme.textPrimary; font.pixelSize: 24; font.bold: true }
            UI.TextButton { text: root.t("open_settings"); subtle: true; onClicked: root.settingsRequested() }
        }
        Label {
            Layout.fillWidth: true; text: root.t("browse_help")
            wrapMode: Text.WordWrap; color: UI.Theme.textMuted
        }
        RowLayout {
            Repeater {
                model: ["jp", "en", "cn", "tw", "kr"]
                UI.TextButton {
                    required property string modelData
                    objectName: "sekaiRegion" + modelData
                    text: root.t("region_" + modelData); subtle: root.region !== modelData
                    enabled: root.available
                    onClicked: { root.region = modelData; root.vocalType = ""; root.search() }
                }
            }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: root.t("version_filter"); color: UI.Theme.textMuted }
            SelectBox {
                id: vocalFilter; objectName: "sekaiVocalFilter"
                Layout.fillWidth: true; Layout.minimumWidth: 0
                model: [""].concat(root.client.vocalTypes)
                currentIndex: Math.max(0, model.indexOf(root.vocalType))
                displayText: currentIndex > 0 ? root.t("vocal_" + currentText) : root.t("all_versions")
                delegate: ItemDelegate {
                    required property string modelData
                    width: vocalFilter.width
                    palette.windowText: UI.Theme.textPrimary
                    text: modelData ? root.t("vocal_" + modelData) : root.t("all_versions")
                }
                enabled: root.available && !root.client.busy
                onActivated: { root.vocalType = currentText; root.search() }
                Accessible.name: root.t("version_filter")
            }
            UI.TextButton {
                text: root.t("refresh"); subtle: true; enabled: root.available && !root.client.busy
                onClicked: root.client.reload(root.region, query.text.trim(), root.vocalType)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            UI.InputField {
                id: query; objectName: "sekaiQuery"
                Layout.fillWidth: true; Layout.minimumWidth: 0
                placeholderText: root.t("query_hint")
                Accessible.name: placeholderText
                maximumLength: 128
                enabled: root.available
                onAccepted: if (!root.client.busy) root.search()
            }
            UI.TextButton {
                objectName: "sekaiSearch"
                text: root.t(root.client.busy ? "loading" : "browse")
                enabled: root.available && !root.client.busy
                onClicked: root.search()
            }
        }
        Label {
            Layout.fillWidth: true; visible: Boolean(root.client.message)
            text: root.t(root.client.message); textFormat: Text.PlainText; wrapMode: Text.WordWrap
            color: root.client.failed ? UI.Theme.statusError : root.client.warning ? UI.Theme.statusWarning : UI.Theme.statusSuccess
        }
        ListView {
            id: results; objectName: "sekaiResults"
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            model: root.client.tracks; spacing: 4; currentIndex: -1
            onModelChanged: currentIndex = -1
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                width: results.width; height: 100; radius: UI.Theme.radiusSm
                color: results.currentIndex === index ? UI.Theme.bgSelected : UI.Theme.bgSurface
                border.color: activeFocus ? UI.Theme.accentPrimary : "transparent"
                HoverHandler { id: rowHover }
                ToolTip.visible: rowHover.hovered || row.activeFocus
                ToolTip.delay: 700
                ToolTip.text: [modelData.title, modelData.artist, modelData.version, modelData.author].filter(Boolean).join(" · ")
                Keys.onReturnPressed: if (results.currentIndex === index) root.client.action(modelData, "play")
                Keys.onEnterPressed: if (results.currentIndex === index) root.client.action(modelData, "play")
                MouseArea {
                    anchors.fill: parent
                    onClicked: { results.currentIndex = row.index; row.forceActiveFocus() }
                    onDoubleClicked: root.client.action(row.modelData, "play")
                }
                RowLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 10
                    Item {
                        Layout.preferredWidth: 48; Layout.preferredHeight: 48
                        Image { anchors.fill: parent; source: "qrc:/artwork/default-cover.png"; fillMode: Image.PreserveAspectCrop }
                        Image {
                            objectName: "sekaiCover"; anchors.fill: parent
                            source: row.modelData.cover_url || ""; asynchronous: true; mipmap: true
                            sourceSize.width: 96; sourceSize.height: 96
                            fillMode: Image.PreserveAspectCrop; visible: status === Image.Ready
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 4
                        Label {
                            objectName: "sekaiTrackTitle"; Layout.fillWidth: true; Layout.minimumWidth: 0
                            text: row.modelData.title || root.t("untitled"); textFormat: Text.PlainText
                            color: UI.Theme.textPrimary; elide: Text.ElideRight; font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            text: [row.modelData.artist, row.modelData.version || row.modelData.vocal_type].filter(Boolean).join(" · ")
                            textFormat: Text.PlainText; color: UI.Theme.textMuted; elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            text: root.t("author").replace("{name}", row.modelData.author || "—")
                            textFormat: Text.PlainText; color: UI.Theme.textMuted; elide: Text.ElideRight
                        }
                    }
                    UI.TextButton { text: root.t("play"); subtle: true; enabled: !root.client.actionBusy && root.available; onClicked: root.client.action(row.modelData, "play") }
                    UI.TextButton { objectName: "sekaiEnqueue"; text: root.t("enqueue"); subtle: true; enabled: !root.client.actionBusy && root.available; onClicked: root.client.action(row.modelData, "enqueue") }
                    UI.TextButton {
                        objectName: "sekaiDownload"; text: root.t("download"); subtle: true
                        enabled: !root.client.actionBusy && !root.client.downloadTask && root.available
                        onClicked: root.client.action(row.modelData, "download")
                    }
                }
            }
            Label {
                anchors.centerIn: parent; width: parent.width; horizontalAlignment: Text.AlignHCenter
                visible: results.count === 0; wrapMode: Text.WordWrap; color: UI.Theme.textMuted
                text: root.t(root.client.busy ? "loading" : root.client.searched ? "empty" : "start")
            }
        }
        RowLayout {
            Layout.fillWidth: true; visible: Boolean(root.client.downloadTask)
            Label { Layout.fillWidth: true; text: root.t("downloading"); color: UI.Theme.textSecondary }
            UI.TextButton { text: root.t("cancel"); subtle: true; onClicked: root.client.cancel() }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: root.t("count").replace("{count}", root.client.total); color: UI.Theme.textMuted }
            UI.TextButton {
                objectName: "sekaiPrevious"; text: root.t("previous"); subtle: true
                enabled: root.available && !root.client.busy && root.client.page > 1
                onClicked: root.client.browse(root.client.submittedRegion, root.client.submittedQuery, root.client.page - 1, root.client.submittedVocal)
            }
            Label { text: root.t("page").replace("{page}", root.client.page); color: UI.Theme.textMuted }
            UI.TextButton {
                objectName: "sekaiNext"; text: root.t("next"); subtle: true
                enabled: root.available && !root.client.busy && Boolean(root.client.cursor)
                onClicked: root.client.browse(root.client.submittedRegion, root.client.submittedQuery, Number(root.client.cursor), root.client.submittedVocal)
            }
        }
    }
}
