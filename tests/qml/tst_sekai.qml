import QtQuick
import QtQuick.Controls
import QtTest
import "../../sekai/qml" as Plugin

Rectangle {
    id: scene
    width: 980; height: 780; color: "#0E0D14"
    Plugin.Translator { id: pluginTranslator }
    QtObject {
        id: fakeApi
        property string ownerId: "nekotune.sekai"
        property var calls: []
        property var pending: []
        property bool failSave: false
        property bool earlyDownload: false
        property var config: ({enabled: true, master_url: "https://master.example", asset_url: "https://audio.example", cover_url: "https://cover.example", region: "jp", format: "mp3", length: "long", fallback_jp: true})
        function invoke(service, params) {
            calls = calls.concat([{method: service, params: params}])
            if (service === "status") return Promise.resolve(config)
            if (service === "defaults") return Promise.resolve({master_url: "https://new.example", asset_url: "https://new.example", cover_url: "https://new.example"})
            if (service === "config.set") return failSave ? Promise.reject(new Error("sekai_invalid_config")) : Promise.resolve(params)
            if (service === "cache.clear") return Promise.resolve({cleared: true})
            return new Promise((resolve, reject) => { pending = pending.concat([{resolve: resolve, reject: reject}]) })
        }
        function call(method, params) {
            calls = calls.concat([{method: method, params: params}])
            if (earlyDownload && method === "music.download")
                tests.controller.earlyDownloads = [{task: "download-task", state: "finished"}]
            return Promise.resolve({task: "download-task", queue_id: 1})
        }
    }
    Component { id: controllerComponent; Plugin.Controller { api: fakeApi } }
    Component { id: panelComponent; Plugin.MusicPanel { anchors.fill: parent; anchors.margins: 20; client: tests.controller; translator: pluginTranslator } }
    Component { id: settingsComponent; Plugin.SettingsPanel { anchors.fill: parent; anchors.margins: 20; client: tests.controller; translator: pluginTranslator } }
    TestCase {
        id: tests; name: "SekaiPlugin"; when: windowShown
        property var controller
        property var panel
        function init() {
            failOnWarning(/TypeError|ReferenceError|Binding loop/)
            scene.width = 980; pluginTranslator.language = "zh"
            fakeApi.calls = []; fakeApi.pending = []; fakeApi.failSave = false; fakeApi.earlyDownload = false
            controller = createTemporaryObject(controllerComponent, scene)
            verify(controller !== null); tryCompare(controller, "ready", true)
            panel = createTemporaryObject(panelComponent, scene); verify(panel !== null)
        }
        function cleanup() { panel.destroy(); wait(0); controller.destroy(); wait(0) }
        function screenshot(name) {
            const file = testFixtures.screenshotPath(name)
            if (file) { waitForRendering(panel); grabImage(scene).save(file) }
        }
        function seed() {
            controller.tracks = [
                {id: "jp:1185:long", title: "モニタリング · Monitoring · A long title for narrow windows", artist: "初音ミク / Hatsune Miku", version: "VIRTUAL SINGER ver.", author: "DECO*27", cover_url: "qrc:/artwork/default-cover.png"},
                {id: "jp:1186:long", title: "<b>Plain title</b>", artist: "Hatsune Miku / GUMI", version: "Duet", author: "DECO*27", cover_url: ""}
            ]; controller.total = 2
            const list = findChild(panel, "sekaiResults")
            tryCompare(list, "count", 2); tryVerify(() => list.itemAtIndex(1) !== null)
            return list
        }
        function test_browseWithoutQueryPaginationAndLateRegionResponse() {
            mouseClick(findChild(panel, "sekaiSearch"))
            verify(controller.busy)
            compare(fakeApi.calls[fakeApi.calls.length - 1].params.query, "")
            const old = fakeApi.pending[0]
            mouseClick(findChild(panel, "sekaiRegionen"))
            compare(fakeApi.calls[fakeApi.calls.length - 1].params.region, "en")
            fakeApi.pending[1].resolve({tracks: [{id: "en:1185:long", title: "Monitoring"}], cursor: "2", total: 41, vocal_types: ["original_song", "future_type"]})
            tryCompare(controller, "busy", false)
            old.resolve({tracks: [{id: "jp:999:long", title: "Late"}], total: 1})
            wait(0); compare(controller.tracks[0].id, "en:1185:long")
            findChild(panel, "sekaiQuery").text = "unsent"
            mouseClick(findChild(panel, "sekaiNext"))
            compare(fakeApi.calls[fakeApi.calls.length - 1].params.query, "")
            compare(fakeApi.calls[fakeApi.calls.length - 1].params.region, "en")
            compare(fakeApi.calls[fakeApi.calls.length - 1].params.cursor, "2")
            controller.acceptStatus(Object.assign({}, controller.status, {format: "flac"}))
            fakeApi.pending[2].resolve({tracks: [{id: "en:99:long", title: "Late page"}]})
            wait(0); compare(controller.tracks.length, 0); compare(controller.total, 0)
            verify(findChild(panel, "sekaiSearch").enabled); verify(!controller.busy)
        }
        function test_queueDownloadAndEarlyCompletion() {
            const list = seed(), row = list.itemAtIndex(0)
            mouseClick(findChild(row, "sekaiEnqueue")); tryCompare(controller, "actionBusy", false)
            compare(list.currentIndex, -1)
            compare(fakeApi.calls[fakeApi.calls.length - 1].method, "music.enqueue")
            compare(fakeApi.calls[fakeApi.calls.length - 1].params.play, false)
            mouseClick(findChild(row, "sekaiDownload")); tryCompare(controller, "downloadTask", "download-task")
            verify(!findChild(row, "sekaiDownload").enabled)
            controller.downloadEvent({task: "other", state: "finished"}); compare(controller.downloadTask, "download-task")
            controller.cancel(); compare(fakeApi.calls[fakeApi.calls.length - 1].method, "music.cancel")
            controller.downloadEvent({task: "download-task", state: "cancelled"}); compare(controller.message, "cancelled")
            fakeApi.earlyDownload = true
            mouseClick(findChild(row, "sekaiDownload")); tryCompare(controller, "message", "downloaded")
            compare(controller.downloadTask, "")
            controller.downloadTask = "with-cover-error"
            controller.downloadEvent({task: "with-cover-error", state: "finished", cover_status: "error", asset_warning: "sekai_cover_failed"})
            compare(controller.message, "downloaded_cover_warning")
            verify(controller.warning); verify(!controller.failed)
            controller.downloadTask = "with-lyrics-error"
            controller.downloadEvent({task: "with-lyrics-error", state: "finished", lyrics_status: "error", asset_warning: "sekai_default_lyrics_failed"})
            compare(controller.message, "downloaded_lyrics_warning")
            verify(controller.warning); verify(!controller.failed)
            mouseClick(row, 80, 30); keyClick(Qt.Key_Return); tryCompare(controller, "actionBusy", false)
            compare(fakeApi.calls[fakeApi.calls.length - 1].params.play, true)
        }
        function test_narrowLayoutFallbackAndLanguages() {
            const list = seed()
            tryCompare(findChild(list.itemAtIndex(0), "sekaiCover"), "status", Image.Ready)
            compare(findChild(list.itemAtIndex(1), "sekaiCover").visible, false)
            compare(findChild(list.itemAtIndex(1), "sekaiTrackTitle").textFormat, Text.PlainText)
            screenshot("sekai-music-zh")
            scene.width = 600; pluginTranslator.language = "en"; waitForRendering(panel)
            const row = list.itemAtIndex(0), button = findChild(row, "sekaiDownload")
            verify(button.mapToItem(row, button.width, 0).x <= row.width)
            verify(findChild(row, "sekaiTrackTitle").width > 0)
            verify(findChild(row, "sekaiTrackTitle").truncated)
            screenshot("sekai-music-narrow-en")
        }
        function test_settingsFailureKeepsDraftAndSaveApplies() {
            panel.destroy(); panel = createTemporaryObject(settingsComponent, scene); verify(panel !== null)
            verify(!findChild(panel, "sekaiSave").enabled)
            verify(findChild(panel, "sekaiEnabled") === null)
            verify(findChild(panel, "sekaiTrimDownloads").checked)
            mouseClick(findChild(panel, "sekaiTrimDownloads"))
            compare(panel.draft.trim_downloads, false)
            verify(!findChild(panel, "sekai_master_url").visible)
            screenshot("sekai-settings-zh")
            pluginTranslator.language = "en"; scene.width = 600; screenshot("sekai-settings-narrow-en")
            mouseClick(findChild(panel, "sekaiAdvanced"))
            verify(findChild(panel, "sekai_master_url").visible)
            mouseClick(findChild(panel, "sekaiDefaults"))
            tryVerify(() => panel.draft.master_url === "https://new.example")
            panel.advanced = false
            waitForRendering(panel)
            fakeApi.failSave = true; mouseClick(findChild(panel, "sekaiSave"))
            tryCompare(panel, "saving", false); compare(panel.message, "sekai_invalid_config")
            compare(panel.draft.master_url, "https://new.example")
            fakeApi.failSave = false; mouseClick(findChild(panel, "sekaiSave"))
            tryCompare(panel, "message", "saved"); compare(controller.status.master_url, "https://new.example")
            compare(controller.status.trim_downloads, false)
            verify(!findChild(panel, "sekaiSave").enabled)
        }
    }
}
