#pragma once
#include <QByteArray>
#include <QJsonValue>
#include <QString>
#include <optional>

namespace nekotune::ext {
/// Parse JSON while keeping integers beyond 2^53 exact: such literals become strings, matching
/// the JavaScript plugins' parseJson. Returns nullopt for invalid JSON.
std::optional<QJsonValue> parseJson(const QByteArray &text);
/// Decimal string for a JSON id that may be a string or a safe integer; empty otherwise.
QString stringId(const QJsonValue &value);
/// Integer value of a finite number, or fallback.
qint64 integer(const QJsonValue &value, qint64 fallback = 0);
QByteArray compactJson(const QJsonValue &value);
} // namespace nekotune::ext
