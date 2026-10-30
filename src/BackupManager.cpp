#include "BackupManager.h"
#include "Settings.h"
#include "InstanceManager.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <atomic>
#include <memory>
#include <algorithm>

namespace {
const QString marker = ".gdlauncher-backup.json";
std::atomic<int> activeOperations{0};
struct Busy {
    Busy() { ++activeOperations; }
    ~Busy() { --activeOperations; }
};
bool validName(const QString &name) {
    return !name.isEmpty() && name != "." && name != ".." && !name.contains('/') && !name.contains('\\');
}
std::unique_ptr<QLockFile> lockOperations(QString &error) {
    const auto base = Settings::backupsDir();
    if (!QDir::isAbsolutePath(base) || !QDir().mkpath(base)) {
        error = "Could not open the configured backup folder.";
        return {};
    }
    auto lock = std::make_unique<QLockFile>(base + "/.backup-operation.lock");
    if (!lock->tryLock(0)) {
        error = "Another backup operation is in progress, or the backup folder is not writable. Please retry.";
        return {};
    }
    return lock;
}
QJsonObject backupMetadata(const QString &path) {
    const auto canonical = QFileInfo(path).canonicalFilePath();
    const auto parent = QFileInfo(canonical).dir().canonicalPath();
    const auto root = QFileInfo(Settings::backupsDir()).canonicalFilePath();
    if (root.isEmpty() || parent != root || QFileInfo(path).isSymLink()) return {};
    QFile file(path + "/" + marker);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto data = QJsonDocument::fromJson(file.readAll()).object();
    if (!data.value("complete").toBool() || !validName(data.value("instanceName").toString())) return {};
    return data;
}
bool collectFiles(const QString &directory, const QString &relative,
                  QStringList &files, QStringList &directories,
                  QSet<QString> ancestors, QString &error) {
    const QFileInfo root(directory);
    const QString real = root.canonicalFilePath();
    if (real.isEmpty() || !root.isReadable() || ancestors.contains(real)) {
        error = "Could not read the instance folder, or a directory link forms a loop: " + directory;
        return false;
    }
    ancestors.insert(real);
    const auto entries = QDir(directory).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const auto &entry : entries) {
        if (relative.isEmpty() && entry.fileName() == marker) continue;
        const QString path = relative.isEmpty() ? entry.fileName() : relative + "/" + entry.fileName();
        if (entry.isDir()) {
            directories << path;
            if (!collectFiles(entry.filePath(), path, files, directories, ancestors, error)) return false;
        } else if (entry.isFile() && entry.isReadable()) files << path;
        else {
            error = "Could not back up this instance item: " + entry.filePath();
            return false;
        }
    }
    return true;
}
bool copyTree(const QString &source, const QString &target, const BackupManager::Progress &progress, QString &error) {
    QStringList files, directories;
    if (!collectFiles(source, {}, files, directories, {}, error)) return false;
    for (const auto &dir : directories) if (!QDir().mkpath(target + "/" + dir)) {
        error = "Could not create destination folder: " + dir; return false;
    }
    int current = 0;
    for (const auto &file : files) {
        if (!QFile::copy(source + "/" + file, target + "/" + file)) {
            error = "Could not copy " + file + ". Check disk space and permissions."; return false;
        }
        if (progress) progress(file, ++current, files.size());
    }
    return true;
}
bool writeInstance(const QString &directory, const QString &name, bool forked, QString &error) {
    QFile file(directory + "/instance.json");
    if (!file.open(QIODevice::ReadOnly)) { error = "The backup is missing instance.json."; return false; }
    auto data = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!data.isObject() || data.object().isEmpty()) { error = "The backup's instance.json is invalid."; return false; }
    auto instance = data.object();
    instance["name"] = name;
    if (forked) instance["creationDate"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const auto bytes = QJsonDocument(instance).toJson();
    QSaveFile out(directory + "/instance.json");
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        error = "Could not write the restored instance metadata."; return false;
    }
    return true;
}
}

BackupResult BackupManager::createScheduled(const QString &name, const QDate &date, const Progress &progress) {
    Busy busy;
    if (!Settings::load().backupsEnabled) { BackupResult skipped; skipped.success = true; return skipped; }
    BackupResult failure;
    auto lock = lockOperations(failure.error);
    if (!lock) return failure;
    return snapshot(name, date, progress, false);
}
bool BackupManager::isBusy() { return activeOperations.load() > 0; }

