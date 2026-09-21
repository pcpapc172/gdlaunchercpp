#pragma once
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVector>
#include <functional>

struct InstanceInfo {
    QString name;
    qint64 size = -1; // -1 = not calculated yet
    QString version;
    QJsonObject data;
    bool valid = true;
};

struct InstanceSaveResult {
    bool success = false;
    QString error;
};

// Port of the instance-management functions from main.js
class InstanceManager {
public:
    static QStringList getAllManagedItems();
    static QStringList getManagedItems(bool isGeode, bool useMegahack);

    static QVector<InstanceInfo> getInstances();
    static QVector<QPair<QString, qint64>> calculateInstanceSizes();

    static InstanceSaveResult createInstance(const QJsonObject &data);
    static InstanceSaveResult editInstance(const QString &originalName, const QJsonObject &data);
    static InstanceSaveResult deleteInstance(const QString &name);

    static qint64 getItemSize(const QString &itemPath);
    static void ensureInstanceIntegrity(const QString &instancePath, bool isGeode, bool useMegahack);

    using FileProgressCb = std::function<void(const QString &file, int current)>;
    static void transferManagedItems(const QString &src, const QString &dest,
                                      const QStringList &managedItems, bool move,
                                      const FileProgressCb &onFileProgress = nullptr);

    static QString moveToTrash(const QString &sourcePath, const QString &prefix);

private:
    static void copyDirWithProgress(const QString &src, const QString &dest, const QString &instanceRoot,
                                     int &counter, const FileProgressCb &onFileProgress);
    static void copyDir(const QString &src, const QString &dest);
    // Builds the full instance.json contents from an InstanceDialog-shaped QJsonObject. Shared
    // by createInstance/editInstance so the two can never again drift into writing different
    // field sets (createInstance used to silently drop useSteamEmu, skipRestartCheck,
    // executablePath, and the Geode-logging fields entirely).
    static QJsonObject buildInstanceJson(const QJsonObject &data, const QString &name, const QString &creationDate);
};
