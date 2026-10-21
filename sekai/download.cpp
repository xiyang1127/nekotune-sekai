#include "download.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <cmath>
#include <memory>

using namespace nekotune;

namespace sekai {
namespace {
constexpr qint64 maxBytes = 256LL * 1024 * 1024;
AppError fail(const QString &key) { return failure("sekai_" + key); }
} // namespace

double leadingSilence(const QString &log, double limit) {
    static const QRegularExpression start(R"(silence_start:\s*(-?[\d.]+))");
    static const QRegularExpression end(R"(silence_end:\s*([\d.]+))");
    const auto begin = start.match(log);
    if (!begin.hasMatch() || begin.captured(1).toDouble() > 0.001)
        return 0;
    const auto finish = end.match(log, begin.capturedStart());
    const double seconds = finish.hasMatch() ? finish.captured(1).toDouble() : 0;
    // FFmpeg also reports a silence_end when the probe stops: that is not an observed music onset.
    return seconds >= 0.5 && seconds < limit - 0.1 ? qMax(0.0, seconds - 0.03) : 0;
}

DownloadPreparer::DownloadPreparer(QString directory, std::function<void(const QString &)> log, QObject *parent)
    : QObject(parent), m_directory(std::move(directory)), m_log(std::move(log)) {
    collect();
}
DownloadPreparer::~DownloadPreparer() { close(); }
void DownloadPreparer::close() {
    m_closed = true;
    for (auto *process : QSet(m_processes))
        process->kill();
    for (auto *reply : QSet(m_replies))
        static_cast<QNetworkReply *>(reply)->abort();
}
void DownloadPreparer::discardPrepared() {
    for (const auto &file : QDir(m_directory).entryInfoList({"prepared-*"}, QDir::Files))
        QFile::remove(file.filePath());
}
void DownloadPreparer::collect() {
    // The host copies a prepared file immediately; anything older is a leftover.
    const QDir directory(m_directory);
    for (const auto &entry : directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot))
        if (entry.lastModified().msecsTo(QDateTime::currentDateTime()) > 30 * 60'000) {
            if (entry.isDir() && !entry.isSymLink())
                QDir(entry.filePath()).removeRecursively();
            else
                QFile::remove(entry.filePath());
        }
}
void DownloadPreparer::run(const QString &program, const QStringList &arguments, std::function<void(Result<QString>)> done) {
    if (m_closed)
        return done(fail("trim_failed"));
    const auto executable = QStandardPaths::findExecutable(program);
    if (executable.isEmpty())
        return done(fail("ffmpeg_missing"));
    auto *process = new QProcess(this);
    m_processes.insert(process);
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto output = std::make_shared<QByteArray>();
    connect(process, &QProcess::readyRead, this, [process, output] {
        *output = (*output + process->readAll()).right(128 * 1024);
    });
    auto finished = std::make_shared<bool>(false);
    auto complete = [this, process, output, finished, done](bool ok) {
        if (*finished)
            return;
        *finished = true;
        m_processes.remove(process);
        process->deleteLater();
        *output += process->readAll();
        if (ok)
            done(QString::fromUtf8(*output));
        else
            done(fail("trim_failed"));
    };
    connect(process, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart || error == QProcess::Crashed)
            complete(false);
    });
    connect(process, &QProcess::finished, this, [complete](int code, QProcess::ExitStatus status) {
        complete(status == QProcess::NormalExit && code == 0);
    });
    process->start(executable, arguments);
}
void DownloadPreparer::probe(const QString &file, std::function<void(Result<Probe>)> done) {
    run("ffprobe",
        {"-v", "error", "-select_streams", "a:0", "-show_entries", "stream=codec_name:format=duration", "-of", "json", file},
        [done](Result<QString> output) {
            if (!output)
                return done(output.error());
            // ffprobe may print warnings around the JSON on merged output; keep the object.
            const auto text = output.value();
            const auto json = QJsonDocument::fromJson(text.mid(text.indexOf('{'), text.lastIndexOf('}') - text.indexOf('{') + 1).toUtf8())
                                  .object();
            const auto codec = json.value("streams").toArray().first().toObject().value("codec_name").toString();
            const auto extension = codec == "mp3" ? "mp3" : codec == "flac" ? "flac" : codec.startsWith("pcm_") ? "wav"
                                   : codec == "vorbis" ? "ogg" : codec == "opus" ? "opus" : codec == "aac" ? "m4a" : "";
            bool ok = false;
            const double duration = json.value("format").toObject().value("duration").toString().toDouble(&ok);
            if (QString(extension).isEmpty() || !ok || duration <= 0)
                return done(fail("trim_failed"));
            done(Probe{extension, duration});
        });
}
void DownloadPreparer::trim(const QString &input, const QString &workDirectory, double fillerSec, Done done) {
    probe(input, [this, input, workDirectory, fillerSec, done](Result<Probe> source) {
        if (!source)
            return done(source.error());
        const double hint = std::isfinite(fillerSec) && fillerSec > 0 ? fillerSec : 0;
        const double limit = qMin(source.value().duration, qMin(30.0, qMax(12.0, hint + 3)));
        run("ffmpeg",
            {"-hide_banner", "-nostdin", "-i", input, "-t", QString::number(limit), "-af", "silencedetect=noise=-60dB:d=0.05", "-f",
             "null", "-"},
            [this, input, workDirectory, source = source.value(), limit, done](Result<QString> log) {
                if (!log)
                    return done(log.error());
                const double trimmed = leadingSilence(log.value(), limit);
                if (trimmed == 0)
                    return done(QVariantMap{{"file", input}, {"extension", source.extension}, {"trimmed", 0.0}});
                const auto output = QDir(workDirectory).filePath("trimmed." + source.extension);
                run("ffmpeg",
                    {"-hide_banner", "-nostdin", "-v", "error", "-i", input, "-ss", QString::number(trimmed, 'f', 6), "-map", "0:a:0",
                     "-map_metadata", "0", "-vn", "-c:a", source.extension == "flac" ? "flac" : "copy", "-y", output},
                    [this, output, source, trimmed, done](Result<QString> written) {
                        if (!written)
                            return done(written.error());
                        probe(output, [output, source, trimmed, done](Result<Probe> result) {
                            // Validate before the host sees any bytes: its hash and duration describe the final file.
                            if (!result || QFileInfo(output).size() < 64 || result.value().duration < 0.1 ||
                                qAbs(result.value().duration - (source.duration - trimmed)) > 0.3)
                                return done(fail("trim_failed"));
                            done(QVariantMap{{"file", output}, {"extension", result.value().extension}, {"trimmed", trimmed}});
                        });
                    });
            });
    });
}
void DownloadPreparer::fetch(QUrl url, const QStringList &allowedHosts, const QString &target, int redirects,
                             std::function<void(Result<void>)> done) {
    if (redirects > 5 || url.scheme() != "https" || !url.userName().isEmpty() || !url.password().isEmpty() ||
        (url.port() != -1 && url.port() != 443) || !allowedHosts.contains(url.host()))
        return done(fail("audio_unavailable"));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(60'000);
    auto *reply = m_network.get(request);
    m_replies.insert(reply);
    auto file = std::make_shared<QFile>(target);
    auto received = std::make_shared<qint64>(0);
    auto finished = std::make_shared<bool>(false);
    auto complete = [this, reply, file, finished, done](Result<void> result) {
        if (*finished)
            return;
        *finished = true;
        m_replies.remove(reply);
        reply->disconnect(this);
        if (!reply->isFinished())
            reply->abort();
        reply->deleteLater();
        file->close();
        done(result);
    };
    connect(reply, &QNetworkReply::readyRead, this, [reply, file, received, complete] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status < 200 || status >= 300)
            return; // redirects and errors are handled when finished
        if (!file->isOpen() && !file->open(QIODevice::WriteOnly | QIODevice::Truncate))
            return complete(fail("audio_unavailable"));
        const auto chunk = reply->readAll();
        *received += chunk.size();
        if (*received > maxBytes)
            return complete(fail("download_too_large"));
        if (file->write(chunk) != chunk.size())
            complete(fail("audio_unavailable"));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, allowedHosts, target, redirects, file, received, finished, complete, done] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (QList<int>{301, 302, 303, 307, 308}.contains(status)) {
            const auto location = reply->header(QNetworkRequest::LocationHeader).toUrl();
            if (!location.isValid())
                return complete(fail("audio_unavailable"));
            // Retire this hop without reporting completion, then follow the redirect.
            *finished = true;
            m_replies.remove(reply);
            reply->disconnect(this);
            reply->deleteLater();
            file->close();
            fetch(url.resolved(location), allowedHosts, target, redirects + 1, done);
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
            return complete(fail("audio_unavailable"));
        if (!file->isOpen() && !file->open(QIODevice::WriteOnly | QIODevice::Truncate))
            return complete(fail("audio_unavailable"));
        const auto chunk = reply->readAll();
        *received += chunk.size();
        if (*received > maxBytes)
            return complete(fail("download_too_large"));
        file->write(chunk);
        complete({});
    });
}
void DownloadPreparer::prepare(const QVariantMap &audio, double fillerSec, Done done) {
    if (m_closed)
        return done(fail("trim_failed"));
    if (QStandardPaths::findExecutable("ffmpeg").isEmpty() || QStandardPaths::findExecutable("ffprobe").isEmpty())
        return done(fail("ffmpeg_missing"));
    collect();
    QDir().mkpath(m_directory);
    const auto work = QDir(m_directory).filePath("download-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(work);
    const auto input = QDir(work).filePath("original.audio");
    auto cleanup = [work] { QDir(work).removeRecursively(); };
    fetch(QUrl(audio.value("url").toString()), audio.value("allowedHosts").toStringList(), input, 0,
          [this, input, work, fillerSec, cleanup, done](Result<void> fetched) {
              if (!fetched) {
                  cleanup();
                  return done(fetched.error());
              }
              if (QFileInfo(input).size() == 0) {
                  cleanup();
                  return done(fail("audio_unavailable"));
              }
              trim(input, work, fillerSec, [this, work, cleanup, done](Result<QVariantMap> result) {
                  if (!result) {
                      cleanup();
                      return done(result.error());
                  }
                  const auto extension = result.value().value("extension").toString();
                  const auto prepared = QDir(m_directory).filePath("prepared-" + QUuid::createUuid().toString(QUuid::WithoutBraces) +
                                                                   "." + extension);
                  const bool moved = QFile::rename(result.value().value("file").toString(), prepared);
                  cleanup();
                  if (!moved)
                      return done(fail("trim_failed"));
                  m_log(QStringLiteral("Prepared download: removed %1s of leading silence")
                            .arg(result.value().value("trimmed").toDouble(), 0, 'f', 3));
                  done(QVariantMap{{"url", QUrl::fromLocalFile(prepared).toString()}, {"extension", extension}});
              });
          });
}
} // namespace sekai
