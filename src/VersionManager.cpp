#include "VersionManager.h"
#include "Settings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QNetworkRequest>
#include <QDebug>
#include <QDateTime>
#include <functional>
#include <cstring>

extern "C" {
#include "miniz.h"
}

static const char *REMOTE_VERSIONS_URL = "http://api.pcpapc172.ir/archive/versions.json";

VersionManager::VersionManager(QObject *parent) : QObject(parent) {}

QVector<LocalVersion> VersionManager::getVersions() {
    QVector<LocalVersion> out;
    QDir root(Settings::versionsDir());
    if (!root.exists()) return out;
    const auto categories = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &category : categories) {
        QDir catDir(root.filePath(category));
        const auto versionFolders = catDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &versionFolder : versionFolders) {
            LocalVersion v;
            v.category = category;
            v.version = versionFolder;
            v.id = category + "/" + versionFolder;
            v.path = v.id;
            out.push_back(v);
        }
    }
    return out;
}

qint64 VersionManager::getVersionSize(const QString &versionPath) {
    QDir dir(Settings::versionsDir() + "/" + versionPath);
    qint64 total = 0;
    std::function<qint64(const QString &)> recurse = [&](const QString &p) -> qint64 {
        QFileInfo fi(p);
        if (!fi.exists()) return 0;
        if (fi.isFile()) return fi.size();
        qint64 t = 0;
        QDir d(p);
        for (const QFileInfo &e : d.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries)) t += recurse(e.filePath());
        return t;
    };
    total = recurse(dir.path());
    return total;
}

VersionDefaults VersionManager::getVersionDefaults(const QString &versionPath) {
    VersionDefaults d;
    QFile f(Settings::versionsDir() + "/" + versionPath + "/version.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject o = doc.object();
            if (o.contains("executable")) d.executable = o["executable"].toString();
            if (o.contains("steam_emulator")) d.steamEmulator = o["steam_emulator"].toString();
        }
    }
    return d;
}

bool VersionManager::deleteVersion(const QString &versionPath, QString *error) {
    const QString fullPath = Settings::versionsDir() + "/" + versionPath;
    QString prefix = "Version_" + QString(versionPath).replace('/', '_');
    QDir().mkpath(Settings::trashDir());
    const QString dateStr = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH-mm-ss-zzz");
    const QString trashPath = Settings::trashDir() + "/" + prefix + "_" + dateStr;
    if (!QFileInfo::exists(fullPath)) return true;
    if (!QDir().rename(fullPath, trashPath)) {
        if (error) *error = "Failed to move version to trash";
        return false;
    }
    return true;
}

void VersionManager::fetchRemoteVersions() {
    QNetworkRequest req{QUrl(REMOTE_VERSIONS_URL)};
    QNetworkReply *reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QJsonArray result;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
            if (err.error == QJsonParseError::NoError && doc.isArray()) {
                for (const QJsonValue &v : doc.array()) {
                    QJsonObject o = v.toObject();
                    const QString path = o.value("path").toString();
                    const bool installed = QFileInfo::exists(Settings::versionsDir() + "/" + path);
                    o["isInstalled"] = installed;
                    o["size"] = "Calculating...";
                    result.push_back(o);
                }
            }
        }
        emit remoteVersionsReady(result);
    });
}

static bool extractZip(const QString &zipPath, const QString &destDir, QString *error) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    const QByteArray zipPathBytes = zipPath.toLocal8Bit();
    if (!mz_zip_reader_init_file(&zip, zipPathBytes.constData(), 0)) {
        if (error) *error = "Failed to open zip archive";
        return false;
    }
    const mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    QDir().mkpath(destDir);
    bool ok = true;
    for (mz_uint i = 0; i < numFiles; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) { ok = false; break; }
        QString name = QString::fromUtf8(stat.m_filename);
        name.replace('\\', '/');
        const QString outPath = destDir + "/" + name;
        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            QDir().mkpath(outPath);
            continue;
        }
        QDir().mkpath(QFileInfo(outPath).path());
        const QByteArray outPathBytes = outPath.toLocal8Bit();
        if (!mz_zip_reader_extract_to_file(&zip, i, outPathBytes.constData(), 0)) {
            ok = false;
            break;
        }
    }
    mz_zip_reader_end(&zip);
    if (!ok && error) *error = "Failed to extract archive";
    return ok;
}

void VersionManager::downloadVersion(const QJsonObject &version) {
    const QString id = version.value("id").toString();
    const QString versionPath = version.value("path").toString();
    const QString url = version.value("url").toString();

    emit downloadStarted(id);

    const QString tmpZip = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                            "/" + QString(id).replace('/', '_') + ".zip";
    QFile *outFile = new QFile(tmpZip, this);
    if (!outFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit downloadFinished(id, false, "Failed to open temp file for download");
        delete outFile;
        return;
    }

    QNetworkRequest req{QUrl(url)};
    QNetworkReply *reply = m_net.get(req);

    connect(reply, &QNetworkReply::readyRead, this, [reply, outFile]() {
        outFile->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, id](qint64 recv, qint64 total) {
        emit downloadProgress(id, recv, total);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, outFile, id, versionPath, tmpZip]() {
        outFile->close();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit downloadFinished(id, false, reply->errorString());
            outFile->deleteLater();
            return;
        }

        const QString extractPath = Settings::versionsDir() + "/" + versionPath;
        QDir().mkpath(Settings::versionsDir() + "/" + versionPath.split('/').first());
        if (QFileInfo::exists(extractPath)) QDir(extractPath).removeRecursively();

        QString err;
        const bool ok = extractZip(tmpZip, extractPath, &err);
        QFile::remove(tmpZip);
        outFile->deleteLater();

        if (!ok) {
            emit downloadFinished(id, false, err);
            return;
        }

        const QString versionJsonPath = extractPath + "/version.json";
        if (!QFileInfo::exists(versionJsonPath)) {
            QFile vf(versionJsonPath);
            if (vf.open(QIODevice::WriteOnly)) {
                QJsonObject defaults;
                defaults["executable"] = "GeometryDash.exe";
                defaults["steam_emulator"] = "SmartSteamEmu.exe";
                vf.write(QJsonDocument(defaults).toJson(QJsonDocument::Indented));
            }
        }

        emit downloadFinished(id, true, QString());
    });
}
