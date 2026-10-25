#include "common/lyrics.h"

namespace nekotune::ext {
namespace {
const char *const knownKeys[] = {"title",         "artist",       "album",      "duration_ms",
                                 "score",         "song_result",  "synced_lyrics", "plain_lyrics",
                                 "krc_lyrics",    "cover_url",    "instrumental"};
sdk::LyricsDocument documentFromMap(const QVariantMap &map) {
    return {map.value("synced_lyrics").toString(), map.value("plain_lyrics").toString(),
            map.value("krc_lyrics").toString(), map.value("cover_url").toString(),
            map.value("instrumental").toBool()};
}
} // namespace

QVariantMap lyricsQueryToMap(const sdk::LyricsQuery &query) {
    return {{"title", query.title},
            {"artist", query.artist},
            {"album", query.album},
            {"duration_ms", query.durationMs},
            {"track_id", query.trackId}};
}
sdk::LyricsCandidate lyricsCandidateFromMap(const QVariantMap &map) {
    sdk::LyricsCandidate candidate;
    candidate.title = map.value("title").toString();
    candidate.artist = map.value("artist").toString();
    candidate.album = map.value("album").toString();
    candidate.durationMs = map.value("duration_ms").toLongLong();
    candidate.score = map.value("score", 1.0).toDouble();
    candidate.songResult = map.value("song_result").toBool();
    candidate.document = documentFromMap(map);
    candidate.extra = map;
    for (const auto *key : knownKeys)
        candidate.extra.remove(key);
    return candidate;
}
QVariantMap lyricsCandidateToMap(const sdk::LyricsCandidate &candidate) {
    auto map = candidate.extra;
    for (const auto &[key, value] :
         {std::pair{"title", candidate.title}, {"artist", candidate.artist}, {"album", candidate.album},
          {"synced_lyrics", candidate.document.syncedLyrics}, {"plain_lyrics", candidate.document.plainLyrics},
          {"krc_lyrics", candidate.document.krcLyrics}, {"cover_url", candidate.document.coverUrl}})
        if (!value.isEmpty())
            map.insert(key, value);
    if (candidate.durationMs > 0)
        map.insert("duration_ms", candidate.durationMs);
    map.insert("score", candidate.score);
    if (candidate.songResult)
        map.insert("song_result", true);
    if (candidate.document.instrumental)
        map.insert("instrumental", true);
    return map;
}
QVector<sdk::LyricsCandidate> lyricsCandidatesFromList(const QVariantList &list) {
    QVector<sdk::LyricsCandidate> candidates;
    candidates.reserve(list.size());
    for (const auto &value : list)
        candidates.append(lyricsCandidateFromMap(value.toMap()));
    return candidates;
}
sdk::LyricsResolution lyricsResolutionFromMap(const QVariantMap &map) {
    if (map.contains("candidates"))
        return {std::nullopt, lyricsCandidatesFromList(map.value("candidates").toList())};
    return {documentFromMap(map), {}};
}
} // namespace nekotune::ext
