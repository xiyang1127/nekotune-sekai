#include "extension.h"
#include <QDir>

using namespace nekotune;

namespace sekai {
namespace {
sdk::Track toTrack(const QVariantMap &map) {
    return {map.value("id").toString(),       map.value("title").toString(),     map.value("artist").toString(),
            map.value("album").toString(),    map.value("cover_url").toString(), 0};
}
} // namespace

class Music final : public sdk::IMusicProvider {
  public:
    explicit Music(Extension &extension) : m_extension(extension) {}
    void search(const QString &query, const QString &cursor, std::function<void(Result<sdk::SearchPage>)> done) override {
        QVariantMap params{{"query", query}};
        if (!cursor.isEmpty())
            params.insert("cursor", cursor);
        m_extension.m_provider->search(params, [done](Result<QVariantMap> result) {
            if (!result)
                return done(result.error());
            sdk::SearchPage page;
            for (const auto &track : result.value().value("tracks").toList())
                page.tracks.append(toTrack(track.toMap()));
            page.cursor = result.value().value("cursor").toString();
            done(page);
        });
    }
    void track(const QString &id, std::function<void(Result<sdk::Track>)> done) override {
        m_extension.m_provider->track(id, [done](Result<QVariantMap> result) {
            done(result ? Result<sdk::Track>(toTrack(result.value())) : Result<sdk::Track>(result.error()));
        });
    }
    void resolve(const QString &id, const QString &purpose, std::function<void(Result<sdk::ResolvedAudio>)> done) override {
        auto &provider = *m_extension.m_provider;
        provider.resolve(id, [this, &provider, id, purpose, done](Result<QVariantMap> audio) {
            if (!audio)
                return done(audio.error());
            if (purpose != "download" || !provider.config().trimDownloads)
                return done(toAudio(audio.value()));
            provider.rawTrack(id, [this, audio = audio.value(), done](Result<MusicTrack> track) {
                if (!track)
                    return done(track.error());
                if (track.value().length != "long")
                    return done(toAudio(audio));
                m_extension.m_downloads->prepare(audio, track.value().fillerSec, [done](Result<QVariantMap> prepared) {
                    done(prepared ? Result<sdk::ResolvedAudio>(toAudio(prepared.value())) : Result<sdk::ResolvedAudio>(prepared.error()));
                });
            });
        });
    }
    void downloaded(const QString &id, const QString &path, int songId, bool, sdk::Reply done) override {
        m_extension.m_downloads->discardPrepared();
        auto &context = m_extension.m_context;
        m_extension.m_provider->downloaded(
            id, path,
            [&context, songId](std::function<void(std::optional<bool>)> answer) {
                context.hostCall("song.metadata", {{"song_id", songId}}, [answer](Result<sdk::Params> metadata) {
                    if (!metadata)
                        return answer(std::nullopt);
                    answer(!metadata.value().value("lyrics").toString().trimmed().isEmpty());
                });
            },
            done);
    }

  private:
    static sdk::ResolvedAudio toAudio(const QVariantMap &map) {
        sdk::ResolvedAudio audio;
        audio.url = QUrl(map.value("url").toString());
        audio.extension = map.value("extension").toString();
        audio.expiresAt = map.value("expiresAt").toLongLong();
        audio.allowedHosts = map.value("allowedHosts").toStringList();
        audio.maxBytes = map.value("maxBytes").toLongLong();
        return audio;
    }
    Extension &m_extension;
};

Extension::Extension(sdk::IExtensionContext &context, ext::HttpTransport &transport) : m_context(context), m_transport(transport) {}
Extension::~Extension() { close(); }
void Extension::close() {
    if (m_closed)
        return;
    m_closed = true;
    m_registrations.clear();
    if (m_provider)
        m_provider->close();
    if (m_downloads)
        m_downloads->close();
}
void Extension::reset() {
    if (m_provider) {
        m_provider->close();
        m_retired.push_back(std::move(m_provider));
    }
    const auto config = configuration(m_context.config());
    m_provider = std::make_unique<Provider>(config ? config.value() : Configuration{}, m_transport);
}
void Extension::service(const QString &id, sdk::Handler handler) {
    m_registrations.push_back(m_context.registerService(id, std::move(handler)));
}
void Extension::activate(std::function<void(Result<void>)> done) {
    const auto config = configuration(m_context.config());
    if (!config)
        return done(config.error());
    auto start = [this, config = config.value(), done] {
        m_provider = std::make_unique<Provider>(config, m_transport);
        m_downloads = std::make_unique<DownloadPreparer>(QDir(m_context.dataDirectory()).filePath("audio-preparation"),
                                                         [this](const QString &message) { m_context.log(QtInfoMsg, message); });
        m_music = std::make_unique<Music>(*this);
        m_registrations.push_back(
            m_context.registerMusicProvider("music", m_music.get(), {{"name", "Project SEKAI"}, {"download", true}, {"page", "music"}}));
        service("status", [this](const sdk::Params &, sdk::Reply reply) { reply(m_provider->config().toMap()); });
        service("defaults", [](const sdk::Params &, sdk::Reply reply) { reply(recommendedUrls); });
        service("config.set", [this](const sdk::Params &params, sdk::Reply reply) {
            const auto next = configuration(params);
            if (!next)
                return reply(next.error());
            const auto map = next.value().toMap();
            m_context.updateConfig(map, [this, map, reply](Result<void> saved) {
                if (!saved)
                    return reply(saved.error());
                reset();
                m_context.publish("status", map);
                reply(map);
            });
        });
        service("search", [this](const sdk::Params &params, sdk::Reply reply) { m_provider->search(params, reply); });
        service("cache.clear", [this](const sdk::Params &, sdk::Reply reply) {
            reset();
            reply(sdk::Params{{"cleared", true}});
        });
        m_registrations.push_back(m_context.on("extension.config_changed", [this](const sdk::Params &) {
            if (m_closed)
                return;
            reset();
            m_context.publish("status", m_provider->config().toMap());
        }));
        done({});
    };
    // Upgrade the initial setup gate while retaining any custom mirrors and audio preferences.
    if (config.value().toMap() == m_context.config())
        return start();
    m_context.updateConfig(config.value().toMap(), [start, done](Result<void> saved) {
        if (!saved)
            return done(saved);
        start();
    });
}
} // namespace sekai
