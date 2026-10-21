#include "catalog.h"
#include "common/json.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace nekotune;

namespace sekai {
namespace {
const QHash<QString, QString> repositories{{"jp", "sekai-master-db-diff"},
                                           {"en", "sekai-master-db-en-diff"},
                                           {"cn", "sekai-master-db-cn-diff"},
                                           {"tw", "sekai-master-db-tc-diff"},
                                           {"kr", "sekai-master-db-kr-diff"}};
const QHash<QString, QString> buckets{{"jp", "sekai-jp-assets"}, {"en", "sekai-en-assets"}, {"cn", "sekai-cn-assets"},
                                      {"tw", "sekai-tc-assets"}, {"kr", "sekai-kr-assets"}};
QString base(const QString &url) {
    QString result = url;
    while (result.endsWith('/'))
        result.chop(1);
    return result;
}
AppError dataError(const QString &source, const QString &detail) {
    return failure(QStringLiteral("Invalid master data (%1): %2").arg(source, detail));
}
bool nonnegativeId(const QJsonValue &value) {
    const double number = value.toDouble(-1);
    return value.isDouble() && number >= 0 && number == std::floor(number) && number <= 9007199254740991.0;
}
QString text(const QJsonValue &value) { return value.isString() ? value.toString() : QString(); }
Result<QString> segment(const QString &value) {
    // Reject path separators and dot segments instead of treating remote names as paths.
    if (value == "." || value == ".." || value.contains('/') || value.contains('\\'))
        return failure("Invalid master data (assetbundleName): expected one path segment");
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}
struct Music {
    qint64 id;
    QString title, assetbundleName, composer, lyricist, arranger;
    double fillerSec;
    QVariant isFullLength;
};
struct Character {
    qint64 id;
    QString type;
    double seq;
};
struct Vocal {
    qint64 id, musicId;
    QString type, caption, assetbundleName;
    QList<Character> characters;
};
Result<QList<Music>> parseMusics(const QJsonValue &data, const QString &source) {
    if (!data.isArray())
        return dataError(source, "expected an array");
    QList<Music> result;
    QSet<qint64> ids;
    int index = 0;
    for (const auto &value : data.toArray()) {
        const auto key = QStringLiteral("%1[%2]").arg(source).arg(index++);
        if (!value.isObject())
            return dataError(key, "expected an object");
        const auto row = value.toObject();
        if (!nonnegativeId(row.value("id")))
            return dataError(key + ".id", "expected a nonnegative integer ID");
        for (const auto *field : {"title", "assetbundleName"})
            if (text(row.value(field)).trimmed().isEmpty())
                return dataError(key + "." + field, "expected a nonempty string");
        const auto filler = row.value("fillerSec");
        result.append({qint64(row.value("id").toDouble()), row.value("title").toString(), row.value("assetbundleName").toString(),
                       text(row.value("composer")), text(row.value("lyricist")), text(row.value("arranger")),
                       filler.isDouble() ? filler.toDouble() : 0,
                       row.value("isFullLength").isBool() ? QVariant(row.value("isFullLength").toBool()) : QVariant()});
        if (ids.contains(result.last().id))
            return dataError(source, "duplicate IDs");
        ids.insert(result.last().id);
    }
    return result;
}
Result<QList<Vocal>> parseVocals(const QJsonValue &data, const QString &source) {
    if (!data.isArray())
        return dataError(source, "expected an array");
    QList<Vocal> result;
    QSet<qint64> ids;
    int index = 0;
    for (const auto &value : data.toArray()) {
        const auto key = QStringLiteral("%1[%2]").arg(source).arg(index++);
        if (!value.isObject())
            return dataError(key, "expected an object");
        const auto row = value.toObject();
        for (const auto *field : {"id", "musicId"})
            if (!nonnegativeId(row.value(field)))
                return dataError(key + "." + field, "expected a nonnegative integer ID");
        for (const auto *field : {"assetbundleName", "musicVocalType"})
            if (text(row.value(field)).trimmed().isEmpty())
                return dataError(key + "." + field, "expected a nonempty string");
        if (!row.value("characters").isArray())
            return dataError(key + ".characters", "expected an array");
        Vocal vocal{qint64(row.value("id").toDouble()), qint64(row.value("musicId").toDouble()),
                    row.value("musicVocalType").toString(), text(row.value("caption")), row.value("assetbundleName").toString(), {}};
        int position = 0;
        for (const auto &characterValue : row.value("characters").toArray()) {
            const auto characterKey = QStringLiteral("%1.characters[%2]").arg(key).arg(position);
            if (!characterValue.isObject())
                return dataError(characterKey, "expected an object");
            const auto character = characterValue.toObject();
            if (!nonnegativeId(character.value("characterId")))
                return dataError(characterKey + ".characterId", "expected a nonnegative integer ID");
            if (text(character.value("characterType")).trimmed().isEmpty())
                return dataError(characterKey + ".characterType", "expected a nonempty string");
            vocal.characters.append({qint64(character.value("characterId").toDouble()), character.value("characterType").toString(),
                                     character.value("seq").isDouble() ? character.value("seq").toDouble() : position});
            ++position;
        }
        std::stable_sort(vocal.characters.begin(), vocal.characters.end(),
                         [](const auto &a, const auto &b) { return a.seq < b.seq; });
        if (ids.contains(vocal.id))
            return dataError(source, "duplicate IDs");
        ids.insert(vocal.id);
        result.append(vocal);
    }
    return result;
}
Result<QHash<qint64, QString>> parseCharacters(const QJsonValue &data, const QString &source, bool western) {
    if (!data.isArray())
        return dataError(source, "expected an array");
    QHash<qint64, QString> result;
    int index = 0;
    for (const auto &value : data.toArray()) {
        const auto key = QStringLiteral("%1[%2]").arg(source).arg(index++);
        if (!value.isObject())
            return dataError(key, "expected an object");
        const auto row = value.toObject();
        if (!nonnegativeId(row.value("id")))
            return dataError(key + ".id", "expected a nonnegative integer ID");
        QStringList parts;
        for (const auto *field : {"firstName", "givenName"})
            if (!text(row.value(field)).isEmpty())
                parts.append(text(row.value(field)));
        const auto name = text(row.value("name")).isEmpty() ? parts.join(western ? " " : "") : text(row.value("name"));
        const auto id = qint64(row.value("id").toDouble());
        if (result.contains(id))
            return dataError(source, "duplicate IDs");
        result.insert(id, name);
    }
    return result;
}
struct ResolveState {
    QStringList urls;
    QString kind;
    bool allMissing = true;
    std::function<void(QString, AssetStatus)> done;
};
} // namespace

Catalog::Catalog(ext::HttpTransport &transport, CatalogOptions options, std::function<qint64()> now)
    : m_transport(transport), m_options(std::move(options)),
      m_now(now ? std::move(now) : [] { return QDateTime::currentMSecsSinceEpoch(); }) {
    const auto master = base(m_options.masterBaseUrl), assets = base(m_options.assetBaseUrl);
    for (const auto &region : regions) {
        m_masterUrls.insert(region, master + "/" + repositories.value(region));
        m_assetUrls.insert(region, assets + "/" + buckets.value(region));
    }
    m_options.coverBaseUrl = base(m_options.coverBaseUrl);
}
void Catalog::clearCache() {
    ++m_epoch;
    m_cache.clear();
    const auto pending = std::exchange(m_pending, {});
    for (const auto &[url, waiters] : pending)
        for (const auto &waiter : waiters)
            waiter(failure("Aborted", ErrorCode::Cancelled));
    m_transport.cancelAll();
}
void Catalog::close() {
    m_closed = true;
    clearCache();
}
void Catalog::table(const QString &url, std::function<void(Result<QJsonValue>)> done) {
    if (m_closed)
        return done(failure("Aborted", ErrorCode::Cancelled));
    if (const auto cached = m_cache.find(url); cached != m_cache.end() && cached->second > m_now())
        return done(cached->first);
    if (auto pending = m_pending.find(url); pending != m_pending.end()) {
        pending->second.append(std::move(done));
        return;
    }
    m_pending[url].append(std::move(done));
    ext::HttpRequest request;
    request.url = QUrl(url);
    request.headers = {{"Accept", "application/json"}};
    request.timeoutMs = m_options.timeoutMs;
    request.maxBytes = 64 * 1024 * 1024;
    request.followRedirects = true;
    const auto epoch = m_epoch;
    m_transport.send(request, [this, url, epoch](Result<ext::HttpResponse> response) {
        auto found = m_pending.find(url);
        if (found == m_pending.end())
            return;
        const auto waiters = std::move(found->second);
        m_pending.erase(found);
        Result<QJsonValue> result = failure("pending");
        if (!response)
            result = failure(response.error().message == "cancelled" ? QStringLiteral("Aborted") : response.error().message,
                             response.error().code);
        else if (response.value().status < 200 || response.value().status >= 300)
            result = failure(QStringLiteral("HTTP %1: %2").arg(response.value().status).arg(url));
        else if (const auto value = ext::parseJson(response.value().body))
            result = *value;
        else
            result = failure("Invalid JSON: " + url);
        if (result && epoch == m_epoch && m_options.cacheTtlMs > 0)
            m_cache.insert(url, {result.value(), m_now() + m_options.cacheTtlMs});
        for (const auto &waiter : waiters)
            waiter(result);
    });
}
void Catalog::listTracks(const QString &region, const QString &length, const QString &format,
                         std::function<void(Result<QList<MusicTrack>>)> done) {
    if (!regions.contains(region))
        return done(failure("Unknown region: " + region));
    struct State {
        QJsonValue tables[4];
        int remaining = 4;
        bool failed = false;
    };
    auto state = std::make_shared<State>();
    const QStringList names{"musics", "musicVocals", "gameCharacters", "outsideCharacters"};
    const auto master = m_masterUrls.value(region);
    auto finish = [this, state, region, length, format, master, names, done] {
        const auto url = [&](int i) { return master + "/" + names[i] + ".json"; };
        const auto musics = parseMusics(state->tables[0], url(0));
        if (!musics)
            return done(musics.error());
        const auto vocals = parseVocals(state->tables[1], url(1));
        if (!vocals)
            return done(vocals.error());
        const auto characters = parseCharacters(state->tables[2], url(2), region == "en");
        if (!characters)
            return done(characters.error());
        const auto outsiders = parseCharacters(state->tables[3], url(3), region == "en");
        if (!outsiders)
            return done(outsiders.error());
        QHash<qint64, Music> songs;
        for (const auto &music : musics.value())
            songs.insert(music.id, music);
        QStringList roots{m_assetUrls.value(region)};
        if (m_options.fallbackToJapaneseAssets && region != "jp")
            roots.append(m_assetUrls.value("jp"));
        QStringList coverRoots{m_options.coverBaseUrl};
        for (const auto &root : roots)
            if (!coverRoots.contains(root))
                coverRoots.append(root);
        QList<MusicTrack> tracks;
        for (const auto &vocal : vocals.value()) {
            if (!songs.contains(vocal.musicId))
                return done(failure(QStringLiteral("Invalid master data (%1/musicVocals): vocal %2 refers to absent music %3")
                                        .arg(region).arg(vocal.id).arg(vocal.musicId)));
            const auto &music = songs[vocal.musicId];
            const auto bundle = segment(vocal.assetbundleName), jacket = segment(music.assetbundleName);
            if (!bundle)
                return done(bundle.error());
            if (!jacket)
                return done(jacket.error());
            MusicTrack track;
            track.id = QStringLiteral("%1:%2").arg(region).arg(vocal.id);
            track.region = region;
            track.musicId = music.id;
            track.vocalId = vocal.id;
            track.title = music.title;
            track.composer = music.composer;
            track.lyricist = music.lyricist;
            track.arranger = music.arranger;
            track.author = music.composer.isEmpty() ? music.lyricist : music.composer;
            for (const auto &character : vocal.characters) {
                const auto &names = character.type == "game_character"      ? characters.value()
                                    : character.type == "outside_character" ? outsiders.value()
                                                                            : QHash<qint64, QString>{};
                const auto name = names.value(character.id);
                if (!name.isEmpty() && !track.singers.contains(name))
                    track.singers.append(name);
            }
            track.artist = track.singers.isEmpty() ? track.author : track.singers.join(" / ");
            track.vocalType = vocal.type;
            track.version = vocal.caption;
            track.assetbundleName = vocal.assetbundleName;
            track.format = format;
            track.length = length;
            const auto filename = bundle.value() + (length == "short" ? "_short" : "") + "." + format;
            for (const auto &root : roots) {
                const auto candidate = QStringLiteral("%1/music/%2/%3/%4").arg(root, length, bundle.value(), filename);
                if (!track.audioCandidates.contains(candidate))
                    track.audioCandidates.append(candidate);
            }
            const auto jacketFile = jacket.value() + (music.id == 19 && vocal.type == "original_song" ? "_org" : "");
            for (const auto &root : coverRoots)
                for (const auto *extension : {"webp", "png"})
                    track.coverCandidates.append(QStringLiteral("%1/music/jacket/%2/%3.%4").arg(root, jacket.value(), jacketFile, extension));
            track.isFullLength = music.isFullLength;
            track.fillerSec = music.fillerSec;
            tracks.append(track);
        }
        done(tracks);
    };
    for (int i = 0; i < 4; ++i)
        table(master + "/" + names[i] + ".json", [state, i, finish, done](Result<QJsonValue> result) {
            if (state->failed)
                return;
            if (!result) {
                state->failed = true;
                return done(result.error());
            }
            state->tables[i] = result.value();
            if (--state->remaining == 0)
                finish();
        });
}
void Catalog::probe(const QString &url, const QString &kind, std::function<void(AssetStatus)> done) {
    if (m_closed)
        return done(AssetStatus::Unknown);
    auto inspect = [kind, done](const ext::HttpResponse &response) {
        const auto type = QString::fromLatin1(response.header("content-type")).section(';', 0, 0).trimmed().toLower();
        const bool mimeOk = type.startsWith(kind + "/") || type == "application/octet-stream";
        done(response.status == 404 || response.status == 410 ? AssetStatus::Missing
             : response.status >= 200 && response.status < 300 && mimeOk ? AssetStatus::Available
                                                                          : AssetStatus::Unknown);
    };
    ext::HttpRequest request;
    request.method = "HEAD";
    request.url = QUrl(url);
    request.timeoutMs = m_options.timeoutMs;
    request.headersOnly = true;
    request.followRedirects = true;
    m_transport.send(request, [this, request, inspect, done](Result<ext::HttpResponse> response) mutable {
        if (!response)
            return done(AssetStatus::Unknown);
        if (response.value().status != 405 && response.value().status != 501)
            return inspect(response.value());
        request.method = "GET";
        request.headers = {{"Range", "bytes=0-0"}};
        m_transport.send(request, [inspect, done](Result<ext::HttpResponse> ranged) {
            if (!ranged)
                return done(AssetStatus::Unknown);
            inspect(ranged.value());
        });
    });
}
void Catalog::resolveFrom(const std::shared_ptr<void> &opaque, int index) {
    const auto state = std::static_pointer_cast<ResolveState>(opaque);
    if (index >= state->urls.size())
        return state->done({}, !state->urls.isEmpty() && state->allMissing ? AssetStatus::Missing : AssetStatus::Unknown);
    probe(state->urls.at(index), state->kind, [this, opaque, state, index](AssetStatus status) {
        if (status == AssetStatus::Available)
            return state->done(state->urls.at(index), status);
        if (status != AssetStatus::Missing)
            state->allMissing = false;
        resolveFrom(opaque, index + 1);
    });
}
void Catalog::resolve(const QStringList &candidates, const QString &kind, std::function<void(QString, AssetStatus)> done) {
    auto state = std::make_shared<ResolveState>();
    for (const auto &url : candidates)
        if (!state->urls.contains(url))
            state->urls.append(url);
    state->kind = kind;
    state->done = std::move(done);
    resolveFrom(state, 0);
}
} // namespace sekai
