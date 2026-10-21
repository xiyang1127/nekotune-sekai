#pragma once
#include "catalog.h"
#include <QVariantMap>
#include <memory>
#include <optional>

namespace sekai {
using Reply = std::function<void(Result<QVariantMap>)>;
struct Configuration {
    QString masterUrl, assetUrl, coverUrl, region = "jp", format = "mp3", length = "long";
    bool fallbackJp = true, trimDownloads = true;
    QVariantMap toMap() const;
};
inline const QVariantMap recommendedUrls{{"master_url", "https://sekai-world.github.io"},
                                         {"asset_url", "https://storage.sekai.best"},
                                         {"cover_url", "https://storage.exmeaning.com/sekai-cn-assets"}};
/// Blank mirrors fall back to the recommended defaults; fails with sekai_invalid_config.
Result<Configuration> configuration(const QVariantMap &value = {});
struct TrackId {
    QString region;
    qint64 vocalId = 0;
    QString length;
};
Result<TrackId> parseId(const QString &id);
QVariantMap hostTrack(const MusicTrack &track);

class Provider {
  public:
    Provider(Configuration config, nekotune::ext::HttpTransport &transport, std::function<qint64()> now = {});
    const Configuration &config() const { return m_config; }
    void close();
    void search(const QVariantMap &params, Reply done);
    void rawTrack(const QString &id, std::function<void(Result<MusicTrack>)> done);
    void track(const QString &id, Reply done);
    /// ResolvedAudio map for the first available audio candidate.
    void resolve(const QString &id, Reply done);
    /// Cover and default-lyrics sidecars next to a downloaded file.
    void downloaded(const QString &id, const QString &audioPath, std::function<void(std::function<void(std::optional<bool>)>)> hasCustomLyrics,
                    Reply done);
    nekotune::ext::HttpTransport &transport() { return m_transport; }

  private:
    void catalog(const QString &region, const QString &length, std::function<void(Result<QList<MusicTrack>>)> done);

    Configuration m_config;
    nekotune::ext::HttpTransport &m_transport;
    std::function<qint64()> m_now;
    std::unique_ptr<Catalog> m_client;
    struct Cached {
        qint64 expires = 0;
        bool ready = false;
        QList<MusicTrack> tracks;
        QList<std::function<void(Result<QList<MusicTrack>>)>> waiters;
    };
    QHash<QString, std::shared_ptr<Cached>> m_catalogs;
    bool m_closed = false;
};

/// Download jacket art (webp/png/jpeg, at most 5 MiB) from the candidates' origins only.
void saveCover(nekotune::ext::HttpTransport &transport, const QString &audioPath, const QStringList &candidates, Reply done);
inline const QString defaultLyrics = "[00:00.000]暂无歌词\n";
/// Write the placeholder .lrc unless a lyric sidecar or custom lyrics already exist.
/// hasCustomLyrics is empty when song metadata could not be read.
QVariantMap saveDefaultLyrics(const QString &audioPath, std::optional<bool> hasCustomLyrics = false);
} // namespace sekai
