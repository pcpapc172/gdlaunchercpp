#include "InstanceManager.h"
#include "Settings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDateTime>

QStringList InstanceManager::getAllManagedItems() {
    return {
        "CCGameManager.dat", "CCGameManager2.dat", "CCGameManager.dat.bak",
        "CCLocalLevels.dat", "CCLocalLevels2.dat", "CCLocalLevels.dat.bak",
        "geode", "geode-backups", "trashed-levels",
        "CCBetterInfo.dat", "CCBetterInfo2.dat",
        "CCBetterInfoCache.dat", "CCBetterInfoCache2.dat",
        "CCBetterInfoStats.dat", "CCBetterInfoStats2.dat"
    };
}

QStringList InstanceManager::getManagedItems(bool isGeode, bool useMegahack) {
    QStringList items = {
        "CCGameManager.dat", "CCGameManager2.dat", "CCGameManager.dat.bak",
        "CCLocalLevels.dat", "CCLocalLevels2.dat", "CCLocalLevels.dat.bak"
    };
    if (isGeode) items << "geode" << "geode-backups" << "trashed-levels";
    if (useMegahack) items << "CCBetterInfo.dat" << "CCBetterInfo2.dat"
                           << "CCBetterInfoCache.dat" << "CCBetterInfoCache2.dat"
                           << "CCBetterInfoStats.dat" << "CCBetterInfoStats2.dat";
    return items;
}

qint64 InstanceManager::getItemSize(const QString &itemPath) {
    QFileInfo fi(itemPath);
    if (!fi.exists()) return 0;
    if (fi.isFile()) return fi.size();
    if (fi.isDir()) {
        qint64 total = 0;
        QDir dir(itemPath);
        const auto entries = dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
        for (const QString &e : entries) total += getItemSize(dir.filePath(e));
        return total;
    }
    return 0;
}

static QString instanceJsonPath(const QString &instancePath) {
    return instancePath + "/instance.json";
}

QVector<InstanceInfo> InstanceManager::getInstances() {
    QVector<InstanceInfo> out;
    QDir dir(Settings::instancesDir());
    if (!dir.exists()) return out;
    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : entries) {
        InstanceInfo info;
        info.name = name;
        const QString instancePath = dir.filePath(name);
        QFile f(instanceJsonPath(instancePath));
        if (!f.open(QIODevice::ReadOnly)) {
            info.size = 0; info.version = "Error"; info.valid = false;
            out.push_back(info);
            continue;
        }
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            info.size = 0; info.version = "Error"; info.valid = false;
            out.push_back(info);
            continue;
        }
        info.data = doc.object();
        const QString versionType = info.data.value("versionType").toString();
        if (versionType == "local") {
            info.version = info.data.value("version").toString();
        } else {
            const QString exePath = info.data.value("executablePath").toString();
            info.version = QFileInfo(exePath).dir().dirName();
        }
        out.push_back(info);
    }
    std::sort(out.begin(), out.end(), [](const InstanceInfo &a, const InstanceInfo &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return out;
}

QVector<QPair<QString, qint64>> InstanceManager::calculateInstanceSizes() {
    QVector<QPair<QString, qint64>> out;
    QDir dir(Settings::instancesDir());
    if (!dir.exists()) return out;
    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : entries) {
        const QString instancePath = dir.filePath(name);
        QFile f(instanceJsonPath(instancePath));
        if (!f.open(QIODevice::ReadOnly)) { out.push_back({name, 0}); continue; }
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) { out.push_back({name, 0}); continue; }
        const QJsonObject data = doc.object();
        const QStringList managed = getManagedItems(data.value("isGeodeCompatible").toBool(),
                                                      data.value("useMegaHack").toBool());
        qint64 total = 0;
        for (const QString &item : managed) total += getItemSize(instancePath + "/" + item);
        out.push_back({name, total});
    }
    return out;
}

void InstanceManager::ensureInstanceIntegrity(const QString &instancePath, bool isGeode, bool useMegahack) {
    const QStringList items = getManagedItems(isGeode, useMegahack);
    for (const QString &item : items) {
        const QString itemPath = instancePath + "/" + item;
        if (QFileInfo::exists(itemPath)) continue;
        if (item.contains('.') && !item.startsWith("geode")) {
            QFile f(itemPath);
            f.open(QIODevice::WriteOnly);
        } else {
            QDir().mkpath(itemPath);
        }
    }
}

QJsonObject InstanceManager::buildInstanceJson(const QJsonObject &data, const QString &name, const QString &creationDate) {
    QJsonObject instanceData;
    instanceData["name"] = name;
    instanceData["versionType"] = data.value("versionType").toString("remote");
    instanceData["version"] = data.value("version").toString("2.2/2.207");
    instanceData["versionPath"] = data.value("versionPath").toString("");
    instanceData["saveFolderName"] = data.value("saveFolderName").toString("GeometryDash");
    instanceData["isGeodeCompatible"] = data.contains("isGeodeCompatible") ? data.value("isGeodeCompatible") : QJsonValue(true);
    instanceData["useMegaHack"] = data.contains("useMegaHack") ? data.value("useMegaHack") : QJsonValue(true);
    instanceData["useSteamEmu"] = data.value("useSteamEmu").toBool(false);
    instanceData["skipRestartCheck"] = data.value("skipRestartCheck").toBool(false);
    instanceData["enableGeodeLogging"] = data.value("enableGeodeLogging").toBool(false);
    instanceData["geodeLogModVersion"] = data.value("geodeLogModVersion").toString("");
    instanceData["executablePath"] = data.value("executablePath");
    instanceData["creationDate"] = creationDate;
    return instanceData;
}

