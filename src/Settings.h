#pragma once
#include <QString>
#include <QJsonObject>

// Mirrors DEFAULT_SETTINGS / load-settings / save-settings in the original main.js
struct AppSettings {
    QString theme = "Dark";
    QString closeBehavior = "Stay Open";
    int syncDelay = 5;
    QString lastRunVersion;
    bool enableLogOutput = false;
    bool useProtonGE = false;
    QString nongDownloadFolder = "%LOCALAPPDATA%/GeometryDash";
    bool closeGuardEnabled = true;
    bool allowForceClose = true;
    bool confirmForceCloseTwice = true;
    bool backupsEnabled = true;
    int backupIntervalDays = 1; // 0 = every launch, 1 = daily
    int backupKeepCount = 0; // 0 = keep all completed backups
    QString backupDirectory; // empty = launcher data/Backups
    QString backupScope = "instance"; // "instance" or "managed"

    QJsonObject toJson() const;
    static AppSettings fromJson(const QJsonObject &obj);
};

class Settings {
public:
    static QString baseDir();
    static QString instancesDir();
    static QString trashDir();
    static QString versionsDir();
    static QString backupsDir();
    static QString settingsFile();
    static QString installationType();
    static QString gameSaveDir(const QString &saveFolderName, bool useProton = false);

    static void ensureDirs();

    static AppSettings load();
    static bool save(const AppSettings &settings);
};
