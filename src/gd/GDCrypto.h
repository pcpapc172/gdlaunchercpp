#pragma once
#include <QByteArray>
#include <QString>

// Port of the GDCrypto object from editor.js: GD's save-file XOR + url-safe
// base64 + gzip scheme, and the plain url-safe-base64 + gzip scheme used for
// individual level strings (k4).
namespace GDCrypto {

QByteArray xorBuf(const QByteArray &buf, quint8 key);
QByteArray urlSafeBase64Decode(const QString &str);
QString urlSafeBase64Encode(const QByteArray &buf);

QByteArray gzipCompress(const QByteArray &data);
QByteArray gzipDecompress(const QByteArray &data, bool *ok = nullptr);

// Returns a null QString on failure.
QString decryptSaveFile(const QString &filePath);
bool encryptSaveFile(const QString &xml, const QString &filePath);

QString decryptLevelString(const QString &s);
QString encryptLevelString(const QString &s);

}
