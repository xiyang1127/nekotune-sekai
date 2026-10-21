#include "json.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace nekotune::ext {
std::optional<QJsonValue> parseJson(const QByteArray &text) {
    QByteArray rewritten;
    rewritten.reserve(text.size() + 16);
    bool inString = false, escaped = false;
    for (qsizetype i = 0; i < text.size();) {
        const char c = text[i];
        if (inString) {
            rewritten += c;
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            ++i;
            continue;
        }
        if (c == '"') {
            inString = true;
            rewritten += c;
            ++i;
            continue;
        }
        if ((c >= '0' && c <= '9') || c == '-') {
            qsizetype end = i + 1;
            while (end < text.size() && QByteArray("0123456789.eE+-").contains(text[end]))
                ++end;
            const auto literal = text.mid(i, end - i);
            const bool digits = std::all_of(literal.begin(), literal.end(), [](char d) { return d >= '0' && d <= '9'; });
            // Beyond Number.MAX_SAFE_INTEGER (16 digits may already exceed it).
            bool unsafe = digits && (literal.size() > 16 ||
                                     (literal.size() == 16 && literal > QByteArray("9007199254740991")));
            rewritten += unsafe ? '"' + literal + '"' : literal;
            i = end;
            continue;
        }
        rewritten += c;
        ++i;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(rewritten, &error);
    if (error.error != QJsonParseError::NoError) {
        // QJsonDocument rejects top-level scalars; wrap them.
        const auto wrapped = QJsonDocument::fromJson("[" + rewritten + "]", &error);
        if (error.error != QJsonParseError::NoError)
            return std::nullopt;
        return wrapped.array().first();
    }
    return document.isObject() ? QJsonValue(document.object()) : QJsonValue(document.array());
}
QString stringId(const QJsonValue &value) {
    if (value.isString())
        return value.toString();
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (std::isfinite(number) && number == std::floor(number) && std::fabs(number) <= 9007199254740991.0)
            return QString::number(qint64(number));
    }
    return {};
}
qint64 integer(const QJsonValue &value, qint64 fallback) {
    double number;
    if (value.isDouble())
        number = value.toDouble();
    else if (value.isString()) {
        bool ok = false;
        number = value.toString().toDouble(&ok);
        if (!ok)
            return fallback;
    } else
        return fallback;
    return std::isfinite(number) ? qint64(std::llround(number)) : fallback;
}
QByteArray compactJson(const QJsonValue &value) {
    if (value.isObject())
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    if (value.isArray())
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    const auto wrapped = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return wrapped.mid(1, wrapped.size() - 2);
}
} // namespace nekotune::ext
