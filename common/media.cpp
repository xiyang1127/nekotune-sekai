#include "media.h"
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QtEndian>

namespace nekotune::ext {
namespace {
quint32 be32(const QByteArray &b, qsizetype at) { return qFromBigEndian<quint32>(b.constData() + at); }
quint16 be16(const QByteArray &b, qsizetype at) { return qFromBigEndian<quint16>(b.constData() + at); }
quint32 le32(const QByteArray &b, qsizetype at) { return qFromLittleEndian<quint32>(b.constData() + at); }
quint16 le16(const QByteArray &b, qsizetype at) { return qFromLittleEndian<quint16>(b.constData() + at); }
quint32 le24(const QByteArray &b, qsizetype at) {
    return quint32(uchar(b[at])) | (quint32(uchar(b[at + 1])) << 8) | (quint32(uchar(b[at + 2])) << 16);
}
bool bounded(qint64 width, qint64 height) { return width > 0 && height > 0 && width <= 4096 && height <= 4096; }
} // namespace

QString imageSuffix(const QByteArray &bytes, const QString &mimeType) {
    const auto mime = mimeType.section(';', 0, 0).trimmed().toLower();
    const auto size = bytes.size();
    if (mime == "image/png" && size >= 45 && bytes.startsWith("\x89PNG\r\n\x1a\n") && bytes.mid(12, 4) == "IHDR" &&
        bytes.mid(size - 8, 4) == "IEND" && bounded(be32(bytes, 16), be32(bytes, 20)))
        return "png";
    if (mime == "image/jpeg" && size > 4 && be16(bytes, 0) == 0xffd8 && be16(bytes, size - 2) == 0xffd9) {
        qsizetype offset = 2;
        while (offset + 4 < size && uchar(bytes[offset]) == 0xff) {
            while (offset < size && uchar(bytes[offset]) == 0xff)
                ++offset;
            if (offset >= size)
                break;
            const uchar marker = uchar(bytes[offset++]);
            if (marker == 0xda || marker == 0xd9 || offset + 2 > size)
                break;
            const auto length = be16(bytes, offset);
            if (length < 2 || offset + length > size)
                break;
            if ((marker == 0xc0 || marker == 0xc1 || marker == 0xc2) && length >= 8 &&
                bounded(be16(bytes, offset + 5), be16(bytes, offset + 3)))
                return "jpg";
            offset += length;
        }
    }
    if (mime == "image/webp" && size >= 30 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP" &&
        qint64(le32(bytes, 4)) + 8 == size) {
        const auto kind = bytes.mid(12, 4);
        if (kind == "VP8X" && bounded(1 + le24(bytes, 24), 1 + le24(bytes, 27)))
            return "webp";
        if (kind == "VP8L" && uchar(bytes[20]) == 0x2f) {
            const auto bits = le32(bytes, 21);
            if (bounded(1 + (bits & 0x3fff), 1 + ((bits >> 14) & 0x3fff)))
                return "webp";
        }
        if (kind == "VP8 " && bytes.mid(23, 3) == QByteArray("\x9d\x01\x2a", 3) &&
            bounded(le16(bytes, 26) & 0x3fff, le16(bytes, 28) & 0x3fff))
            return "webp";
    }
    return {};
}
bool pathExists(const QString &path) {
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}
Result<QString> publishFile(const QString &file, const QByteArray &bytes) {
    if (pathExists(file))
        return QStringLiteral("existing");
    const auto temporary = file + "." + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".partial";
    QFile output(temporary);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return failure(output.errorString(), ErrorCode::Io);
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const bool written = output.write(bytes) == bytes.size();
    output.close();
    if (!written) {
        QFile::remove(temporary);
        return failure("Cannot write " + file, ErrorCode::Io);
    }
    // QFile::rename refuses to replace an existing target, so a concurrent writer wins.
    if (!QFile::rename(temporary, file)) {
        QFile::remove(temporary);
        if (pathExists(file))
            return QStringLiteral("existing");
        return failure("Cannot save " + file, ErrorCode::Io);
    }
    return QStringLiteral("saved");
}
} // namespace nekotune::ext
