#pragma once
#include <QDate>
#include <QString>
#include <functional>
#include <QVector>

struct BackupInfo {
    QString path, instanceName, createdAt, scope, reason;
};

struct BackupResult {
    bool success = false;
    bool created = false;
    QString path;
    QString error;
    QString warning;
};

class BackupManager {
public:
    using Progress = std::function<void(const QString &file, int current, int total)>;
    // Applies the configured per-instance schedule (daily by default).
    static BackupResult createScheduled(const QString &instanceName,
                                    const QDate &date = QDate::currentDate(),
                                    const Progress &progress = {});
    static QVector<BackupInfo> list();
    static BackupResult restore(const QString &backupPath, const Progress &progress = {});
    static BackupResult fork(const QString &backupPath, const QString &newName, const Progress &progress = {});
    static bool remove(const QString &backupPath, QString *error);
    static bool isBusy();
private:
    static BackupResult snapshot(const QString &name, const QDate &date,
                                 const Progress &progress, bool safety);
};