BackupResult BackupManager::snapshot(const QString &name, const QDate &date, const Progress &progress, bool safety) {
    BackupResult result;
    auto settings = Settings::load();
    if (safety) {
        settings.backupsEnabled = true;
        settings.backupIntervalDays = 0;
        settings.backupScope = "instance";
        settings.backupKeepCount = 0;
    }
    if (!settings.backupsEnabled) { result.success = true; return result; }
    if (name.isEmpty() || name == "." || name == ".." || name.contains('/') || name.contains('\\') || !date.isValid()) {
        result.error = "Invalid instance name or backup date.";
        return result;
    }
    const QString source = QDir(Settings::instancesDir()).filePath(name);
    const QString base = Settings::backupsDir();
    if (!QDir::isAbsolutePath(base)) {
        result.error = "The backup folder must be an absolute path.";
        return result;
    }
    if (!QFileInfo::exists(source + "/instance.json")) {
        result.error = "Could not find instance.json for the daily backup.";
        return result;
    }
    QFile instanceFile(source + "/instance.json");
    if (!instanceFile.open(QIODevice::ReadOnly)) {
        result.error = "Could not read instance.json for the daily backup.";
        return result;
    }
    const auto instanceData = QJsonDocument::fromJson(instanceFile.readAll()).object();
    const QString createdAt = instanceData.value("creationDate").toString();
    instanceFile.close();
    if (!QDir().mkpath(base)) {
        result.error = "Could not create the Backups folder.";
        return result;
    }
    const auto instanceRoot = QFileInfo(Settings::instancesDir()).canonicalFilePath();
    const auto backupRoot = QFileInfo(base).canonicalFilePath();
#ifdef Q_OS_WIN
    const auto comparison = Qt::CaseInsensitive;
#else
    const auto comparison = Qt::CaseSensitive;
#endif
    if (backupRoot.compare(instanceRoot, comparison) == 0 ||
        backupRoot.startsWith(instanceRoot + "/", comparison)) {
        result.error = "Choose a backup folder outside the Instances directory.";
        return result;
    }
    QLockFile lock(base + "/.daily-backup.lock");
    if (!lock.tryLock(0)) {
        result.error = "Another launcher is creating a backup, or the Backups folder is not writable. Please retry.";
        return result;
    }
    QStringList previousBackups;
    QString recentPath;
    // Only a published snapshot with a completion marker counts for scheduling.
    for (const auto &folder : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!folder.startsWith(name + "-")) continue;
        if (QFileInfo(base + "/" + folder).isSymLink()) continue;
        QFile file(base + "/" + folder + "/" + marker);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const auto info = QJsonDocument::fromJson(file.readAll()).object();
        if (info.value("instanceName").toString() != name ||
            info.value("instanceCreatedAt").toString() != createdAt || !info.value("complete").toBool()) continue;
        previousBackups << base + "/" + folder;
        const int days = QDate::fromString(info.value("date").toString(), Qt::ISODate).daysTo(date);
        if (settings.backupIntervalDays > 0 && days >= 0 && days < settings.backupIntervalDays &&
            info.value("scope").toString("instance") == settings.backupScope &&
            info.value("reason").toString("scheduled") == "scheduled")
            recentPath = base + "/" + folder;
    }
    if (!recentPath.isEmpty()) { result.success = true; result.path = recentPath; return result; }
    QStringList files, directories;
    if (settings.backupScope == "managed") {
        files << "instance.json";
        for (const auto &item : InstanceManager::getManagedItems(instanceData.value("isGeodeCompatible").toBool(),
                                                                instanceData.value("useMegaHack").toBool())) {
            const QFileInfo entry(source + "/" + item);
            if (!entry.exists()) continue;
            if (entry.isDir()) {
                directories << item;
                if (!collectFiles(entry.filePath(), item, files, directories, {}, result.error)) return result;
            } else files << item;
        }
    } else if (!collectFiles(source, {}, files, directories, {}, result.error)) return result;
    QTemporaryDir staging(base + "/.partial-XXXXXX");
    if (!staging.isValid()) {
        result.error = "Could not create a temporary backup folder.";
        return result;
    }
    for (const auto &directory : directories) {
        if (!QDir().mkpath(staging.path() + "/" + directory)) {
            result.error = "Could not create a backup subfolder: " + directory;
            return result;
        }
    }
    int copied = 0;
    for (const auto &file : files) {
        if (!QFile::copy(source + "/" + file, staging.path() + "/" + file)) {
            result.error = "Could not back up " + file + ". Check disk space and permissions.";
            return result;
        }
        if (progress) progress(file, ++copied, files.size());
    }
    QSaveFile completed(staging.path() + "/" + marker);
    const auto json = QJsonDocument(QJsonObject{{"instanceName", name}, {"instanceCreatedAt", createdAt}, {"date", date.toString(Qt::ISODate)},
        {"scope", settings.backupScope}, {"complete", true},
        {"reason", safety ? "before_restore" : "scheduled"},
        {"createdAt", QDateTime::currentDateTime().toString(Qt::ISODateWithMs)}}).toJson();
    if (!completed.open(QIODevice::WriteOnly) || completed.write(json) != json.size() || !completed.commit()) {
        result.error = "Could not record the completed backup.";
        return result;
    }
    const QString timestamp = QDateTime(date, QTime::currentTime()).toString("yyyy-MM-dd_HH-mm-ss-zzz");
    result.path = base + "/" + name + "-" + timestamp;
    if (QFileInfo::exists(result.path)) result.path += "-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    if (!QDir().rename(staging.path(), result.path)) {
        result.error = "Could not publish the completed backup.";
        return result;
    }
    result.success = true;
    result.created = true;
    if (settings.backupKeepCount > 0) {
        std::stable_sort(previousBackups.begin(), previousBackups.end(), [](const QString &a, const QString &b) {
            return QFileInfo(a + "/" + marker).lastModified() < QFileInfo(b + "/" + marker).lastModified();
        });
        previousBackups << result.path;
        while (previousBackups.size() > settings.backupKeepCount) {
            const auto old = previousBackups.takeFirst();
            if (!QDir(old).removeRecursively()) result.warning = "Some old backups could not be removed.";
        }
    }
    return result;
}

