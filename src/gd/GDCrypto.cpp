#include "GDCrypto.h"
#include <QFile>
#include <zlib.h>

QByteArray GDCrypto::xorBuf(const QByteArray &buf, quint8 key) {
    QByteArray out(buf.size(), Qt::Uninitialized);
    for (int i = 0; i < buf.size(); ++i)
        out[i] = static_cast<char>(static_cast<quint8>(buf[i]) ^ key);
    return out;
}

QByteArray GDCrypto::urlSafeBase64Decode(const QString &str) {
    QString s = str;
    s.replace('-', '+').replace('_', '/');
    while (s.length() % 4 != 0) s += '=';
    return QByteArray::fromBase64(s.toLatin1());
}

QString GDCrypto::urlSafeBase64Encode(const QByteArray &buf) {
    QString s = QString::fromLatin1(buf.toBase64());
    s.replace('+', '-').replace('/', '_');
    return s;
}

QByteArray GDCrypto::gzipCompress(const QByteArray &data) {
    z_stream strm{};
    // 15 + 16 tells zlib to wrap the deflate stream in a gzip header/trailer.
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return {};

    QByteArray out;
    out.resize(static_cast<int>(deflateBound(&strm, data.size())));

    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    strm.avail_in = static_cast<uInt>(data.size());
    strm.next_out = reinterpret_cast<Bytef *>(out.data());
    strm.avail_out = static_cast<uInt>(out.size());

    const int ret = deflate(&strm, Z_FINISH);
    const uLong producedSize = strm.total_out;
    deflateEnd(&strm);
    if (ret != Z_STREAM_END) return {};
    out.resize(static_cast<int>(producedSize));
    return out;
}

QByteArray GDCrypto::gzipDecompress(const QByteArray &data, bool *ok) {
    z_stream strm{};
    if (ok) *ok = false;
    if (inflateInit2(&strm, 15 + 16) != Z_OK) return {};

    QByteArray out;
    QByteArray chunk;
    chunk.resize(64 * 1024);

    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    strm.avail_in = static_cast<uInt>(data.size());

    int ret;
    do {
        strm.next_out = reinterpret_cast<Bytef *>(chunk.data());
        strm.avail_out = static_cast<uInt>(chunk.size());
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) { inflateEnd(&strm); return {}; }
        out.append(chunk.constData(), static_cast<int>(chunk.size() - strm.avail_out));
    } while (ret != Z_STREAM_END && strm.avail_in > 0);

    inflateEnd(&strm);
    if (ret != Z_STREAM_END) return {};
    if (ok) *ok = true;
    return out;
}

QString GDCrypto::decryptSaveFile(const QString &filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    const QByteArray d = f.readAll();

    if (QString::fromUtf8(d).trimmed().startsWith('<')) return QString::fromUtf8(d);

    bool ok = false;
    {
        const QByteArray xored = xorBuf(d, 11);
        const QString asText = QString::fromUtf8(xored);
        const QByteArray decoded = urlSafeBase64Decode(asText);
        const QByteArray inflated = gzipDecompress(decoded, &ok);
        if (ok) return QString::fromUtf8(inflated);
    }
    {
        const QString asText = QString::fromUtf8(d);
        const QByteArray decoded = urlSafeBase64Decode(asText);
        const QByteArray inflated = gzipDecompress(decoded, &ok);
        if (ok) return QString::fromUtf8(inflated);
    }
    return QString();
}

bool GDCrypto::encryptSaveFile(const QString &xml, const QString &filePath) {
    const QByteArray zipped = gzipCompress(xml.toUtf8());
    if (zipped.isEmpty() && !xml.isEmpty()) return false;
    const QString b64 = urlSafeBase64Encode(zipped);
    const QByteArray xored = xorBuf(b64.toUtf8(), 11);

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(xored);
    return true;
}

QString GDCrypto::decryptLevelString(const QString &s) {
    if (s.isEmpty()) return QString();
    bool ok = false;
    const QByteArray decoded = urlSafeBase64Decode(s);
    const QByteArray inflated = gzipDecompress(decoded, &ok);
    if (!ok) return s;
    return QString::fromUtf8(inflated);
}

QString GDCrypto::encryptLevelString(const QString &s) {
    if (s.isEmpty()) return QString();
    const QByteArray zipped = gzipCompress(s.toUtf8());
    if (zipped.isEmpty()) return QString();
    return urlSafeBase64Encode(zipped);
}
