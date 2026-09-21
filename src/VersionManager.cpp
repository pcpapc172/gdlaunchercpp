#include "VersionManager.h"
#include "Settings.h"
#include "DebugLog.h"
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
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <functional>
#include <cstring>

extern "C" {
#include "miniz.h"
}

static const char *REMOTE_VERSIONS_URL = "http://api.pcpapc172.ir/archive/versions.json";

VersionManager::VersionManager(QObject *parent) : QObject(parent) {
    // extractionProgress is emitted from a background thread (see downloadVersion); Qt queues
    // it onto this object's own thread automatically since the receiver lives here, so it's
    // safe to update m_operations (which the UI thread also reads) only from this slot.
    connect(this, &VersionManager::extractionProgress, this, [this](const QString &id, qint64 cur, qint64 tot) {
        if (auto it = m_operations.find(id); it != m_operations.end()) { it->current = cur; it->total = tot; }
    });
}

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
    // A version whose id/path mentions "geode" is Geode-patched by definition, so that's a
    // sensible fallback for versions.json entries or on-disk version.json files predating the
    // geode_compatible field -- better than silently defaulting to false for every one of them.
    d.geodeCompatible = versionPath.contains("geode", Qt::CaseInsensitive);

    const QString versionJsonPath = Settings::versionsDir() + "/" + versionPath + "/version.json";
    QFile f(versionJsonPath);
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray raw = f.readAll();
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            // A malformed version.json (e.g. a missing/trailing comma) used to fail silently
            // here and every field would fall back to its built-in default with no
            // indication why -- surface it instead of guessing quietly.
            const QString msg = QString("%1 is not valid JSON (%2 at offset %3); ignoring it and using built-in defaults.")
                .arg(versionJsonPath, err.errorString()).arg(err.offset);
            qWarning().noquote() << "[VersionManager]" << msg;
            GD_DEBUG_LOG("versions", msg);
            d.parseError = QString("%1: %2 (offset %3)").arg(versionJsonPath, err.errorString()).arg(err.offset);
        } else {
            const QJsonObject o = doc.object();
            if (o.contains("executable")) d.executable = o["executable"].toString();
            if (o.contains("steam_emulator")) d.steamEmulator = o["steam_emulator"].toString();
            if (o.contains("geode_compatible")) d.geodeCompatible = o["geode_compatible"].toBool();
            if (o.contains("use_megahack")) d.useMegaHack = o["use_megahack"].toBool();
            if (o.contains("use_steam_emu")) d.useSteamEmu = o["use_steam_emu"].toBool();
            if (o.contains("skip_restart_check")) d.skipRestartCheck = o["skip_restart_check"].toBool();
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
                    // The remote manifest only carries id/category/version/name/url (no
                    // "path"); the on-disk layout is always <category>/<version>, so derive
                    // it rather than trusting a "path" field that doesn't exist. Using a
                    // missing field previously meant every entry resolved to the Versions
                    // root directory (which always exists) and showed as installed.
                    const QString category = o.value("category").toString();
                    const QString version = o.value("version").toString();
                    QString path = o.value("path").toString();
                    if (path.isEmpty()) path = category + "/" + version;
                    o["path"] = path;
                    o["id"] = path;

                    const bool installed = !path.isEmpty() && QFileInfo::exists(Settings::versionsDir() + "/" + path);
                    o["isInstalled"] = installed;
                    o["size"] = installed ? QString() : QString("Unknown");
                    result.push_back(o);
                }
            }
        }
        emit remoteVersionsReady(result);
    });
}

static QString formatBytes(qint64 bytes) {
    double b = static_cast<double>(bytes);
    static const char *units[] = {"B", "KB", "MB", "GB"};
    int i = 0;
    while (b >= 1024.0 && i < 3) { b /= 1024.0; i++; }
    return QString::number(b, 'f', 2) + " " + units[i];
}

