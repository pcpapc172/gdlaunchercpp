#include "InstanceUpdater.h"
#include "InstanceManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <cmath>
#include <filesystem>
#include <memory>

namespace {
const QString workName = ".gdlauncher-update";
const QString stateName = ".gdlauncher-release.json";
const QString journalName = "/journal.json";
const QString commitName = "/committed";
const qint64 manifestLimit = 2 * 1024 * 1024;

struct Failure { QString message; };
void require(bool ok, const QString &error) {
    if (!ok) throw Failure{error};
}

struct Entry {
    QString path;
    QUrl url;
    QByteArray hash;
    qint64 size = 0;
};

bool isLink(const QFileInfo &info) {
    return info.isSymLink() || info.isJunction();
}

bool renameFile(const QString &source, const QString &target) {
    const auto nativePath = [](const QString &path) {
#ifdef Q_OS_WIN
        return std::filesystem::path(path.toStdWString());
#else
        return std::filesystem::path(QFile::encodeName(path).constData());
#endif
    };
    std::error_code error;
    std::filesystem::rename(nativePath(source), nativePath(target), error);
    return !error;
}

bool validPath(const QString &path) {
    static const QRegularExpression invalid(R"([\\:<>"|?*\x00-\x1f])");
    static const QRegularExpression device(R"(^(CON|PRN|AUX|NUL|COM[0-9¹²³]|LPT[0-9¹²³])$)",
                                           QRegularExpression::CaseInsensitiveOption);
    if (path.size() > 512 || path.contains(invalid)) return false;
    const auto parts = path.split('/');
    for (const auto &part : parts) {
        if (part.isEmpty() || part == "." || part == ".." || part.size() > 255 ||
            part.endsWith('.') || part.endsWith(' ') || part.startsWith(' ') ||
            device.match(part.section('.', 0, 0).trimmed()).hasMatch()) return false;
    }
    const QString leaf = parts.last().toCaseFolded();
    if (leaf.startsWith("ccgamemanager") || leaf.startsWith("cclocallevels") ||
        leaf.startsWith("ccbetterinfo") || leaf == "instance.json") return false;
    static const auto reserved = InstanceManager::getAllManagedItems();
    const QString first = parts.first().toCaseFolded();
    for (const auto &name : reserved) {
        // Geode mod packages belong to the installation. Its logs and save data do not.
        if (name == "geode" && parts.size() > 2 && parts[1].compare("mods", Qt::CaseInsensitive) == 0)
            continue;
        if (first == name.toCaseFolded()) return false;
    }
    return !first.startsWith(".gdlauncher-");
}

void checkDestination(const QString &root, const QString &path) {
    QString parent = root;
    const auto parts = path.split('/');
    for (qsizetype i = 0; i < parts.size(); ++i) {
        parent += '/' + parts[i];
        const QFileInfo info(parent);
        require(!isLink(info), "Update path is a link: " + path);
        require(!info.exists() || (i + 1 == parts.size() ? info.isFile() : info.isDir()),
                "Update path has the wrong type: " + path);
    }
}

QByteArray readSmall(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > manifestLimit) return {};
    return file.read(manifestLimit);
}

void writeAtomically(const QString &path, const QByteArray &data) {
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit(),
            "Could not write " + path);
}

void cleanup(const QString &work) {
    // Remove the journal first. The commit marker must survive as long as the journal does.
    if (QFileInfo::exists(work + journalName))
        require(QFile::remove(work + journalName), "Could not clear the update journal");
    require(QDir(work).removeRecursively(), "Could not clean up " + work);
}

void recover(const QString &root, const QString &work) {
    checkDestination(root, workName + "/journal.json");
    checkDestination(root, workName + "/committed");
    if (!QFileInfo::exists(work)) return;
    if (QFileInfo::exists(work + journalName) && !QFileInfo::exists(work + commitName)) {
        const auto doc = QJsonDocument::fromJson(readSmall(work + journalName));
        require(doc.isArray() && !doc.array().isEmpty(), "Invalid update recovery journal: " + work);
        const auto rows = doc.array();
        for (qsizetype i = rows.size(); i > 0; --i) {
            const auto row = rows[i - 1].toObject();
            const QString path = row.value("path").toString();
            const bool state = path == stateName;
            require(state || validPath(path), "Unsafe path in update recovery journal");
            checkDestination(root, path);
            const QString target = root + '/' + path;
            const QString previous = work + "/previous/" + QString::number(i - 1);
            const QString staged = work + "/new/" + QString::number(i - 1);
            checkDestination(root, workName + "/previous/" + QString::number(i - 1));
            checkDestination(root, workName + "/new/" + QString::number(i - 1));
            if (QFileInfo::exists(previous)) {
                require(!QFileInfo::exists(target) || QFile::remove(target), "Could not restore " + path);
                require(renameFile(previous, target), "Could not restore " + path);
            } else if (!row.value("existed").toBool() && !QFileInfo::exists(staged)) {
                require(!QFileInfo::exists(target) || QFile::remove(target), "Could not remove " + path);
            }
        }
    }
    cleanup(work);
}

