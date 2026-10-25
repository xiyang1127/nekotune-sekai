#include "common/lyrics.h"
#include <QtTest>

using namespace nekotune;

class LyricsAdapterTest final : public QObject {
    Q_OBJECT
  private slots:
    void queryUsesProtocolKeys() {
        const auto map = ext::lyricsQueryToMap({"Song", "Artist", "Album", "track-1", 180000});
        QCOMPARE(map.value("title").toString(), "Song");
        QCOMPARE(map.value("artist").toString(), "Artist");
        QCOMPARE(map.value("album").toString(), "Album");
        QCOMPARE(map.value("track_id").toString(), "track-1");
        QCOMPARE(map.value("duration_ms").toLongLong(), 180000);
    }
    void candidateRoundTripKeepsSourceFields() {
        const QVariantMap source{{"title", "Song"}, {"artist", "Artist"}, {"duration_ms", 1000},
                                 {"score", 0.5},    {"song_result", true}, {"hash", "abc"},
                                 {"accesskey", "k"}, {"synced_lyrics", "[00:00.00]x"}};
        const auto candidate = ext::lyricsCandidateFromMap(source);
        QCOMPARE(candidate.title, "Song");
        QCOMPARE(candidate.durationMs, 1000);
        QCOMPARE(candidate.score, 0.5);
        QVERIFY(candidate.songResult);
        QCOMPARE(candidate.document.syncedLyrics, "[00:00.00]x");
        QCOMPARE(candidate.extra, (QVariantMap{{"hash", "abc"}, {"accesskey", "k"}}));
        QCOMPARE(ext::lyricsCandidateToMap(candidate), source);
    }
    void defaultScoreAndResolution() {
        QCOMPARE(ext::lyricsCandidateFromMap({{"title", "x"}}).score, 1.0);
        const auto staged = ext::lyricsResolutionFromMap({{"candidates", QVariantList{QVariantMap{{"title", "a"}}}}});
        QVERIFY(!staged.document);
        QCOMPARE(staged.candidates.size(), 1);
        const auto document = ext::lyricsResolutionFromMap({{"krc_lyrics", "krc"}, {"instrumental", true}});
        QVERIFY(document.document);
        QCOMPARE(document.document->krcLyrics, "krc");
        QVERIFY(document.document->instrumental);
        QVERIFY(document.candidates.isEmpty());
    }
};

QTEST_GUILESS_MAIN(LyricsAdapterTest)
#include "lyrics_adapter_test.moc"
