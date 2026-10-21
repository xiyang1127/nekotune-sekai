#pragma once
#include "common/http.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <functional>
#include <optional>

/// Canned HTTP transport for protocol tests. handler answers each request; when it returns an
/// empty optional the request stays pending until cancelAll().
class FakeTransport final : public nekotune::ext::HttpTransport {
  public:
    using Handler = std::function<std::optional<nekotune::Result<nekotune::ext::HttpResponse>>(
        const nekotune::ext::HttpRequest &)>;
    Handler handler;
    QList<nekotune::ext::HttpRequest> requests;
    QList<nekotune::ext::HttpDone> pending;

    void send(const nekotune::ext::HttpRequest &request, nekotune::ext::HttpDone done) override {
        requests.append(request);
        auto reply = handler ? handler(request) : std::nullopt;
        if (reply)
            done(*reply);
        else
            pending.append(std::move(done));
    }
    void cancelAll() override {
        for (const auto &done : std::exchange(pending, {}))
            done(nekotune::failure("cancelled"));
    }
};

inline nekotune::ext::HttpResponse jsonResponse(const QJsonObject &body, int status = 200) {
    nekotune::ext::HttpResponse response;
    response.status = status;
    response.headers.insert("content-type", "application/json");
    response.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return response;
}
inline nekotune::ext::HttpResponse rawResponse(const QByteArray &body, int status = 200,
                                                const QHash<QByteArray, QByteArray> &headers = {}) {
    nekotune::ext::HttpResponse response;
    response.status = status;
    response.headers = headers;
    response.body = body;
    return response;
}
