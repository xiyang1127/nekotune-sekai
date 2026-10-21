import QtQuick
import NekoTune 1.0
QtObject {
    id: root
    property var status: ({region: "jp", format: "mp3", length: "long"})
    property bool ready: false
    property bool busy: false
    property bool actionBusy: false
    property bool startingDownload: false
    property var tracks: []
    property var earlyDownloads: []
    property string downloadTask: ""
    property string downloadMessage: ""
    property string message: ""
    property bool failed: false
    property bool warning: false
    property int generation: 0
    property string submittedQuery: ""
    property string submittedRegion: "jp"
    property string submittedVocal: ""
    property var vocalTypes: []
    property int total: 0
    property string cursor: ""
    property int page: 1
    property bool searched: false
    property var api: ExtensionApi {
        onEvent: function(event) {
            if (event.event === "extension.nekotune.sekai.status") root.acceptStatus(event.data)
            if (event.event === "music.download") {
                if (root.startingDownload) root.earlyDownloads = root.earlyDownloads.concat([event])
                else root.downloadEvent(event)
            }
        }
    }
    function errorKey(error) {
        const match = String(error).match(/sekai_[a-z_]+/)
        return match ? match[0] : "failed"
    }
    function acceptStatus(value) {
        if (JSON.stringify(status) !== JSON.stringify(value)) {
            ++generation
            busy = false
            tracks = []; cursor = ""; searched = false; total = 0; page = 1; vocalTypes = []
        }
        status = value
        ready = true
    }
    function refresh() {
        const revision = generation
        return api.invoke("status").then(value => {
            if (revision === root.generation) root.acceptStatus(value)
        }).catch(error => {
            if (revision !== root.generation) return
            root.message = root.errorKey(error); root.failed = true
        })
    }
    function browse(region, query, pageNumber, vocalType) {
        if (!ready) return
        const revision = ++generation
        busy = true; message = ""; failed = false; warning = false
        const params = {region: region, query: query, cursor: String(pageNumber), vocal_type: vocalType || ""}
        api.invoke("search", params).then(result => {
            if (revision !== root.generation) return
            root.tracks = result.tracks || []
            root.cursor = result.cursor || ""
            root.submittedQuery = query; root.submittedRegion = region; root.submittedVocal = vocalType || ""
            root.vocalTypes = result.vocal_types || []; root.total = result.total || 0
            root.page = pageNumber; root.searched = true; root.busy = false
        }).catch(error => {
            if (revision !== root.generation) return
            root.message = root.errorKey(error); root.failed = true; root.busy = false
        })
    }
    function reload(region, query, vocalType) {
        const revision = ++generation
        busy = true
        api.invoke("cache.clear").then(() => {
            if (revision !== root.generation) return
            root.browse(region, query, 1, vocalType)
        }).catch(error => {
            if (revision !== root.generation) return
            root.busy = false; root.failed = true; root.message = root.errorKey(error)
        })
    }
    function action(track, kind) {
        if (!ready || actionBusy || (kind === "download" && downloadTask)) return
        actionBusy = true; message = ""; failed = false; warning = false
        startingDownload = kind === "download"
        earlyDownloads = []
        const method = kind === "download" ? "music.download" : "music.enqueue"
        api.call(method, {source: api.ownerId + "/music", track: track, play: kind === "play"}).then(result => {
            root.actionBusy = false; root.startingDownload = false
            if (kind === "download") {
                root.downloadTask = result.task; root.downloadMessage = "downloading"
                for (const event of root.earlyDownloads) root.downloadEvent(event)
                root.earlyDownloads = []
            } else root.message = kind === "play" ? "playing" : "queued"
        }).catch(error => {
            root.actionBusy = false; root.startingDownload = false; root.earlyDownloads = []
            root.message = root.errorKey(error); root.failed = true
        })
    }
    function downloadEvent(event) {
        if (!downloadTask || event.task !== downloadTask) return
        if (event.state === "running") return
        downloadTask = ""; downloadMessage = ""
        failed = event.state !== "finished" && event.state !== "cancelled"
        warning = event.state === "finished" && (event.cover_status === "error" || Boolean(event.asset_warning))
        const warningMessage = event.lyrics_status === "error" ? "downloaded_lyrics_warning" : "downloaded_cover_warning"
        message = event.state === "finished" ? (warning ? warningMessage : "downloaded") : event.state === "cancelled" ? "cancelled" : errorKey(event.message)
    }
    function cancel() {
        if (downloadTask) api.call("music.cancel", {task: downloadTask}).catch(error => {
            root.message = root.errorKey(error); root.failed = true
        })
    }
    Component.onCompleted: refresh()
    Component.onDestruction: ++generation
}
