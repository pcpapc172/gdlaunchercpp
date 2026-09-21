#include "Settings.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

QJsonObject AppSettings::toJson() const {
    QJsonObject o;
    o["theme"] = theme;
    o["close_behavior"] = closeBehavior;
    o["sync_delay"] = syncDelay;
    o["last_run_version"] = lastRunVersion.isEmpty() ? QJsonValue() : QJsonValue(lastRunVersion);
    o["enable_log_output"] = enableLogOutput;
    return o;
}

AppSettings AppSettings::fromJson(const QJsonObject &obj) {
    AppSettings s;
    if (obj.contains("theme")) s.theme = obj["theme"].toString(s.theme);
    if (obj.contains("close_behavior")) s.closeBehavior = obj["close_behavior"].toString(s.closeBehavior);
    if (obj.contains("sync_delay")) s.syncDelay = obj["sync_delay"].toInt(s.syncDelay);
    if (obj.contains("last_run_version")) s.lastRunVersion = obj["last_run_version"].toString();
    if (obj.contains("enable_log_output")) s.enableLogOutput = obj["enable_log_output"].toBool(s.enableLogOutput);
    return s;
}

QString Settings::baseDir() {
    // Equivalent of Electron's app.getPath('userData') -> <AppData>/GDLauncher
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
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
