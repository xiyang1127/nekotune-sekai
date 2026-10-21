#pragma once
#include <nekotune/result.h>
#include <QByteArray>
#include <QString>

namespace nekotune::ext {
/// "png", "jpg" or "webp" when bytes are a complete image of that MIME type with sides of at
/// most 4096 pixels; empty otherwise. mime may carry parameters.
QString imageSuffix(const QByteArray &bytes, const QString &mime);
/// Write bytes to file only if it does not exist yet: "saved" or "existing".
Result<QString> publishFile(const QString &file, const QByteArray &bytes);
/// True when path (or a link at path) exists.
bool pathExists(const QString &path);
} // namespace nekotune::ext
