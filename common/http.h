#pragma once
#include <nekotune/result.h>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <functional>

class QNetworkReply;

namespace nekotune::ext {
struct HttpRequest {
    QByteArray method = "GET";
    QUrl url;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;
    int timeoutMs = 20'000;
    qint64 maxBytes = 4 * 1024 * 1024;
    /// Redirects are returned as responses unless explicitly followed.
    bool followRedirects = false;
    /// Complete as soon as the status line and headers arrive, discarding any body (probes).
    bool headersOnly = false;
};
struct HttpResponse {
    int status = 0;
    /// Lower-case header names; repeated headers are joined with '\n' (Set-Cookie).
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
    QByteArray header(const QByteArray &name) const { return headers.value(name.toLower()); }
};
/// Transport failures use these messages: "cancelled", "timeout", "network_error", "response_too_large".
using HttpDone = std::function<void(Result<HttpResponse>)>;

/// Injectable HTTP seam so protocol code can be tested with canned responses.
class HttpTransport {
  public:
    virtual ~HttpTransport() = default;
    virtual void send(const HttpRequest &request, HttpDone done) = 0;
    /// Fail every in-flight request with "cancelled".
    virtual void cancelAll() = 0;
};

/// QNetworkAccessManager transport. Lives on the thread that created it; callbacks run there.
class NetworkTransport final : public QObject, public HttpTransport {
  public:
    explicit NetworkTransport(QObject *parent = nullptr);
    ~NetworkTransport() override;
    void send(const HttpRequest &request, HttpDone done) override;
    void cancelAll() override;

  private:
    QNetworkAccessManager m_network;
    QSet<QNetworkReply *> m_replies;
};

/// application/x-www-form-urlencoded body from ordered key/value pairs.
QByteArray formBody(const QList<QPair<QString, QString>> &fields);
} // namespace nekotune::ext
