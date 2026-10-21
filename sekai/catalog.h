#pragma once
#include "common/http.h"
#include <QHash>
#include <QJsonValue>
#include <QStringList>
#include <QVariant>
#include <functional>
#include <map>
#include <memory>

namespace sekai {
using nekotune::Result;
inline const QStringList regions{"jp", "en", "cn", "tw", "kr"};

/// One vocal version of a song in one region (C++ port of the sekai-music MusicTrack subset the
/// plugin uses).
struct MusicTrack {
    QString id; // "<region>:<vocalId>"
    QString region;
    qint64 musicId = 0, vocalId = 0;
    QString title, author, composer, lyricist, arranger;
    QStringList singers;
    QString artist, vocalType, version, assetbundleName, format, length;
    QStringList audioCandidates, coverCandidates;
    QVariant isFullLength; // bool or null
    double fillerSec = 0;
};
struct CatalogOptions {
    QString masterBaseUrl = "https://sekai-world.github.io";
    QString assetBaseUrl = "https://storage.sekai.best";
    QString coverBaseUrl = "https://storage.exmeaning.com/sekai-cn-assets";
    bool fallbackToJapaneseAssets = true;
    int timeoutMs = 6000;
    qint64 cacheTtlMs = 15 * 60'000;
};
enum class AssetStatus { Available, Missing, Unknown };

/// Remote Project SEKAI master data and asset URL resolution.
class Catalog {
  public:
    Catalog(nekotune::ext::HttpTransport &transport, CatalogOptions options, std::function<qint64()> now = {});
    void clearCache();
    /// Cancel in-flight requests and refuse new ones.
    void close();
    void listTracks(const QString &region, const QString &length, const QString &format,
                    std::function<void(Result<QList<MusicTrack>>)> done);
    /// First available URL among candidates; status is Missing only when every probe was 404/410.
    void resolve(const QStringList &candidates, const QString &kind,
                 std::function<void(QString url, AssetStatus status)> done);

  private:
    void table(const QString &url, std::function<void(Result<QJsonValue>)> done);
    void probe(const QString &url, const QString &kind, std::function<void(AssetStatus)> done);
    void resolveFrom(const std::shared_ptr<void> &state, int index);

    nekotune::ext::HttpTransport &m_transport;
    CatalogOptions m_options;
    std::function<qint64()> m_now;
    QHash<QString, QString> m_masterUrls, m_assetUrls;
    QHash<QString, std::pair<QJsonValue, qint64>> m_cache;
    std::map<QString, QList<std::function<void(Result<QJsonValue>)>>> m_pending;
    quint64 m_epoch = 0;
    bool m_closed = false;
};
} // namespace sekai
