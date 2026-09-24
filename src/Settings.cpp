#include "Settings.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QProcessEnvironment>

QJsonObject AppSettings::toJson() const {
    QJsonObject o;
    o["theme"] = theme;
    o["close_behavior"] = closeBehavior;
    o["sync_delay"] = syncDelay;
    o["last_run_version"] = lastRunVersion.isEmpty() ? QJsonValue() : QJsonValue(lastRunVersion);
    o["enable_log_output"] = enableLogOutput;
    o["update_package_type"] = updatePackageType;
    return o;
}

AppSettings AppSettings::fromJson(const QJsonObject &obj) {
    AppSettings s;
    if (obj.contains("theme")) s.theme = obj["theme"].toString(s.theme);
    if (obj.contains("close_behavior")) s.closeBehavior = obj["close_behavior"].toString(s.closeBehavior);
    if (obj.contains("sync_delay")) s.syncDelay = obj["sync_delay"].toInt(s.syncDelay);
    if (obj.contains("last_run_version")) s.lastRunVersion = obj["last_run_version"].toString();
    if (obj.contains("enable_log_output")) s.enableLogOutput = obj["enable_log_output"].toBool(s.enableLogOutput);
    if (obj.contains("update_package_type")) s.updatePackageType = obj["update_package_type"].toString(s.updatePackageType);
    return s;
}

QString Settings::baseDir() {
    // Mirrors the Electron build's app.getPath('userData'), which uses
    // %AppData%\gd-instance-launcher on Windows (Roaming, no org subfolder) and
    // ~/.config/gd-instance-launcher on Linux. We use our own product name,
    // "gdlauncher", instead -- deliberately a separate directory from the
    // Electron build so the two apps never share or clobber each other's data.
#if defined(Q_OS_WIN)
    const QString appData = QProcessEnvironment::systemEnvironment().value("APPDATA");
    if (!appData.isEmpty()) return appData + "/gdlauncher";
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/gdlauncher";
#elif defined(Q_OS_MACOS)
    return QDir::homePath() + "/Library/Application Support/gdlauncher";
#else
    return QDir::homePath() + "/.config/gdlauncher";
#endif
}

QString Settings::instancesDir() { return baseDir() + "/Instances"; }
QString Settings::trashDir() { return baseDir() + "/Trash"; }
QString Settings::versionsDir() { return baseDir() + "/Versions"; }
QString Settings::backupsDir() { return baseDir() + "/Backups"; }
QString Settings::settingsFile() { return baseDir() + "/launcher_settings.json"; }

void Settings::ensureDirs() {
    QDir().mkpath(instancesDir());
    QDir().mkpath(versionsDir());
    QDir().mkpath(trashDir());
    QDir().mkpath(backupsDir());
}

AppSettings Settings::load() {
    QFile f(settingsFile());
    if (!f.open(QIODevice::ReadOnly)) return AppSettings();
    const QByteArray data = f.readAll();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return AppSettings();
    return AppSettings::fromJson(doc.object());
}

bool Settings::save(const AppSettings &settings) {
    QFile f(settingsFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QJsonDocument doc(settings.toJson());
    f.write(doc.toJson(QJsonDocument::Indented));
    return true;
}
