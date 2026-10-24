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
