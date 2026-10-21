#include "http.h"
#include <QNetworkReply>
#include <QTimer>
#include <QUrlQuery>
#include <memory>

namespace nekotune::ext {
NetworkTransport::NetworkTransport(QObject *parent) : QObject(parent) {}
NetworkTransport::~NetworkTransport() { cancelAll(); }

void NetworkTransport::send(const HttpRequest &request, HttpDone done) {
    QNetworkRequest message(request.url);
    message.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         request.followRedirects ? QNetworkRequest::NoLessSafeRedirectPolicy
                                                 : QNetworkRequest::ManualRedirectPolicy);
    message.setTransferTimeout(request.timeoutMs);
    for (const auto &[name, value] : request.headers)
        message.setRawHeader(name, value);
    auto *reply = request.body.isNull() && request.method == "GET"
                      ? m_network.get(message)
                      : m_network.sendCustomRequest(message, request.method, request.body);
    m_replies.insert(reply);
    auto body = std::make_shared<QByteArray>();
    auto finished = std::make_shared<bool>(false);
    auto complete = [this, reply, finished, done](Result<HttpResponse> result) {
        if (*finished)
            return;
        *finished = true;
        m_replies.remove(reply);
        reply->disconnect(this);
        if (!reply->isFinished())
            reply->abort();
        reply->deleteLater();
        done(std::move(result));
    };
    if (request.headersOnly)
        connect(reply, &QNetworkReply::metaDataChanged, this, [reply, complete] {
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            if (!status.isValid())
                return;
            HttpResponse response;
            response.status = status.toInt();
            for (const auto &[name, value] : reply->rawHeaderPairs())
                response.headers.insert(name.toLower(), value);
            complete(response);
        });
    connect(reply, &QNetworkReply::readyRead, this, [reply, body, complete, limit = request.maxBytes] {
        *body += reply->readAll();
        if (limit > 0 && body->size() > limit)
            complete(failure("response_too_large"));
    });
    connect(reply, &QNetworkReply::finished, this, [reply, body, complete, limit = request.maxBytes] {
        *body += reply->readAll();
        if (limit > 0 && body->size() > limit) {
            complete(failure("response_too_large"));
            return;
        }
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            complete(failure(reply->property("nekotuneCancelled").toBool() ? "cancelled" : "timeout",
                             ErrorCode::Cancelled));
            return;
        }
        if (!status.isValid()) {
            complete(failure("network_error", ErrorCode::Unavailable));
            return;
        }
        HttpResponse response;
        response.status = status.toInt();
        for (const auto &[name, value] : reply->rawHeaderPairs()) {
            auto &slot = response.headers[name.toLower()];
            slot = slot.isEmpty() ? value : slot + '\n' + value;
        }
        response.body = *body;
        complete(response);
    });
}
void NetworkTransport::cancelAll() {
    for (auto *reply : QSet(m_replies)) {
        reply->setProperty("nekotuneCancelled", true);
        reply->abort();
    }
}

QByteArray formBody(const QList<QPair<QString, QString>> &fields) {
    QByteArray result;
    for (const auto &[key, value] : fields) {
        if (!result.isEmpty())
            result += '&';
        result += QUrl::toPercentEncoding(key) + '=' + QUrl::toPercentEncoding(value);
    }
    return result;
}
} // namespace nekotune::ext