InstanceSaveResult InstanceManager::createInstance(const QJsonObject &data) {
    InstanceSaveResult res;
    const QString name = data.value("name").toString();
    if (name.isEmpty()) { res.error = "Instance name is required"; return res; }

    const QString instancePath = Settings::instancesDir() + "/" + name;
    if (QFileInfo::exists(instancePath)) { res.error = "Instance already exists"; return res; }

    QDir().mkpath(instancePath);

    const QJsonObject instanceData = buildInstanceJson(data, name, QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    QFile f(instanceJsonPath(instancePath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { res.error = "Failed to write instance.json"; return res; }
    f.write(QJsonDocument(instanceData).toJson(QJsonDocument::Indented));
    f.close();

    ensureInstanceIntegrity(instancePath, instanceData["isGeodeCompatible"].toBool(), instanceData["useMegaHack"].toBool());

    res.success = true;
    return res;
}

InstanceSaveResult InstanceManager::editInstance(const QString &originalName, const QJsonObject &data) {
    InstanceSaveResult res;
    const QString oldPath = Settings::instancesDir() + "/" + originalName;
    const QString newName = data.value("name").toString();
    const QString newPath = Settings::instancesDir() + "/" + newName;

    if (originalName != newName) {
        if (QFileInfo::exists(newPath)) { res.error = "An instance with that name already exists"; return res; }
        if (!QDir().rename(oldPath, newPath)) { res.error = "Failed to rename instance folder"; return res; }
    }

    const QJsonObject instanceData = buildInstanceJson(data, newName, data.value("creationDate").toString());

    QFile f(instanceJsonPath(newPath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { res.error = "Failed to write instance.json"; return res; }
    f.write(QJsonDocument(instanceData).toJson(QJsonDocument::Indented));
    f.close();

    ensureInstanceIntegrity(newPath, data.value("isGeodeCompatible").toBool(), data.value("useMegaHack").toBool());

    res.success = true;
    return res;
}

QString InstanceManager::moveToTrash(const QString &sourcePath, const QString &prefix) {
    const QString dateStr = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH-mm-ss-zzz");
    const QString folderName = prefix + "_" + dateStr;
    const QString trashPath = Settings::trashDir() + "/" + folderName;
    QDir().mkpath(Settings::trashDir());
    if (!QFileInfo::exists(sourcePath)) return QString();

    QFileInfo fi(sourcePath);
    if (fi.isDir()) {
        QDir parent(Settings::trashDir());
        if (!QDir().rename(sourcePath, trashPath)) {
            copyDir(sourcePath, trashPath);
            QDir(sourcePath).removeRecursively();
        }
    } else {
        QDir().mkpath(trashPath);
        const QString destFile = trashPath + "/" + fi.fileName();
        if (!QFile::rename(sourcePath, destFile)) {
            QFile::copy(sourcePath, destFile);
            QFile::remove(sourcePath);
        }
    }
    return trashPath;
}

InstanceSaveResult InstanceManager::deleteInstance(const QString &name) {
    InstanceSaveResult res;
    const QString instancePath = Settings::instancesDir() + "/" + name;
    moveToTrash(instancePath, "Instance_" + name);
    res.success = true;
    return res;
}

void InstanceManager::copyDir(const QString &src, const QString &dest) {
    QDir().mkpath(dest);
    QDir srcDir(src);
    const auto entries = srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const QFileInfo &e : entries) {
        const QString destPath = dest + "/" + e.fileName();
        if (e.isDir()) copyDir(e.filePath(), destPath);
        else QFile::copy(e.filePath(), destPath);
    }
}

void InstanceManager::copyDirWithProgress(const QString &src, const QString &dest, const QString &instanceRoot,
                                           int &counter, const FileProgressCb &onFileProgress) {
    QDir().mkpath(dest);
    QDir srcDir(src);
    const auto entries = srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const QFileInfo &e : entries) {
        const QString destPath = dest + "/" + e.fileName();
        if (e.isDir()) {
            copyDirWithProgress(e.filePath(), destPath, instanceRoot, counter, onFileProgress);
        } else {
            counter++;
            const QString relPath = QDir(instanceRoot).relativeFilePath(e.filePath());
            if (onFileProgress) onFileProgress(relPath, counter);
            QFile::copy(e.filePath(), destPath);
        }
    }
}

void InstanceManager::transferManagedItems(const QString &src, const QString &dest,
                                            const QStringList &managedItems, bool move,
                                            const FileProgressCb &onFileProgress) {
    int counter = 0;
    for (const QString &item : managedItems) {
        const QString srcPath = src + "/" + item;
        const QString destPath = dest + "/" + item;
        if (!QFileInfo::exists(srcPath)) continue;
        if (QFileInfo::exists(destPath)) {
            QFileInfo destInfo(destPath);
            if (destInfo.isDir()) QDir(destPath).removeRecursively();
            else QFile::remove(destPath);
        }
        QFileInfo srcInfo(srcPath);
        if (srcInfo.isDir()) {
            copyDirWithProgress(srcPath, destPath, src, counter, onFileProgress);
            if (move) QDir(srcPath).removeRecursively();
        } else {
            counter++;
            if (onFileProgress) onFileProgress(item, counter);
            if (move) {
                if (!QFile::rename(srcPath, destPath)) {
                    QFile::copy(srcPath, destPath);
                    QFile::remove(srcPath);
                }
            } else {
                QFile::copy(srcPath, destPath);
            }
        }
    }
}
