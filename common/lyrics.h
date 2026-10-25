#pragma once
#include <nekotune/sdk/providers.h>

namespace nekotune::ext {
/// SDK 3 lyrics structs ↔ the field maps the source clients use internally
/// (title, artist, album, duration_ms, score, song_result, synced_lyrics, plain_lyrics,
/// krc_lyrics, cover_url, instrumental; every other key is source-defined).
QVariantMap lyricsQueryToMap(const sdk::LyricsQuery &query);
sdk::LyricsCandidate lyricsCandidateFromMap(const QVariantMap &map);
QVariantMap lyricsCandidateToMap(const sdk::LyricsCandidate &candidate);
QVector<sdk::LyricsCandidate> lyricsCandidatesFromList(const QVariantList &list);
/// {candidates: [...]} becomes further candidates; any other map is a lyrics document.
sdk::LyricsResolution lyricsResolutionFromMap(const QVariantMap &map);
} // namespace nekotune::ext