void VersionManager::resolveSizes(const QJsonArray &versions) {
    for (const QJsonValue &vv : versions) {
        const QJsonObject v = vv.toObject();
        const QString id = v.value("id").toString();
        const QString path = v.value("path").toString();

        if (v.value("isInstalled").toBool()) {
            const qint64 bytes = getVersionSize(path);
            emit sizeResolved(id, formatBytes(bytes));
            continue;
        }

        const QString url = v.value("url").toString();
        if (url.isEmpty()) { emit sizeResolved(id, "Unknown"); continue; }

        QNetworkRequest req{QUrl(url)};
        QNetworkReply *reply = m_net.head(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) { emit sizeResolved(id, "Unknown"); return; }
            const QVariant lenHeader = reply->header(QNetworkRequest::ContentLengthHeader);
            if (!lenHeader.isValid()) { emit sizeResolved(id, "Unknown"); return; }
            emit sizeResolved(id, formatBytes(lenHeader.toLongLong()));
        });
    }
}

namespace {

struct ExtractResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
};

struct WriteCallbackCtx {
    QFile *outFile = nullptr;
    std::atomic<bool> *cancelFlag = nullptr;
    qint64 cumulative = 0;
    qint64 total = 0;
    QString id;
    VersionManager *manager = nullptr;
};

// miniz calls this per chunk as it inflates a file, so we get real byte-level
// progress (and a cancellation point) instead of jumping 0% -> 100% per file,
// which matters when an archive is mostly one or two huge files.
size_t extractWriteCallback(void *pOpaque, mz_uint64 /*fileOfs*/, const void *pBuf, size_t n) {
    auto *ctx = static_cast<WriteCallbackCtx *>(pOpaque);
    if (ctx->cancelFlag->load()) return 0; // returning short tells miniz to abort
    const qint64 written = ctx->outFile->write(static_cast<const char *>(pBuf), static_cast<qint64>(n));
    if (written != static_cast<qint64>(n)) return 0;
    ctx->cumulative += static_cast<qint64>(n);
    if (ctx->manager) emit ctx->manager->extractionProgress(ctx->id, ctx->cumulative, ctx->total);
    return n;
}

// Runs entirely on a background thread (see QtConcurrent::run below in downloadVersion) so the
// UI stays responsive for large archives; `manager`/`id` are only used to emit progress signals,
// which Qt safely queues back onto the manager's own (main) thread.
ExtractResult extractZip(const QString &zipPath, const QString &destDir, std::shared_ptr<std::atomic<bool>> cancelFlag,
                          VersionManager *manager, const QString &id) {
    ExtractResult result;

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    const QByteArray zipPathBytes = zipPath.toLocal8Bit();
    if (!mz_zip_reader_init_file(&zip, zipPathBytes.constData(), 0)) {
        result.error = "Failed to open zip archive";
        return result;
    }

    const mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    qint64 totalUncompressed = 0;
    for (mz_uint i = 0; i < numFiles; ++i) {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&zip, i, &stat) && !mz_zip_reader_is_file_a_directory(&zip, i))
            totalUncompressed += static_cast<qint64>(stat.m_uncomp_size);
    }

    QDir().mkpath(destDir);
    qint64 cumulative = 0;
    bool ok = true;

    for (mz_uint i = 0; i < numFiles; ++i) {
        if (cancelFlag->load()) { result.cancelled = true; ok = false; break; }

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
        QFile outFile(outPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) { ok = false; break; }

        WriteCallbackCtx ctx;
        ctx.outFile = &outFile;
        ctx.cancelFlag = cancelFlag.get();
        ctx.cumulative = cumulative;
        ctx.total = totalUncompressed;
        ctx.id = id;
        ctx.manager = manager;

        const mz_bool extracted = mz_zip_reader_extract_to_callback(&zip, i, extractWriteCallback, &ctx, 0);
        outFile.close();
        cumulative = ctx.cumulative;

        if (!extracted) {
            ok = false;
            if (cancelFlag->load()) result.cancelled = true;
            break;
        }
    }

    mz_zip_reader_end(&zip);
    result.ok = ok;
    if (!ok && !result.cancelled) result.error = "Failed to extract archive";
    if (result.cancelled) QDir(destDir).removeRecursively();
    return result;
}

} // namespace

