#include "Settings.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <msi.h>
#include <msiquery.h>

static bool msiOwnsExecutable(const QString &productCode, const QString &executable) {
    // Older MSI packages did not publish InstallLocation. Query their registered
    // component key paths rather than guessing from a Program Files directory.
    const auto product = productCode.toStdWString();
    PMSIHANDLE package;
    if (MsiOpenProductW(product.c_str(), &package) != ERROR_SUCCESS) return false;
    PMSIHANDLE database = MsiGetActiveDatabase(package);
    PMSIHANDLE view;
    if (MsiDatabaseOpenViewW(database, L"SELECT `ComponentId` FROM `Component`", &view) != ERROR_SUCCESS ||
        MsiViewExecute(view, 0) != ERROR_SUCCESS) return false;
    for (;;) {
        PMSIHANDLE record;
        if (MsiViewFetch(view, &record) != ERROR_SUCCESS) break;
        wchar_t component[39];
        DWORD componentLength = 39;
        if (MsiRecordGetStringW(record, 1, component, &componentLength) != ERROR_SUCCESS) continue;
        wchar_t path[32768];
        DWORD length = 32768;
        if (MsiGetComponentPathW(product.c_str(), component, path, &length) != INSTALLSTATE_LOCAL) continue;
        const QString installed = QFileInfo(QString::fromWCharArray(path, length)).canonicalFilePath();
        if (!installed.isEmpty() && installed.compare(executable, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}
#endif

QJsonObject AppSettings::toJson() const {
    QJsonObject o;
    o["theme"] = theme;
    o["close_behavior"] = closeBehavior;
    o["sync_delay"] = syncDelay;
    o["last_run_version"] = lastRunVersion.isEmpty() ? QJsonValue() : QJsonValue(lastRunVersion);
    o["enable_log_output"] = enableLogOutput;
    o["use_proton_ge"] = useProtonGE;
    o["nong_download_folder"] = nongDownloadFolder;
    return o;
}

AppSettings AppSettings::fromJson(const QJsonObject &obj) {
    AppSettings s;
    if (obj.contains("theme")) s.theme = obj["theme"].toString(s.theme);
    if (obj.contains("close_behavior")) s.closeBehavior = obj["close_behavior"].toString(s.closeBehavior);
    if (obj.contains("sync_delay")) s.syncDelay = obj["sync_delay"].toInt(s.syncDelay);
    if (obj.contains("last_run_version")) s.lastRunVersion = obj["last_run_version"].toString();
    if (obj.contains("enable_log_output")) s.enableLogOutput = obj["enable_log_output"].toBool(s.enableLogOutput);
    s.useProtonGE = obj.value("use_proton_ge").toBool(false);
    s.nongDownloadFolder = obj.value("nong_download_folder").toString(s.nongDownloadFolder);
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

QString Settings::gameSaveDir(const QString &saveFolderName, bool useProton) {
#ifdef Q_OS_LINUX
    if (useProton) return baseDir() + "/ProtonPrefix/drive_c/users/steamuser/AppData/Local/" + saveFolderName;
    const QString home = QDir::homePath();
    const QString user = QFileInfo(home).fileName();
    const QString prefix = home + "/.wine/drive_c/users/" + user;
    const QString modern = prefix + "/AppData/Local/" + saveFolderName;
    const QString legacy = prefix + "/Local Settings/Application Data/" + saveFolderName;
    if (QFileInfo::exists(modern + "/CCGameManager.dat")) return modern;
    if (QFileInfo::exists(legacy + "/CCGameManager.dat")) return legacy;
    return modern;
#else
    Q_UNUSED(useProton);
    return QProcessEnvironment::systemEnvironment().value("LOCALAPPDATA") + "/" + saveFolderName;
#endif
}

QString Settings::installationType() {
    // Detect ownership of THIS executable, so a portable copy beside an installed
    // copy does not accidentally try to update the system package.
    const QString executable = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
#ifdef Q_OS_LINUX
    auto query = [](const QString &command, const QStringList &args) {
        QProcess process;
        process.start(command, args);
        if (!process.waitForFinished(3000)) { process.kill(); process.waitForFinished(); return QString(); }
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
            ? QString::fromUtf8(process.readAllStandardOutput()).trimmed() : QString();
    };
    const auto owners = query("dpkg-query", {"-S", executable}).split('\n');
    for (const auto &owner : owners) {
        if (owner.section(':', 0, 0) == "gdlauncher") return "deb";
    }
    if (query("rpm", {"-qf", "--qf", "%{NAME}", executable}) == "gdlauncher") return "rpm";
#elif defined(Q_OS_WIN)
    const QStringList roots = {
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "HKEY_LOCAL_MACHINE\\Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
    };
    for (const auto &root : roots) {
        QSettings registry(root, QSettings::NativeFormat);
        for (const auto &group : registry.childGroups()) {
            registry.beginGroup(group);
            const QString name = registry.value("DisplayName").toString();
            QString location = QDir::fromNativeSeparators(registry.value("InstallLocation").toString());
            const QString uninstall = registry.value("UninstallString").toString();
            const bool msi = registry.value("WindowsInstaller").toInt() == 1;
            registry.endGroup();
            if (!name.startsWith("GDLauncher", Qt::CaseInsensitive)) continue;
            if (msi && msiOwnsExecutable(group, executable)) return "msi";
            if (location.isEmpty() && !msi) {
                const auto command = QProcess::splitCommand(uninstall);
                if (!command.isEmpty()) location = QFileInfo(QDir::fromNativeSeparators(command.first())).absolutePath();
            }
            if (location.isEmpty()) continue;
            const QString installed = QFileInfo(location + "/bin/gdlauncher.exe").canonicalFilePath();
            if (!installed.isEmpty() && installed.compare(executable, Qt::CaseInsensitive) == 0) {
                if (msi) return "msi";
                if (uninstall.contains("uninstall", Qt::CaseInsensitive)) return "nsis";
            }
        }
    }
#endif
    return "portable";
}

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
