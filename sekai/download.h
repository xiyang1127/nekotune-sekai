#pragma once
#include <nekotune/result.h>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantMap>
#include <functional>

class QProcess;

namespace sekai {
/// Seconds of leading silence to cut, from FFmpeg silencedetect output; 0 keeps the file.
double leadingSilence(const QString &log, double limit);

/// Downloads an audio file and trims silence before the music starts, with ffmpeg/ffprobe from
/// PATH. Results are files inside directory, which the host copies into the library.
class DownloadPreparer final : public QObject {
  public:
    using Done = std::function<void(nekotune::Result<QVariantMap>)>;
    explicit DownloadPreparer(QString directory, std::function<void(const QString &)> log, QObject *parent = nullptr);
    ~DownloadPreparer() override;
    /// audio is a resolved ResolvedAudio map (https URL and allowedHosts). Replies {url: file://, extension}.
    void prepare(const QVariantMap &audio, double fillerSec, Done done);
    /// Trim an existing local file; replies {file, extension, trimmed}.
    void trim(const QString &input, const QString &workDirectory, double fillerSec, Done done);
    void close();
    /// Remove prepared files; the host has copied them once its downloaded hook runs.
    void discardPrepared();

  private:
    struct Probe {
        QString extension;
        double duration = 0;
    };
    void run(const QString &program, const QStringList &arguments, std::function<void(nekotune::Result<QString>)> done);
    void probe(const QString &file, std::function<void(nekotune::Result<Probe>)> done);
    void fetch(QUrl url, const QStringList &allowedHosts, const QString &target, int redirects,
               std::function<void(nekotune::Result<void>)> done);
    void collect();

    QString m_directory;
    std::function<void(const QString &)> m_log;
    QNetworkAccessManager m_network;
    QSet<QProcess *> m_processes;
    QSet<QObject *> m_replies;
    bool m_closed = false;
};
} // namespace sekai