void VersionManager::cancelOperation(const QString &id) {
    if (QNetworkReply *reply = m_activeReplies.value(id)) {
        reply->abort();
        return;
    }
    if (auto flag = m_cancelFlags.value(id)) flag->store(true);
}

bool VersionManager::hasActiveOperation(const QString &id) const {
    return m_operations.contains(id);
}

VersionOperation VersionManager::activeOperation(const QString &id) const {
    return m_operations.value(id);
}

void VersionManager::downloadVersion(const QJsonObject &version) {
    const QString id = version.value("id").toString();
    const QString versionPath = version.value("path").toString();
    const QString url = version.value("url").toString();

    if (m_operations.contains(id)) return; // already downloading/extracting

    m_operations[id] = VersionOperation{VersionOperation::Phase::Downloading, 0, 0};
    emit downloadStarted(id);

    const QString tmpZip = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                            "/" + QString(id).replace('/', '_') + ".zip";
    QFile *outFile = new QFile(tmpZip, this);
    if (!outFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_operations.remove(id);
        emit downloadFinished(id, false, "Failed to open temp file for download");
        delete outFile;
        return;
    }

    QNetworkRequest req{QUrl(url)};
    QNetworkReply *reply = m_net.get(req);
    m_activeReplies[id] = reply;

    connect(reply, &QNetworkReply::readyRead, this, [reply, outFile]() {
        outFile->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, id](qint64 recv, qint64 total) {
        if (auto it = m_operations.find(id); it != m_operations.end()) { it->current = recv; it->total = total; }
        emit downloadProgress(id, recv, total);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, outFile, id, versionPath, tmpZip]() {
        outFile->close();
        m_activeReplies.remove(id);
        const bool wasAborted = reply->error() == QNetworkReply::OperationCanceledError;
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            outFile->deleteLater();
            QFile::remove(tmpZip);
            m_operations.remove(id);
            emit downloadFinished(id, false, wasAborted ? "Cancelled" : reply->errorString());
            return;
        }
        outFile->deleteLater();

        const QString extractPath = Settings::versionsDir() + "/" + versionPath;
        QDir().mkpath(Settings::versionsDir() + "/" + versionPath.split('/').first());
        if (QFileInfo::exists(extractPath)) QDir(extractPath).removeRecursively();

        auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
        m_cancelFlags[id] = cancelFlag;
        m_operations[id] = VersionOperation{VersionOperation::Phase::Extracting, 0, 0};
        emit extractionStarted(id);

        auto *watcher = new QFutureWatcher<ExtractResult>(this);
        connect(watcher, &QFutureWatcher<ExtractResult>::finished, this, [this, watcher, id, extractPath, tmpZip]() {
            const ExtractResult result = watcher->result();
            watcher->deleteLater();
            m_cancelFlags.remove(id);
            m_operations.remove(id);
            QFile::remove(tmpZip);

            if (!result.ok) {
                emit downloadFinished(id, false, result.cancelled ? "Cancelled" : result.error);
                return;
            }

            const QString versionJsonPath = extractPath + "/version.json";
            if (!QFileInfo::exists(versionJsonPath)) {
                QFile vf(versionJsonPath);
                if (vf.open(QIODevice::WriteOnly)) {
                    QJsonObject defaults;
                    defaults["executable"] = "GeometryDash.exe";
                    defaults["steam_emulator"] = "SmartSteamEmu.exe";
                    // Best-effort guess from the version id (e.g. "2.2/2.207-geode") so a
                    // freshly downloaded version pre-checks sensible instance options; the
                    // user can still change any of them per-instance.
                    defaults["geode_compatible"] = id.contains("geode", Qt::CaseInsensitive);
                    defaults["use_megahack"] = true;
                    defaults["use_steam_emu"] = false;
                    defaults["skip_restart_check"] = false;
                    vf.write(QJsonDocument(defaults).toJson(QJsonDocument::Indented));
                }
            }

            emit downloadFinished(id, true, QString());
        });
        watcher->setFuture(QtConcurrent::run(extractZip, tmpZip, extractPath, cancelFlag, this, id));
    });
}
