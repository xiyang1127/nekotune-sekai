#include "provider.h"
#include "common/media.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>

using namespace nekotune;

namespace sekai {
namespace {
constexpr int pageSize = 40;
constexpr qint64 cacheTtl = 15 * 60'000;
AppError fail(const QString &key) { return failure("sekai_" + key); }
QString normalize(const QString &value) { return value.normalized(QString::NormalizationForm_KC).toLower(); }
Result<QString> baseUrl(const QVariant &value) {
    if (value.isValid() && value.typeId() != QMetaType::QString)
        return fail("invalid_config");
    const auto trimmed = value.toString().trimmed();
    if (trimmed.isEmpty())
        return QString();
    const QUrl url(trimmed, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() || !url.userName().isEmpty() ||
        !url.password().isEmpty() || url.hasQuery() || url.hasFragment())
        return fail("invalid_config");
    auto text = url.toString(QUrl::FullyEncoded);
    while (text.endsWith('/'))
        text.chop(1);
    return text;
}
} // namespace

QVariantMap Configuration::toMap() const {
    return {{"master_url", masterUrl}, {"asset_url", assetUrl},   {"cover_url", coverUrl},
            {"region", region},        {"format", format},        {"length", length},
            {"fallback_jp", fallbackJp}, {"trim_downloads", trimDownloads}};
}
Result<Configuration> configuration(const QVariantMap &value) {
    Configuration config;
    for (const auto &[key, target] : {std::pair{"master_url", &config.masterUrl}, {"asset_url", &config.assetUrl},
                                      {"cover_url", &config.coverUrl}}) {
        const auto url = baseUrl(value.value(key));
        if (!url)
            return url.error();
        *target = url.value().isEmpty() ? recommendedUrls.value(key).toString() : url.value();
    }
    const auto region = value.value("region", "jp"), format = value.value("format", "mp3"), length = value.value("length", "long");
    const auto fallback = value.value("fallback_jp", true), trim = value.value("trim_downloads", true);
    if (fallback.typeId() != QMetaType::Bool || trim.typeId() != QMetaType::Bool || region.typeId() != QMetaType::QString ||
        !regions.contains(region.toString()) || format.typeId() != QMetaType::QString ||
        !QStringList{"mp3", "flac", "wav"}.contains(format.toString()) || length.typeId() != QMetaType::QString ||
        !QStringList{"long", "short"}.contains(length.toString()))
        return fail("invalid_config");
    config.region = region.toString();
    config.format = format.toString();
    config.length = length.toString();
    config.fallbackJp = fallback.toBool();
    config.trimDownloads = trim.toBool();
    return config;
}
Result<TrackId> parseId(const QString &id) {
    static const QRegularExpression pattern("^(jp|en|cn|tw|kr):([1-9]\\d*):(long|short)$");
    const auto match = pattern.match(id);
    bool ok = false;
    const auto vocal = match.hasMatch() ? match.captured(2).toLongLong(&ok) : 0;
    if (!match.hasMatch() || !ok || vocal > 9007199254740991LL)
        return fail("invalid_track");
    return TrackId{match.captured(1), vocal, match.captured(3)};
}
QVariantMap hostTrack(const MusicTrack &track) {
    return {{"id", track.id + ":" + track.length},
            {"title", track.title},
            {"artist", track.artist},
            {"album", QStringList{"Project SEKAI", track.region.toUpper(), track.version.isEmpty() ? track.vocalType : track.version,
                                  track.length}
                          .join(" · ")},
            {"cover_url", track.coverCandidates.value(0)},
            {"region", track.region},
            {"music_id", track.musicId},
            {"vocal_id", track.vocalId},
            {"version", track.version},
            {"vocal_type", track.vocalType},
            {"author", track.author},
            {"composer", track.composer},
            {"lyricist", track.lyricist},
            {"arranger", track.arranger},
            {"singers", track.singers},
            {"length", track.length},
            {"is_full_length", track.isFullLength},
            {"filler_sec", track.fillerSec}};
}

Provider::Provider(Configuration config, ext::HttpTransport &transport, std::function<qint64()> now)
    : m_config(std::move(config)), m_transport(transport),
      m_now(now ? std::move(now) : [] { return QDateTime::currentMSecsSinceEpoch(); }) {
    CatalogOptions options;
    options.masterBaseUrl = m_config.masterUrl;
    options.assetBaseUrl = m_config.assetUrl;
    options.coverBaseUrl = m_config.coverUrl;
    options.fallbackToJapaneseAssets = m_config.fallbackJp;
    options.timeoutMs = 6000;
    options.cacheTtlMs = cacheTtl;
    m_client = std::make_unique<Catalog>(transport, options, m_now);
}
void Provider::close() {
    m_closed = true;
    const auto catalogs = std::exchange(m_catalogs, {});
    for (const auto &cached : catalogs)
        for (const auto &waiter : std::exchange(cached->waiters, {}))
            waiter(failure("Aborted", ErrorCode::Cancelled));
    m_client->close();
}
void Provider::catalog(const QString &region, const QString &length, std::function<void(Result<QList<MusicTrack>>)> done) {
    if (m_closed)
        return done(failure("Aborted", ErrorCode::Cancelled));
    const auto key = region + ":" + length;
    if (const auto cached = m_catalogs.value(key); cached && cached->expires > m_now()) {
        if (cached->ready)
            return done(cached->tracks);
        cached->waiters.append(std::move(done));
        return;
    }
    auto entry = std::make_shared<Cached>();
    entry->expires = m_now() + cacheTtl;
    entry->waiters.append(std::move(done));
    m_catalogs.insert(key, entry);
    m_client->listTracks(region, length, m_config.format, [this, key, entry](Result<QList<MusicTrack>> tracks) {
        if (tracks) {
            entry->ready = true;
            entry->tracks = tracks.value();
        } else if (m_catalogs.value(key) == entry)
            m_catalogs.remove(key);
        for (const auto &waiter : std::exchange(entry->waiters, {}))
            waiter(tracks);
    });
}
void Provider::search(const QVariantMap &params, Reply done) {
    const auto query = params.value("query", QString()), cursor = params.value("cursor", "1"),
               region = params.value("region", m_config.region), vocalType = params.value("vocal_type", QString());
    static const QRegularExpression cursorPattern("^[1-9]\\d{0,5}$");
    if (query.typeId() != QMetaType::QString || query.toString().size() > 128 || cursor.typeId() != QMetaType::QString ||
        !cursorPattern.match(cursor.toString()).hasMatch() || !regions.contains(region.toString()) ||
        vocalType.typeId() != QMetaType::QString || vocalType.toString().size() > 128)
        return done(fail("invalid_query"));
    const auto needle = normalize(query.toString().trimmed());
    const int page = cursor.toString().toInt();
    catalog(region.toString(), m_config.length, [needle, page, type = vocalType.toString(), done](Result<QList<MusicTrack>> tracks) {
        if (!tracks)
            return done(tracks.error());
        QList<MusicTrack> filtered;
        QSet<QString> types;
        for (const auto &track : tracks.value()) {
            types.insert(track.vocalType);
            if (!type.isEmpty() && track.vocalType != type)
                continue;
            QStringList fields{track.title, track.author, track.lyricist, track.arranger, track.version, QString::number(track.musicId)};
            fields += track.singers;
            if (std::any_of(fields.cbegin(), fields.cend(), [&](const QString &field) { return normalize(field).contains(needle); }))
                filtered.append(track);
        }
        std::stable_sort(filtered.begin(), filtered.end(), [](const auto &a, const auto &b) {
            return a.musicId != b.musicId ? a.musicId > b.musicId : a.vocalId < b.vocalId;
        });
        const qsizetype offset = qsizetype(page - 1) * pageSize;
        QVariantList result;
        for (qsizetype i = offset; i < qMin(offset + pageSize, filtered.size()); ++i)
            result.append(hostTrack(filtered[i]));
        QStringList sortedTypes(types.cbegin(), types.cend());
        sortedTypes.sort();
        done(QVariantMap{{"tracks", result},
                         {"cursor", offset + pageSize < filtered.size() ? QString::number(page + 1) : QString()},
                         {"page", page},
                         {"total", filtered.size()},
                         {"vocal_types", sortedTypes}});
    });
}
void Provider::rawTrack(const QString &id, std::function<void(Result<MusicTrack>)> done) {
    const auto parsed = parseId(id);
    if (!parsed)
        return done(parsed.error());
    catalog(parsed.value().region, parsed.value().length, [vocal = parsed.value().vocalId, done](Result<QList<MusicTrack>> tracks) {
        if (!tracks)
            return done(tracks.error());
        for (const auto &track : tracks.value())
            if (track.vocalId == vocal)
                return done(track);
        done(fail("not_found"));
    });
}
void Provider::track(const QString &id, Reply done) {
    rawTrack(id, [done](Result<MusicTrack> track) {
        done(track ? Result<QVariantMap>(hostTrack(track.value())) : Result<QVariantMap>(track.error()));
    });
}
void Provider::resolve(const QString &id, Reply done) {
    rawTrack(id, [this, done](Result<MusicTrack> track) {
        if (!track)
            return done(track.error());
        // A missing or slow jacket must not prevent playback of verified audio.
        m_client->resolve(track.value().audioCandidates, "audio", [this, track = track.value(), done](QString url, AssetStatus status) {
            if (url.isEmpty())
                return done(fail(status == AssetStatus::Missing ? "audio_missing" : "audio_unavailable"));
            QStringList hosts;
            for (const auto &candidate : track.audioCandidates)
                if (const auto host = QUrl(candidate).host(); !hosts.contains(host))
                    hosts.append(host);
            done(QVariantMap{{"url", url},
                             {"extension", track.format},
                             {"expiresAt", m_now() + 5 * 60'000},
                             {"allowedHosts", hosts},
                             {"maxBytes", 256LL * 1024 * 1024}});
        });
    });
}
void Provider::downloaded(const QString &id, const QString &audioPath, std::function<void(std::function<void(std::optional<bool>)>)> hasCustomLyrics,
                          Reply done) {
    rawTrack(id, [this, audioPath, hasCustomLyrics, done](Result<MusicTrack> track) {
        auto finishCover = [audioPath, hasCustomLyrics, done](QVariantMap cover) {
            hasCustomLyrics([audioPath, cover, done](std::optional<bool> custom) {
                auto result = cover;
                const auto lyrics = saveDefaultLyrics(audioPath, custom);
                for (auto it = lyrics.cbegin(); it != lyrics.cend(); ++it)
                    if (it.key() != "asset_warning")
                        result.insert(it.key(), it.value());
                QStringList warnings;
                for (const auto &warning : {cover.value("asset_warning").toString(), lyrics.value("asset_warning").toString()})
                    if (!warning.isEmpty())
                        warnings.append(warning);
                result.remove("asset_warning");
                if (!warnings.isEmpty())
                    result.insert("asset_warning", warnings.join(','));
                done(result);
            });
        };
        if (!track)
            return finishCover({{"cover_status", "error"}, {"asset_warning", "sekai_cover_failed"}});
        saveCover(m_transport, audioPath, track.value().coverCandidates, [finishCover](Result<QVariantMap> cover) {
            finishCover(cover ? cover.value() : QVariantMap{{"cover_status", "error"}, {"asset_warning", "sekai_cover_failed"}});
        });
    });
}

namespace {
struct CoverState {
    QStringList urls;
    QSet<QString> origins;
    QString base;
    Reply done;
};
QString origin(const QUrl &url) { return url.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toString(); }
QString existingCover(const QString &base) {
    for (const auto *suffix : {"jpg", "jpeg", "png", "webp"})
        if (ext::pathExists(base + "." + suffix))
            return base + "." + suffix;
    return {};
}
void downloadImage(ext::HttpTransport &transport, const std::shared_ptr<CoverState> &state, QUrl url, int redirects,
                   std::function<void(Result<std::pair<QByteArray, QString>>)> done) {
    if (redirects > 3 || url.scheme() != "https" || !url.userName().isEmpty() || !url.password().isEmpty() ||
        !state->origins.contains(origin(url)))
        return done(fail("invalid_cover"));
    ext::HttpRequest request;
    request.url = url;
    request.headers = {{"Accept", "image/webp,image/png,image/jpeg"}};
    request.timeoutMs = 8000;
    request.maxBytes = 5 * 1024 * 1024;
    transport.send(request, [&transport, state, url, redirects, done](Result<ext::HttpResponse> response) {
        if (!response)
            return done(fail("invalid_cover"));
        const auto &value = response.value();
        if (QList<int>{301, 302, 303, 307, 308}.contains(value.status)) {
            const auto location = value.header("location");
            if (location.isEmpty())
                return done(fail("invalid_cover"));
            return downloadImage(transport, state, url.resolved(QUrl(QString::fromUtf8(location))), redirects + 1, done);
        }
        const auto mime = QString::fromLatin1(value.header("content-type")).section(';', 0, 0).trimmed().toLower();
        if (value.status < 200 || value.status >= 300 || !QStringList{"image/webp", "image/png", "image/jpeg"}.contains(mime))
            return done(fail("invalid_cover"));
        const auto suffix = ext::imageSuffix(value.body, mime);
        if (suffix.isEmpty())
            return done(fail("invalid_cover"));
        done(std::pair{value.body, suffix});
    });
}
void tryCover(ext::HttpTransport &transport, const std::shared_ptr<CoverState> &state, int index) {
    if (index >= state->urls.size())
        return state->done(QVariantMap{{"cover_status", "error"}, {"asset_warning", "sekai_cover_failed"}});
    downloadImage(transport, state, QUrl(state->urls[index]), 0, [&transport, state, index](Result<std::pair<QByteArray, QString>> image) {
        if (!image)
            return tryCover(transport, state, index + 1);
        // A user may have saved artwork while the remote request was in flight.
        if (!existingCover(state->base).isEmpty())
            return state->done(QVariantMap{{"cover_status", "existing"}});
        const auto target = state->base + "." + image.value().second;
        const auto saved = ext::publishFile(target, image.value().first);
        if (!saved)
            return state->done(QVariantMap{{"cover_status", "error"}, {"asset_warning", "sekai_cover_failed"}});
        if (saved.value() == "existing")
            return state->done(QVariantMap{{"cover_status", "existing"}});
        state->done(QVariantMap{{"cover_status", "saved"}, {"cover_path", target}});
    });
}
} // namespace

void saveCover(ext::HttpTransport &transport, const QString &audioPath, const QStringList &candidates, Reply done) {
    const QFileInfo audio(audioPath);
    auto state = std::make_shared<CoverState>();
    state->base = audio.dir().filePath(audio.completeBaseName());
    state->done = std::move(done);
    if (!existingCover(state->base).isEmpty())
        return state->done(QVariantMap{{"cover_status", "existing"}});
    if (!audio.isFile())
        return state->done(QVariantMap{{"cover_status", "error"}, {"asset_warning", "sekai_cover_failed"}});
    for (const auto &url : candidates)
        if (!state->urls.contains(url)) {
            state->urls.append(url);
            state->origins.insert(origin(QUrl(url)));
        }
    if (state->urls.isEmpty())
        return state->done(QVariantMap{{"cover_status", "none"}});
    tryCover(transport, state, 0);
}
QVariantMap saveDefaultLyrics(const QString &audioPath, std::optional<bool> hasCustomLyrics) {
    const QFileInfo audio(audioPath);
    const auto name = audio.completeBaseName();
    const auto exists = [&] {
        for (const auto &file : audio.dir().entryInfoList(QDir::Files | QDir::System | QDir::Hidden))
            if (file.completeBaseName().compare(name, Qt::CaseInsensitive) == 0 &&
                QStringList{"lrc", "krc"}.contains(file.suffix().toLower()))
                return true;
        return false;
    };
    if (exists() || hasCustomLyrics.value_or(false))
        return {{"lyrics_status", "existing"}};
    if (!hasCustomLyrics || !audio.isFile())
        return {{"lyrics_status", "error"}, {"asset_warning", "sekai_default_lyrics_failed"}};
    const auto target = audio.dir().filePath(name + ".lrc");
    const auto saved = ext::publishFile(target, defaultLyrics.toUtf8());
    if (!saved)
        return {{"lyrics_status", "error"}, {"asset_warning", "sekai_default_lyrics_failed"}};
    if (saved.value() == "existing")
        return {{"lyrics_status", "existing"}};
    return {{"lyrics_status", "saved"}, {"lyrics_path", target}};
}
} // namespace sekai