QUrl fetch(QNetworkAccessManager &network, const QUrl &url, qint64 limit,
           const std::atomic_bool &cancelled, const std::function<void(const QByteArray &)> &consume,
           bool allowMissing = false) {
    require(!cancelled, "Cancelled");
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    std::unique_ptr<QNetworkReply> reply(network.get(request));
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, reply.get(), [&]() {
        if (cancelled) reply->abort();
    });
    poll.start(100);
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, reply.get(), &QNetworkReply::abort);
    deadline.start(15 * 60 * 1000);
    QString error;
    qint64 bytes = 0;
    const auto drain = [&]() {
        if (!error.isEmpty() || reply->error() != QNetworkReply::NoError) return;
        try {
            while (reply->bytesAvailable()) {
                const auto block = reply->read(65536);
                if (block.isEmpty()) break;
                bytes += block.size();
                require(bytes <= limit, "Download is larger than expected: " + url.path());
                consume(block);
            }
        } catch (const Failure &failure) {
            error = failure.message;
            reply->abort();
        }
    };
    reply->setReadBufferSize(256 * 1024);
    QObject::connect(reply.get(), &QIODevice::readyRead, &loop, drain);
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (!reply->isFinished()) loop.exec();
    drain();
    require(!cancelled, "Cancelled");
    require(error.isEmpty(), error);
    if (allowMissing && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404) return {};
    require(reply->error() == QNetworkReply::NoError,
            QString("%1: %2").arg(url.path(), reply->errorString()));
    require(InstanceUpdater::validUrl(reply->url()), "Unsafe download URL");
    return reply->url();
}

bool matches(const QString &path, const Entry &entry, const std::atomic_bool &cancelled) {
    QFile file(path);
    if (!QFileInfo::exists(path) || file.size() != entry.size) return false;
    require(file.open(QIODevice::ReadOnly), "Could not read " + entry.path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        require(!cancelled, "Cancelled");
        const auto bytes = file.read(65536);
        require(!bytes.isEmpty(), "Could not read " + entry.path);
        hash.addData(bytes);
    }
    return hash.result() == entry.hash;
}
}

bool InstanceUpdater::validUrl(const QUrl &url) {
    const bool local = url.host() == "localhost" || url.host() == "127.0.0.1" || url.host() == "::1";
    return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() &&
        (url.scheme() == "https" || (local && url.scheme() == "http"));
}

InstanceUpdater::Index InstanceUpdater::readIndex(const QUrl &url, const std::atomic_bool &cancelled) {
    Index index;
    try {
        require(validUrl(url), "Invalid update index URL");
        QByteArray json;
        QNetworkAccessManager network;
        const QUrl base = fetch(network, url, manifestLimit, cancelled,
                                [&](const QByteArray &block) { json += block; }, true);
        if (base.isEmpty()) {
            index.missing = true;
            return index;
        }
        const QJsonObject doc = QJsonDocument::fromJson(json).object();
        require(doc.value("formatVersion").toInt() == 1 && doc.value("updates").isObject(),
                "Invalid instance update index");
        const auto updates = doc.value("updates").toObject();
        for (auto it = updates.begin(); it != updates.end(); ++it) {
            const QStringList parts = it.key().split('/');
            require(parts.size() == 2 && validPath(it.key()), "Invalid version ID in update index: " + it.key());
            const QString link = it.value().toString();
            const QUrl manifest = base.resolved(QUrl(link, QUrl::StrictMode));
            require(!link.isEmpty() && validUrl(manifest) &&
                    (url.scheme() != "https" || manifest.scheme() == "https"),
                    "Invalid update manifest URL for " + it.key());
            index.manifests.insert(it.key(), manifest);
        }
    } catch (const Failure &failure) {
        index.manifests.clear();
        index.error = failure.message;
    }
    return index;
}

static QJsonObject readInstalledRelease(const QString &directory) {
    if (directory.isEmpty()) return {};
    const QString path = directory + '/' + stateName;
    if (isLink(QFileInfo(path))) return {};
    return QJsonDocument::fromJson(readSmall(path)).object();
}

