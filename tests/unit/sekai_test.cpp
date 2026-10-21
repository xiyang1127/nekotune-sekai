#include "fake_context.h"
#include "fake_transport.h"
#include "common/media.h"
#include "sekai/extension.h"
#include <QDir>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace nekotune;
using namespace sekai;

namespace {
QJsonObject fixture(int count = 2) {
    QJsonArray vocals;
    for (int index = 0; index < count; ++index)
        vocals.append(QJsonObject{{"id", 1185 + index},
                                  {"musicId", 628},
                                  {"musicVocalType", index ? "future_type" : "original_song"},
                                  {"caption", index ? "Duet" : "Virtual Singer"},
                                  {"assetbundleName", QStringLiteral("vocal_%1").arg(index)},
                                  {"characters", QJsonArray{QJsonObject{{"characterId", 21}, {"characterType", "game_character"}}}}});
    return {{"musics", QJsonArray{QJsonObject{{"id", 628}, {"title", "Monitoring"}, {"composer", "DECO*27"}, {"lyricist", "Writer"},
                                              {"arranger", "Arranger"}, {"assetbundleName", "jacket_s_628"}, {"fillerSec", 8.09}}}},
            {"musicVocals", vocals},
            {"gameCharacters", QJsonArray{QJsonObject{{"id", 21}, {"firstName", "Hatsune"}, {"givenName", "Miku"}}}},
            {"outsideCharacters", QJsonArray{}}};
}
using Assets = std::function<ext::HttpResponse(const ext::HttpRequest &)>;
void route(FakeTransport &transport, QJsonObject data, Assets assets = {}) {
    transport.handler = [data, assets](const ext::HttpRequest &request) -> std::optional<Result<ext::HttpResponse>> {
        const auto path = request.url.path();
        if (path.endsWith(".json")) {
            const auto name = path.section('/', -1).chopped(5);
            ext::HttpResponse response;
            response.status = 200;
            response.body = QJsonDocument(data.value(name).toArray()).toJson(QJsonDocument::Compact);
            return response;
        }
        if (assets)
            return assets(request);
        return rawResponse({}, 200, {{"content-type", "audio/mpeg"}});
    };
}
Configuration enabled(QVariantMap extra = {}) {
    auto value = recommendedUrls;
    for (auto it = extra.cbegin(); it != extra.cend(); ++it)
        value.insert(it.key(), it.value());
    return configuration(value).value();
}
Result<QVariantMap> run(std::function<void(Reply)> call) {
    std::optional<Result<QVariantMap>> result;
    call([&](Result<QVariantMap> value) { result = std::move(value); });
    return result ? *result : Result<QVariantMap>(failure("pending"));
}
QString error(const Result<QVariantMap> &result) { return result ? QString() : result.error().message; }
QString makeAudio(const QString &directory, const QString &extension, int leading) {
    const auto file = QDir(directory).filePath("source." + extension);
    // After the intro, keep a one-second internal pause between two audible tones.
    const auto expression = QStringLiteral("if(lt(t,%1),0,if(between(t,%2,%3),0,0.2*sin(2*PI*440*t)))")
                                .arg(leading).arg(leading + 1).arg(leading + 2)
                                .replace(",", "\\,");
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-v", "error", "-nostdin", "-f", "lavfi", "-i",
                            QStringLiteral("aevalsrc=%1:s=44100:d=%2").arg(expression).arg(leading + 3), "-y", file});
    ffmpeg.waitForFinished(30'000);
    return file;
}
double duration(const QString &file) {
    QProcess probe;
    probe.start("ffprobe", {"-v", "error", "-show_entries", "format=duration", "-of", "default=nw=1:nk=1", file});
    probe.waitForFinished(30'000);
    return probe.readAllStandardOutput().trimmed().toDouble();
}
Result<QVariantMap> waitFor(std::function<void(DownloadPreparer::Done)> call) {
    std::optional<Result<QVariantMap>> result;
    call([&](Result<QVariantMap> value) { result = std::move(value); });
    QElapsedTimer timer;
    timer.start();
    while (!result && timer.elapsed() < 30'000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return result ? *result : Result<QVariantMap>(failure("timeout"));
}
const QByteArray png = QByteArray::fromBase64(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jJv0AAAAASUVORK5CYII=");
const QStringList coverUrls{"https://covers.example/jacket.webp", "https://covers.example/jacket.png"};
} // namespace

class SekaiTest final : public QObject {
    Q_OBJECT
  private slots:
    void defaultsAndValidation();
    void distinctVocalsAndIds();
    void paginationSearchAndSharedCatalog();
    void resolveWithRegionFallback();
    void missingVersusUnavailable();
    void failedCatalogIsRetriedAndCloseAborts();
    void activationMigratesSetup();
    void leadingSilenceRules();
    void trimRemovesOnlyTheIntro_data();
    void trimRemovesOnlyTheIntro();
    void trimLeavesImmediateAndSilentAudio();
    void coverSidecars();
    void defaultLyrics();
};

void SekaiTest::defaultsAndValidation() {
    const auto config = configuration({{"enabled", false}, {"master_url", ""}, {"asset_url", " "}, {"cover_url", ""}}).value();
    QCOMPARE(config.masterUrl, recommendedUrls.value("master_url").toString());
    QCOMPARE(config.assetUrl, recommendedUrls.value("asset_url").toString());
    QVERIFY(!config.toMap().contains("enabled"));
    for (const auto &bad : QList<QVariantMap>{{{"format", "aac"}}, {{"format", QStringList{"mp3"}}}, {{"region", "all"}}, {{"length", "full"}},
                                              {{"fallback_jp", 1}}, {{"asset_url", "http://example.com"}},
                                              {{"master_url", "https://secret@example.com"}},
                                              {{"cover_url", "https://example.com/?token=secret"}}})
        QCOMPARE(configuration(bad).error().message, "sekai_invalid_config");
    QCOMPARE(enabled({{"asset_url", " https://example.com/assets/ "}}).assetUrl, "https://example.com/assets");
}
void SekaiTest::distinctVocalsAndIds() {
    FakeTransport transport;
    route(transport, fixture());
    Provider provider(enabled(), transport);
    const auto result = run([&](Reply done) { provider.search({{"region", "en"}, {"query", "ＤＥＣＯ"}}, done); });
    QVERIFY2(result, qPrintable(error(result)));
    const auto tracks = result.value().value("tracks").toList();
    QCOMPARE(tracks[0].toMap().value("id").toString(), "en:1185:long");
    QCOMPARE(tracks[1].toMap().value("id").toString(), "en:1186:long");
    QCOMPARE(tracks[0].toMap().value("artist").toString(), "Hatsune Miku");
    QCOMPARE(tracks[0].toMap().value("author").toString(), "DECO*27");
    QVERIFY(!tracks[0].toMap().contains("duration_ms") && !tracks[0].toMap().contains("audioUrl"));
    QCOMPARE(tracks[1].toMap().value("vocal_type").toString(), "future_type");
    QVERIFY(tracks[1].toMap().value("album").toString().contains("Duet"));
    QCOMPARE(transport.requests.size(), 4);
    QCOMPARE(run([&](Reply done) { provider.track("tw:1185:short", done); }).value().value("id").toString(), "tw:1185:short");
    QVERIFY(std::any_of(transport.requests.cbegin(), transport.requests.cend(),
                        [](const auto &request) { return request.url.toString().contains("sekai-master-db-tc-diff"); }));
    QCOMPARE(parseId("jp:1185:long").value().vocalId, 1185);
    for (const auto &id : {"jp:1185", "xx:1:long", "jp:0:long", "../1", "en:9007199254740992:long"})
        QCOMPARE(parseId(id).error().message, "sekai_invalid_track");
}
void SekaiTest::paginationSearchAndSharedCatalog() {
    FakeTransport transport;
    route(transport, fixture(43));
    // Hold the first catalog round so two searches share it.
    QList<std::pair<ext::HttpRequest, ext::HttpDone>> held;
    auto respond = transport.handler;
    transport.handler = [&](const ext::HttpRequest &request) -> std::optional<Result<ext::HttpResponse>> {
        held.append({request, {}});
        return std::nullopt;
    };
    Provider provider(enabled(), transport);
    std::optional<Result<QVariantMap>> first, parallel;
    provider.search({}, [&](Result<QVariantMap> value) { first = value; });
    provider.search({{"query", "628"}}, [&](Result<QVariantMap> value) { parallel = value; });
    QCOMPARE(transport.requests.size(), 4);
    for (int i = 0; i < 4; ++i)
        transport.pending.takeFirst()(*respond(transport.requests[i]));
    transport.handler = respond;
    QCOMPARE(first->value().value("total").toInt(), 43);
    QCOMPARE(first->value().value("tracks").toList().size(), 40);
    QCOMPARE(first->value().value("cursor").toString(), "2");
    QCOMPARE(parallel->value().value("total").toInt(), 43);
    const auto second = run([&](Reply done) { provider.search({{"cursor", "2"}}, done); });
    QCOMPARE(second.value().value("tracks").toList().size(), 3);
    QCOMPARE(second.value().value("cursor").toString(), "");
    QCOMPARE(run([&](Reply done) { provider.search({{"query", "arranger"}, {"vocal_type", "future_type"}}, done); })
                 .value()
                 .value("total")
                 .toInt(),
             42);
    QCOMPARE(run([&](Reply done) { provider.search({{"query", "unknown"}}, done); }).value().value("total").toInt(), 0);
    QCOMPARE(transport.requests.size(), 4);
    for (const auto &params : QList<QVariantMap>{{{"cursor", "0"}}, {{"cursor", "1.1"}}, {{"region", "tc"}}, {{"query", QString(129, 'x')}}})
        QCOMPARE(error(run([&](Reply done) { provider.search(params, done); })), "sekai_invalid_query");
}
void SekaiTest::resolveWithRegionFallback() {
    FakeTransport transport;
    route(transport, fixture(), [](const ext::HttpRequest &request) {
        return request.url.toString().contains("sekai-en-assets") ? rawResponse({}, 404)
                                                                 : rawResponse({}, 200, {{"content-type", "audio/flac"}});
    });
    Provider provider(enabled({{"format", "flac"}}), transport);
    const auto audio = run([&](Reply done) { provider.resolve("en:1185:short", done); });
    QVERIFY2(audio, qPrintable(error(audio)));
    QVERIFY(audio.value().value("url").toString().endsWith("sekai-jp-assets/music/short/vocal_0/vocal_0_short.flac"));
    QCOMPARE(audio.value().value("extension").toString(), "flac");
    QList<ext::HttpRequest> probes;
    for (const auto &request : transport.requests)
        if (!request.url.path().endsWith(".json"))
            probes.append(request);
    QCOMPARE(probes.size(), 2);
    for (const auto &probe : probes) {
        QCOMPARE(probe.method, "HEAD");
        QVERIFY(!probe.url.path().contains("/jacket/"));
    }
    FakeTransport strictTransport;
    route(strictTransport, fixture(), [](const ext::HttpRequest &) { return rawResponse({}, 404); });
    Provider strict(enabled({{"fallback_jp", false}}), strictTransport);
    QCOMPARE(error(run([&](Reply done) { strict.resolve("en:1185:long", done); })), "sekai_audio_missing");
    for (const auto &request : strictTransport.requests)
        QVERIFY(!request.url.toString().contains("sekai-jp-assets"));
}
void SekaiTest::missingVersusUnavailable() {
    for (const auto &response : {rawResponse({}, 403), rawResponse("<html>", 200, {{"content-type", "text/html"}})}) {
        FakeTransport transport;
        route(transport, fixture(), [response](const ext::HttpRequest &) { return response; });
        Provider provider(enabled(), transport);
        QCOMPARE(error(run([&](Reply done) { provider.resolve("jp:1185:long", done); })), "sekai_audio_unavailable");
    }
    FakeTransport transport;
    route(transport, fixture());
    Provider provider(enabled(), transport);
    QCOMPARE(error(run([&](Reply done) { provider.track("jp:9999:long", done); })), "sekai_not_found");
}
void SekaiTest::failedCatalogIsRetriedAndCloseAborts() {
    FakeTransport transport;
    bool failed = true;
    const auto data = fixture();
    transport.handler = [&](const ext::HttpRequest &request) -> std::optional<Result<ext::HttpResponse>> {
        if (failed)
            return rawResponse({}, 503);
        ext::HttpResponse response;
        response.status = 200;
        response.body = QJsonDocument(data.value(request.url.path().section('/', -1).chopped(5)).toArray()).toJson();
        return response;
    };
    Provider provider(enabled(), transport);
    QVERIFY(!run([&](Reply done) { provider.search({}, done); }));
    failed = false;
    QCOMPARE(run([&](Reply done) { provider.search({}, done); }).value().value("total").toInt(), 2);
    provider.close();
    QVERIFY(error(run([&](Reply done) { provider.search({}, done); })).contains("abort", Qt::CaseInsensitive));
    FakeTransport hanging;
    Provider pending(enabled(), hanging);
    std::optional<Result<QVariantMap>> result;
    pending.search({}, [&](Result<QVariantMap> value) { result = value; });
    QVERIFY(!result);
    pending.close();
    QVERIFY(result && !*result);
    QVERIFY(hanging.pending.isEmpty());
}
void SekaiTest::activationMigratesSetup() {
    FakeContext context;
    context.configuration = {{"enabled", false}, {"master_url", ""}, {"asset_url", "https://custom.example"}, {"cover_url", ""}, {"format", "flac"}};
    FakeTransport transport;
    Extension extension(context, transport);
    std::optional<Result<void>> activated;
    extension.activate([&](Result<void> value) { activated = value; });
    QVERIFY(activated && *activated);
    QCOMPARE(transport.requests.size(), 0);
    QCOMPARE(context.descriptors.value("music/music").value("page").toString(), "music");
    QVERIFY(context.descriptors.value("music/music").value("download").toBool());
    QVERIFY(!context.call("status").value().contains("enabled"));
    QCOMPARE(context.configuration.value("asset_url").toString(), "https://custom.example");
    QCOMPARE(context.configuration.value("master_url").toString(), recommendedUrls.value("master_url").toString());
    QCOMPARE(context.configuration.value("format").toString(), "flac");
    QCOMPARE(context.call("config.set", {{"master_url", "http://example.com"}}).error().message, "sekai_invalid_config");
    auto cn = enabled({{"region", "cn"}}).toMap();
    QCOMPARE(context.call("config.set", cn).value().value("region").toString(), "cn");
    QCOMPARE(context.call("status").value().value("region").toString(), "cn");
    context.call("config.set", configuration().value().toMap());
    QCOMPARE(context.call("status").value().value("master_url").toString(), recommendedUrls.value("master_url").toString());
    QVERIFY(context.published("status"));
    extension.close();
    QVERIFY(context.music.isEmpty() && context.services.isEmpty());
}
void SekaiTest::leadingSilenceRules() {
    QCOMPARE(leadingSilence("silence_start: 0\nsilence_end: 8", 12), 7.97);
    QCOMPARE(leadingSilence("silence_start: 1\nsilence_end: 8", 12), 0.0);
    QCOMPARE(leadingSilence("silence_start: 0\nsilence_end: 12", 12), 0.0);
    QCOMPARE(leadingSilence("silence_start: 0\nsilence_end: 0.1", 12), 0.0);
}
void SekaiTest::trimRemovesOnlyTheIntro_data() {
    QTest::addColumn<QString>("extension");
    for (const auto *extension : {"mp3", "flac", "wav"})
        QTest::newRow(extension) << QString(extension);
}
void SekaiTest::trimRemovesOnlyTheIntro() {
    if (QStandardPaths::findExecutable("ffmpeg").isEmpty())
        QSKIP("ffmpeg is not installed");
    QFETCH(QString, extension);
    QTemporaryDir directory;
    const auto input = makeAudio(directory.path(), extension, 8);
    QFile original(input);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const auto bytes = original.readAll();
    original.close();
    DownloadPreparer preparer(directory.filePath("cache"), [](const QString &) {});
    const auto result = waitFor([&](auto done) { preparer.trim(input, directory.path(), 7, done); });
    QVERIFY2(result, qPrintable(result ? QString() : result.error().message));
    const double trimmed = result.value().value("trimmed").toDouble();
    QVERIFY2(trimmed > 7.9 && trimmed < 8.05, qPrintable(QString::number(trimmed)));
    QCOMPARE(result.value().value("extension").toString(), extension);
    const double length = duration(result.value().value("file").toString());
    QVERIFY2(length > 2.9 && length < 3.2, qPrintable(QString::number(length)));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), bytes);
}
void SekaiTest::trimLeavesImmediateAndSilentAudio() {
    if (QStandardPaths::findExecutable("ffmpeg").isEmpty())
        QSKIP("ffmpeg is not installed");
    QTemporaryDir directory;
    DownloadPreparer preparer(directory.filePath("cache"), [](const QString &) {});
    const auto immediate = makeAudio(directory.path(), "wav", 0);
    auto result = waitFor([&](auto done) { preparer.trim(immediate, directory.path(), 9, done); });
    QCOMPARE(result.value().value("file").toString(), immediate);
    QCOMPARE(result.value().value("trimmed").toDouble(), 0.0);
    QTemporaryDir other;
    const auto silent = makeAudio(other.path(), "wav", 20);
    result = waitFor([&](auto done) { preparer.trim(silent, other.path(), 8, done); });
    QCOMPARE(result.value().value("file").toString(), silent);
}
void SekaiTest::coverSidecars() {
    const auto setup = [](QTemporaryDir &directory) {
        const auto audio = directory.filePath("000123.mp3");
        QFile file(audio);
        file.open(QIODevice::WriteOnly);
        file.write("original audio");
        return audio;
    };
    {
        QTemporaryDir directory;
        const auto audio = setup(directory);
        FakeTransport transport;
        transport.handler = [](const ext::HttpRequest &request) -> std::optional<Result<ext::HttpResponse>> {
            return request.url.path().endsWith(".webp") ? rawResponse("<html>unavailable</html>", 200, {{"content-type", "image/webp"}})
                                                         : rawResponse(png, 200, {{"content-type", "image/png"}});
        };
        const auto result = run([&](Reply done) { saveCover(transport, audio, coverUrls, done); });
        QCOMPARE(result.value().value("cover_status").toString(), "saved");
        QCOMPARE(result.value().value("cover_path").toString(), directory.filePath("000123.png"));
        QCOMPARE(transport.requests.size(), 2);
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files, QDir::Name), (QStringList{"000123.mp3", "000123.png"}));
    }
    for (const auto *suffix : {"jpg", "jpeg", "png", "webp"}) {
        QTemporaryDir directory;
        const auto audio = setup(directory);
        QFile cover(directory.filePath(QStringLiteral("000123.%1").arg(suffix)));
        cover.open(QIODevice::WriteOnly);
        cover.write("user artwork");
        cover.close();
        FakeTransport transport;
        QCOMPARE(run([&](Reply done) { saveCover(transport, audio, coverUrls, done); }).value().value("cover_status").toString(),
                 "existing");
        QCOMPARE(transport.requests.size(), 0);
    }
    {
        QTemporaryDir directory;
        const auto audio = setup(directory);
        FakeTransport transport;
        transport.handler = [](const ext::HttpRequest &) -> std::optional<Result<ext::HttpResponse>> { return rawResponse({}, 404); };
        const auto result = run([&](Reply done) { saveCover(transport, audio, coverUrls, done); });
        QCOMPARE(result.value().value("cover_status").toString(), "error");
        QCOMPARE(result.value().value("asset_warning").toString(), "sekai_cover_failed");
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files), QStringList{"000123.mp3"});
        transport.requests.clear();
        transport.handler = [](const ext::HttpRequest &) -> std::optional<Result<ext::HttpResponse>> {
            return rawResponse({}, 302, {{"location", "https://unrelated.example/secret"}});
        };
        QCOMPARE(run([&](Reply done) { saveCover(transport, audio, {coverUrls[0]}, done); }).value().value("cover_status").toString(),
                 "error");
        QCOMPARE(transport.requests.size(), 1);
    }
    auto huge = png;
    huge[16] = char(0x01);
    QVERIFY(ext::imageSuffix(huge, "image/png").isEmpty());
    QCOMPARE(ext::imageSuffix(png, "image/png"), "png");
}
void SekaiTest::defaultLyrics() {
    QTemporaryDir directory;
    const auto audio = directory.filePath("000123.mp3");
    QFile file(audio);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("original audio");
    file.close();
    QCOMPARE(saveDefaultLyrics(audio).value("lyrics_status").toString(), "saved");
    QFile lyrics(directory.filePath("000123.lrc"));
    QVERIFY(lyrics.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(lyrics.readAll()), sekai::defaultLyrics);
    QCOMPARE(saveDefaultLyrics(audio).value("lyrics_status").toString(), "existing");
    for (const auto *suffix : {"lrc", "krc", "LRC", "KRC"}) {
        QTemporaryDir other;
        const auto track = other.filePath("000123.mp3");
        QFile(track).open(QIODevice::WriteOnly);
        QFile sidecar(other.filePath(QStringLiteral("000123.%1").arg(suffix)));
        sidecar.open(QIODevice::WriteOnly);
        sidecar.write("user lyrics");
        sidecar.close();
        QCOMPARE(saveDefaultLyrics(track).value("lyrics_status").toString(), "existing");
        QCOMPARE(QDir(other.path()).entryList(QDir::Files).size(), 2);
    }
    QTemporaryDir custom;
    const auto track = custom.filePath("000123.mp3");
    QFile(track).open(QIODevice::WriteOnly);
    QCOMPARE(saveDefaultLyrics(track, true).value("lyrics_status").toString(), "existing");
    const auto unreadable = saveDefaultLyrics(track, std::nullopt);
    QCOMPARE(unreadable.value("lyrics_status").toString(), "error");
    QCOMPARE(unreadable.value("asset_warning").toString(), "sekai_default_lyrics_failed");
    QFile::remove(track);
    QCOMPARE(saveDefaultLyrics(track).value("lyrics_status").toString(), "error");
    QVERIFY(QDir(custom.path()).entryList(QDir::Files).isEmpty());
    // Default lyrics are saved even when cover metadata cannot be fetched.
    QTemporaryDir offline;
    const auto offlineAudio = offline.filePath("000123.mp3");
    QFile(offlineAudio).open(QIODevice::WriteOnly);
    FakeTransport transport;
    transport.handler = [](const ext::HttpRequest &) -> std::optional<Result<ext::HttpResponse>> { return failure("network_error"); };
    Provider provider(configuration().value(), transport);
    const auto result = run([&](Reply done) {
        provider.downloaded("jp:1185:long", offlineAudio, [](auto answer) { answer(false); }, done);
    });
    QCOMPARE(result.value().value("cover_status").toString(), "error");
    QCOMPARE(result.value().value("lyrics_status").toString(), "saved");
}

QTEST_GUILESS_MAIN(SekaiTest)
#include "sekai_test.moc"
