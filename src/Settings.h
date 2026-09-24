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
    // How this copy of GDLauncher was installed, so an update knows which release asset to
    // fetch and how to apply it: "portable" (zip/tar.gz -- download+extract, same as before),
    // "nsis"/"msi" (Windows installers), or "deb"/"rpm" (Linux packages) -- the latter four all
    // download the real installer/package and hand it off to run, instead of just extracting.
    QString updatePackageType = "portable";

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

    static void ensureDirs();

    static AppSettings load();
    static bool save(const AppSettings &settings);
};