QVector<BackupInfo> BackupManager::list() {
    QVector<BackupInfo> result;
    const QString base = Settings::backupsDir();
    for (const auto &folder : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed)) {
        const QString path = base + "/" + folder;
        const auto info = backupMetadata(path);
        if (info.isEmpty()) continue;
        result.push_back({path, info.value("instanceName").toString(), info.value("createdAt").toString(),
                          info.value("scope").toString("instance"), info.value("reason").toString("scheduled")});
    }
    return result;
}

BackupResult BackupManager::restore(const QString &path, const Progress &progress) {
    Busy busy;
    BackupResult result;
    auto lock = lockOperations(result.error);
    if (!lock) return result;
    const auto info = backupMetadata(path);
    if (info.isEmpty()) { result.error = "This is not a completed backup in the configured backup folder."; return result; }
    const QString name = info.value("instanceName").toString();
    const QString target = Settings::instancesDir() + "/" + name;
    if (!QFileInfo::exists(target + "/instance.json")) {
        result.error = "The original instance no longer exists. Use Fork to create a new instance from this backup.";
        return result;
    }
    QTemporaryDir staging(Settings::instancesDir() + "/.restore-XXXXXX");
    if (!staging.isValid()) { result.error = "Could not create a restore staging folder."; return result; }
    if (info.value("scope").toString() == "managed") {
        if (!copyTree(target, staging.path(), progress, result.error)) return result;
        for (const auto &item : InstanceManager::getAllManagedItems() + QStringList{"instance.json"}) {
            const auto destination = staging.path() + "/" + item;
            if (!QFileInfo::exists(destination)) continue;
            const bool removed = QFileInfo(destination).isDir() ? QDir(destination).removeRecursively() : QFile::remove(destination);
            if (!removed) { result.error = "Could not prepare restored save files."; return result; }
        }
    }
    if (!copyTree(path, staging.path(), progress, result.error) ||
        !writeInstance(staging.path(), name, false, result.error)) return result;
    // Always snapshot the current instance, even if automatic backups are off
    // or today's scheduled snapshot already exists. Do not prune during restore.
    const auto safety = snapshot(name, QDate::currentDate(),
        [progress](const QString &file, int current, int total) {
            if (progress) progress("Safety backup: " + file, current, total);
        }, true);
    if (!safety.success) { result.error = "Restore cancelled because the safety backup failed: " + safety.error; return result; }
    const QString previous = Settings::instancesDir() + "/.previous-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().rename(target, previous)) { result.error = "Could not move the current instance aside. No save data was replaced."; return result; }
    if (!QDir().rename(staging.path(), target)) {
        const bool rolledBack = QDir().rename(previous, target);
        result.error = rolledBack ? "Could not install the restored instance. The current instance was kept."
            : "Restore failed. The previous instance is at " + previous + " and its safety backup is at " + safety.path;
        return result;
    }
    if (!QDir(previous).removeRecursively()) result.warning = "The previous instance folder could not be cleaned up: " + previous;
    result.success = true;
    result.path = safety.path;
    return result;
}

BackupResult BackupManager::fork(const QString &path, const QString &name, const Progress &progress) {
    Busy busy;
    BackupResult result;
    auto lock = lockOperations(result.error);
    if (!lock) return result;
    if (backupMetadata(path).isEmpty() || !validName(name)) { result.error = "Invalid backup or instance name."; return result; }
    const QString target = Settings::instancesDir() + "/" + name;
    if (QFileInfo::exists(target)) { result.error = "An instance with that name already exists."; return result; }
    QDir().mkpath(Settings::instancesDir());
    QTemporaryDir staging(Settings::instancesDir() + "/.fork-XXXXXX");
    if (!staging.isValid()) { result.error = "Could not create the instance staging folder."; return result; }
    if (!copyTree(path, staging.path(), progress, result.error) ||
        !writeInstance(staging.path(), name, true, result.error)) return result;
    if (!QDir().rename(staging.path(), target)) { result.error = "Could not create the new instance."; return result; }
    result.success = true;
    result.path = target;
    return result;
}

bool BackupManager::remove(const QString &path, QString *error) {
    Busy busy;
    auto lock = lockOperations(*error);
    if (!lock) return false;
    if (backupMetadata(path).isEmpty()) { *error = "Invalid or incomplete backup."; return false; }
    if (!QDir(path).removeRecursively()) { *error = "Could not delete the backup. Check permissions."; return false; }
    return true;
}