QString InstanceUpdater::installedVersion(const QString &directory) {
    return readInstalledRelease(directory).value("version").toString();
}

int InstanceUpdater::installedUpdateNumber(const QString &directory) {
    return readInstalledRelease(directory).value("updateNumber").toInt();
}

bool InstanceUpdater::matchesInstalledRelease(const QString &directory, const Result &release) {
    const auto installed = readInstalledRelease(directory);
    return !release.version.isEmpty() && release.version == installed.value("version").toString() &&
        (release.updateNumber == 0 || release.updateNumber == installed.value("updateNumber").toInt());
}

bool InstanceUpdater::needsRecovery(const QString &directory) {
    return !directory.isEmpty() && QFileInfo::exists(directory + '/' + workName + journalName);
}

InstanceUpdater::Result InstanceUpdater::run(const QString &directory, const QUrl &manifest,
                                             bool install, const std::atomic_bool &cancelled,
                                             const Progress &progress, bool onlyIfNew, const QString &installationId) {
    Result result;
    try {
        const QFileInfo directoryInfo(directory);
        const QString root = directoryInfo.canonicalFilePath();
        require(!directory.isEmpty() && !root.isEmpty() && directoryInfo.isDir() && !isLink(directoryInfo),
                "Game installation folder does not exist or is a link");
        const QString work = root + '/' + workName;
        std::unique_ptr<QLockFile> lock;
        if (install) {
            const QString lockPath = root + "/.gdlauncher-update.lock";
            checkDestination(root, ".gdlauncher-update.lock");
            lock = std::make_unique<QLockFile>(lockPath);
            lock->setStaleLockTime(0);
            require(lock->tryLock(0), "This installation is already being updated");
            recover(root, work);
            if (manifest.isEmpty()) return result;
        }
        require(validUrl(manifest), "Enter an HTTPS update manifest URL");
        if (progress) progress("Checking for an instance update…", -1);
        QNetworkAccessManager network;
        QByteArray json;
        const QUrl base = fetch(network, manifest, manifestLimit, cancelled,
                                [&](const QByteArray &block) { json += block; });
        const auto doc = QJsonDocument::fromJson(json).object();
        const QString version = doc.value("version").toString().trimmed();
        const auto files = doc.value("files").toArray();
        require(doc.value("formatVersion").toInt() == 1 && !version.isEmpty() &&
                version.size() <= 100 && version.simplified() == version &&
                !files.isEmpty() && files.size() <= 5000, "Invalid instance update manifest");
        if (doc.contains("installationId")) {
            const QString target = doc.value("installationId").toString();
            require(target.split('/').size() == 2 && validPath(target), "Invalid installation ID in update manifest");
            require(installationId.isEmpty() || target == installationId,
                    QString("This update is for %1, but this instance uses %2.").arg(target, installationId));
        }
        if (doc.contains("updateNumber")) {
            result.updateNumber = doc.value("updateNumber").toInt(0);
            require(result.updateNumber > 0, "Update number must be a positive integer");
        }
        QList<Entry> entries;
        QSet<QString> paths;
        QHash<QString, QString> directories;
        for (const auto &value : files) {
            const auto row = value.toObject();
            const QString path = row.value("path").toString();
            const QString key = path.normalized(QString::NormalizationForm_C).toCaseFolded();
            require(validPath(path) && !paths.contains(key), "Unsafe or duplicate update path: " + path);
            paths.insert(key);
            const QString sha = row.value("sha256").toString();
            static const QRegularExpression hex("^[0-9a-fA-F]{64}$");
            const double size = row.value("size").toDouble(-1);
            const QString source = row.value("url").toString();
            const QUrl url = base.resolved(QUrl(source, QUrl::StrictMode));
            require(hex.match(sha).hasMatch() && size >= 0 && size <= 8.0 * 1024 * 1024 * 1024 &&
                    std::floor(size) == size && !source.isEmpty() && validUrl(url) &&
                    (manifest.scheme() != "https" || url.scheme() == "https"),
                    "Invalid download metadata for " + path);
            entries.append({path, url, QByteArray::fromHex(sha.toLatin1()), static_cast<qint64>(size)});
            QString prefix = path;
            while (prefix.contains('/')) {
                prefix = prefix.section('/', 0, -2);
                const QString folded = prefix.normalized(QString::NormalizationForm_C).toCaseFolded();
                require(!directories.contains(folded) || directories[folded] == prefix,
                        "Conflicting directory names: " + path);
                directories.insert(folded, prefix);
            }
        }
        for (auto it = directories.cbegin(); it != directories.cend(); ++it)
            require(!paths.contains(it.key()), "File and directory conflict: " + it.value());
        result.version = version;
        const auto installed = readInstalledRelease(root);
        const QString recorded = installed.value("version").toString();
        const int installedNumber = installed.value("updateNumber").toInt();
        // A numbered installation must never fall back to a stale or unnumbered manifest.
        if (installedNumber > 0 && result.updateNumber <= installedNumber &&
            (result.updateNumber < installedNumber || version != recorded)) {
            result.version = recorded;
            result.updateNumber = installedNumber;
            return result;
        }
        result.available = version != recorded || result.updateNumber != installedNumber || needsRecovery(root);
        if (!install) {
            if (recorded.isEmpty() && !needsRecovery(root)) {
                result.available = false;
                for (const auto &entry : entries) {
                    checkDestination(root, entry.path);
                    if (!matches(root + '/' + entry.path, entry, cancelled)) {
                        result.available = true;
                        break;
                    }
                }
            }
            return result;
        }
        if (onlyIfNew && !result.available) return result;

        checkDestination(root, stateName);
        QList<Entry> changes;
        qint64 total = 0;
        if (progress) progress("Checking installed files…", -1);
        for (const auto &entry : entries) {
            require(!cancelled, "Cancelled");
            checkDestination(root, entry.path);
            if (!matches(root + '/' + entry.path, entry, cancelled)) {
                changes.append(entry);
                total += entry.size;
            }
        }
        require(!cancelled, "Cancelled");
        if (changes.isEmpty() && !result.available) return result;
        require(QDir().mkpath(work + "/new") && QDir().mkpath(work + "/previous"),
                "Could not create the update staging folder");
        try {
            QJsonArray journal;
            qint64 received = 0;
            int lastPercent = -1;
            for (const auto &entry : changes) {
                const QString staged = work + "/new/" + QString::number(journal.size());
                QFile file(staged);
                require(file.open(QIODevice::WriteOnly), "Could not stage " + entry.path);
                QCryptographicHash hash(QCryptographicHash::Sha256);
                if (progress) progress("Downloading " + entry.path, total ? int(received * 100 / total) : 0);
                fetch(network, entry.url, entry.size, cancelled, [&](const QByteArray &block) {
                    require(file.write(block) == block.size(), "Could not write " + entry.path);
                    hash.addData(block);
                    received += block.size();
                    const int percent = total ? int(received * 100 / total) : 0;
                    if (progress && percent != lastPercent) {
                        lastPercent = percent;
                        progress("Downloading " + entry.path, percent);
                    }
                });
                require(file.size() == entry.size && hash.result() == entry.hash && file.flush(),
                        "Size, SHA-256, or write check failed for " + entry.path);
                QFileInfo original(root + '/' + entry.path);
                if (original.exists())
                    require(file.setPermissions(original.permissions()), "Could not preserve permissions for " + entry.path);
                journal.append(QJsonObject{{"path", entry.path}, {"existed", original.exists()}});
            }
            require(!cancelled, "Cancelled");
            QJsonObject installed{{"version", version}};
            if (result.updateNumber > 0) installed["updateNumber"] = result.updateNumber;
            const auto state = QJsonDocument(installed).toJson(QJsonDocument::Compact);
            writeAtomically(work + "/new/" + QString::number(journal.size()), state);
            journal.append(QJsonObject{{"path", stateName}, {"existed", QFileInfo::exists(root + '/' + stateName)}});
            const auto journalData = QJsonDocument(journal).toJson(QJsonDocument::Compact);
            require(journalData.size() <= manifestLimit, "Update journal is too large");
            writeAtomically(work + journalName, journalData);
            if (progress) progress("Applying instance update…", -1);
            for (qsizetype i = 0; i < journal.size(); ++i) {
                const auto row = journal[i].toObject();
                const QString path = row.value("path").toString();
                checkDestination(root, path);
                const QString target = root + '/' + path;
                const QString index = QString::number(i);
                require(QDir().mkpath(QFileInfo(target).absolutePath()), "Could not create a folder for " + path);
                if (row.value("existed").toBool())
                    require(renameFile(target, work + "/previous/" + index), "Could not move " + path);
                require(renameFile(work + "/new/" + index, target), "Could not install " + path);
            }
            writeAtomically(work + commitName, "ok");
        } catch (const Failure &failure) {
            try {
                recover(root, work);
            } catch (const Failure &recoveryFailure) {
                throw Failure{failure.message + "\nRecovery failed: " + recoveryFailure.message};
            }
            throw;
        }
        result.downloaded = changes.size();
        cleanup(work);
    } catch (const Failure &failure) {
        result.error = failure.message;
    }
    return result;
}
