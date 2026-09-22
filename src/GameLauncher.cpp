#include "GameLauncher.h"
#include "Settings.h"
#include "InstanceManager.h"
#include "DebugLog.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>
#include <QDateTime>
#include <QStandardPaths>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QUrl>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QDir>
#include <QPushButton>
#include <QTime>
#include <QCoreApplication>

#ifdef Q_OS_LINUX
static const bool kIsLinux = true;
#else
static const bool kIsLinux = false;
#endif

static QByteArray httpGetSync(const QUrl &url, int timeoutMs = 8000) {
    QNetworkAccessManager mgr;
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *reply = mgr.get(req);
    QEventLoop loop;
    QTimer timer; timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    QByteArray result;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) result = reply->readAll();
    reply->deleteLater();
    return result;
}

GameLauncher::GameLauncher(QWidget *dialogParent, QObject *parent)
    : QObject(parent), m_dialogParent(dialogParent) {}

QString GameLauncher::linuxAppDataPath(const QString &saveFolderName) const {
    if (!kIsLinux) {
        const QString localAppData = QProcessEnvironment::systemEnvironment().value("LOCALAPPDATA");
        return localAppData + "/" + saveFolderName;
    }
    const QString home = QDir::homePath();
    const QString username = QFileInfo(home).fileName();
    const QString wineUserDir = home + "/.wine/drive_c/users/" + username;
    const QStringList candidates = {
        wineUserDir + "/AppData/Local/" + saveFolderName,
        wineUserDir + "/Local Settings/Application Data/" + saveFolderName
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c + "/CCGameManager.dat")) return c;
    }
    return candidates.first();
}

bool GameLauncher::checkProcessRunning(const QString &processName) const {
    QProcess p;
    if (kIsLinux) {
        p.start("pgrep", {"-f", processName});
    } else {
        p.start("tasklist", {"/FI", QString("IMAGENAME eq %1").arg(processName), "/NH"});
    }
    if (!p.waitForFinished(3000)) { p.kill(); return false; }
    const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
    if (kIsLinux) return !out.trimmed().isEmpty();
    return out.toLower().contains(processName.toLower());
}

GameLauncher::PrepResult GameLauncher::prepareLocalAppData(const QString &localPath, const QString &infoPath, bool isTour) {
    PrepResult res;
    GD_DEBUG_LOG("transfer", QString("Scanning %1 for unmanaged/leftover save data").arg(localPath));
    const QStringList allItems = InstanceManager::getAllManagedItems();
    QStringList foundInAppData;
    for (const QString &item : allItems) {
        if (QFileInfo::exists(localPath + "/" + item)) foundInAppData << item;
    }

    if (foundInAppData.isEmpty()) {
        GD_DEBUG_LOG("transfer", "Nothing found there; skipping.");
        res.success = true; res.found = false; return res;
    }
    GD_DEBUG_LOG("transfer", QString("Found: %1").arg(foundInAppData.join(", ")));

    if (QFileInfo::exists(infoPath)) {
        if (isTour) { res.success = true; res.found = false; return res; }
        QFile f(infoPath);
        if (!f.open(QIODevice::ReadOnly)) { res.success = false; res.error = "Failed to read info.json"; return res; }
        QJsonObject info = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        const QString targetDir = Settings::instancesDir() + "/" + info.value("instanceName").toString();
        if (!QFileInfo::exists(targetDir + "/instance.json")) {
            GD_DEBUG_LOG("transfer", QString("Stale info.json pointed at missing instance '%1'; discarding.")
                .arg(info.value("instanceName").toString()));
            QFile::remove(infoPath);
            res.success = true;
            return res;
        }
        QFile pf(targetDir + "/instance.json");
        pf.open(QIODevice::ReadOnly);
        QJsonObject prevData = QJsonDocument::fromJson(pf.readAll()).object();
        pf.close();
        GD_DEBUG_LOG("transfer", QString("Recovering leftover data back into instance '%1' (previous run likely crashed before syncing)")
            .arg(info.value("instanceName").toString()));
        InstanceManager::transferManagedItems(
            linuxAppDataPath(prevData.value("saveFolderName").toString()), targetDir,
            InstanceManager::getManagedItems(prevData.value("isGeodeCompatible").toBool(),
                                              prevData.value("useMegaHack").toBool()),
            true);
        QFile::remove(infoPath);
        GD_DEBUG_LOG("transfer", "Recovery transfer complete.");
        res.success = true; res.found = true;
        return res;
    }

    const QString fileListStr = foundInAppData.join(", ");
    QMessageBox box(m_dialogParent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle("Unmanaged Save Data Found");
    box.setText(QString("The following unmanaged files were found:\n\n%1\n\nWhat would you like to do?").arg(fileListStr));
    QPushButton *importBtn = box.addButton("Import as \"Imported Instance\"", QMessageBox::AcceptRole);
    QPushButton *trashBtn = box.addButton("Move to Trash", QMessageBox::DestructiveRole);
    box.addButton("Cancel", QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() == importBtn) {
        const QString newName = "Imported_" + QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH-mm-ss");
        const QString newInstancePath = Settings::instancesDir() + "/" + newName;
        GD_DEBUG_LOG("transfer", QString("Importing unmanaged data as new instance '%1'").arg(newName));
        QDir().mkpath(newInstancePath);
        QJsonObject instanceData;
        instanceData["name"] = newName;
        instanceData["versionType"] = "local";
        instanceData["version"] = "2.2/2.207";
        instanceData["saveFolderName"] = "GeometryDash";
        instanceData["isGeodeCompatible"] = true;
        instanceData["useMegaHack"] = true;
        instanceData["creationDate"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        QFile jf(newInstancePath + "/instance.json");
        jf.open(QIODevice::WriteOnly);
        jf.write(QJsonDocument(instanceData).toJson(QJsonDocument::Indented));
        jf.close();
        InstanceManager::transferManagedItems(localPath, newInstancePath, foundInAppData, true);
        GD_DEBUG_LOG("transfer", "Import complete.");
        res.success = true; res.found = true;
    } else if (box.clickedButton() == trashBtn) {
        const QString trashFolder = Settings::trashDir() + "/UnmanagedData_" +
                                     QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH-mm-ss-zzz");
        GD_DEBUG_LOG("transfer", QString("Moving unmanaged data to trash: %1").arg(trashFolder));
        QDir().mkpath(trashFolder);
        InstanceManager::transferManagedItems(localPath, trashFolder, foundInAppData, true);
        GD_DEBUG_LOG("transfer", "Moved to trash.");
        res.success = true; res.found = true;
    } else {
        GD_DEBUG_LOG("transfer", "User cancelled the unmanaged-data prompt; launch aborted.");
        res.success = false; res.error = "Launch cancelled by user.";
    }
    return res;
}

bool GameLauncher::handleFirstRunImport() {
    const AppSettings ignored; // just to reuse infoJsonPath format
    const PrepResult res = prepareLocalAppData(linuxAppDataPath("GeometryDash"), Settings::baseDir() + "/info.json", true);
    return res.found;
}

void GameLauncher::appendLog(const QString &line) {
    m_logBuffer.push_back(line);
    if (m_logBuffer.size() > 2000) m_logBuffer.removeFirst();
    emit logLine(line);
}

void GameLauncher::launchInstance(const QString &instanceName) {
    if (m_gameRunning) {
        emit launchComplete();
        emit statusUpdate("Game is already running");
        return;
    }
    if (m_syncing) {
        emit launchComplete();
        emit statusUpdate("Please wait, syncing in progress...");
        return;
    }

    // Set for the whole prep/copy phase below, so a close attempt mid-launch is rejected the
    // same way as one during an active run or sync -- most of this happens synchronously on
    // the UI thread anyway (so a close can't land here today), but the flag makes that
    // guarantee explicit rather than incidental, and holds if any of this becomes async later.
    m_launching = true;

    emit statusUpdate(QString("Preparing to launch %1...").arg(instanceName));

    const QString instancePath = Settings::instancesDir() + "/" + instanceName;
    QFile jf(instancePath + "/instance.json");
    if (!jf.open(QIODevice::ReadOnly)) {
        m_launching = false;
        emit launchComplete();
        emit statusUpdate("Launch failed: could not read instance.json");
        return;
    }
    const QJsonObject data = QJsonDocument::fromJson(jf.readAll()).object();
    jf.close();

    LaunchContext ctx;
    ctx.instanceName = instanceName;
    ctx.data = data;

    const bool isCustomExe = data.value("versionType").toString() != "local";

    if (isCustomExe) {
        // Custom Executable: executablePath IS the .exe itself (that's what the file-picker
        // in InstanceDialog stores), so the working directory is its parent folder -- it must
        // never be joined onto itself as if it were a versions/<category>/<version> directory
        // (that produced paths like ".../GeometryDash.exe/GeometryDash.exe" and always failed
        // with "Game executable not found").
        ctx.exePath = data.value("executablePath").toString();
        ctx.versionPath = QFileInfo(ctx.exePath).absolutePath();
    } else {
        ctx.versionPath = Settings::versionsDir() + "/" + data.value("version").toString();
    }
    GD_DEBUG_LOG("launch", QString("Resolved version path: %1").arg(ctx.versionPath));

    if (!QFileInfo::exists(ctx.versionPath)) {
        GD_DEBUG_LOG("launch", "Version path does not exist");
        m_launching = false;
        emit launchComplete();
        emit statusUpdate("Version not found");
        return;
    }

    if (!isCustomExe) {
        QString executable = "GeometryDash.exe";
        QString steamEmulator = "SmartSteamEmu.exe";
        const QString versionJsonPath = ctx.versionPath + "/version.json";
        if (QFileInfo::exists(versionJsonPath)) {
            QFile vf(versionJsonPath);
            vf.open(QIODevice::ReadOnly);
            const QJsonObject vc = QJsonDocument::fromJson(vf.readAll()).object();
            if (vc.contains("executable")) executable = vc.value("executable").toString();
            if (vc.contains("steam_emulator")) steamEmulator = vc.value("steam_emulator").toString();
        }

        ctx.gameProcessName = executable;

        // "Launch via SmartSteamEmu.exe" means launch that emulator executable INSTEAD of the
        // game exe directly -- it's the emulator's job to start the real game itself with the
        // Steamworks API faked out. This was saved to instance.json but never actually
        // consulted anywhere in launch, so the checkbox did nothing.
        if (data.value("useSteamEmu").toBool(false)) {
            ctx.exePath = ctx.versionPath + "/" + steamEmulator;
            GD_DEBUG_LOG("launch", QString("useSteamEmu is on; launching emulator instead of the game exe directly: %1").arg(steamEmulator));
        } else {
            ctx.exePath = ctx.versionPath + "/" + executable;
        }
    }

    GD_DEBUG_LOG("launch", QString("Resolved executable: %1").arg(ctx.exePath));
    if (!QFileInfo::exists(ctx.exePath)) {
        GD_DEBUG_LOG("launch", "Executable does not exist");
        m_launching = false;
        emit launchComplete();
        emit statusUpdate(data.value("useSteamEmu").toBool(false) && !isCustomExe
            ? QString("Steam emulator executable not found: %1").arg(QFileInfo(ctx.exePath).fileName())
            : "Game executable not found");
        return;
    }

    ctx.localAppDataPath = linuxAppDataPath(data.value("saveFolderName").toString());
    ctx.infoJsonPath = Settings::baseDir() + "/info.json";

    const PrepResult prep = prepareLocalAppData(ctx.localAppDataPath, ctx.infoJsonPath);
    if (!prep.success) {
        m_launching = false;
        emit launchComplete();
        emit statusUpdate(prep.error.isEmpty() ? "Failed to prepare save data" : prep.error);
        return;
    }

    InstanceManager::ensureInstanceIntegrity(instancePath, data.value("isGeodeCompatible").toBool(),
                                              data.value("useMegaHack").toBool());

    ctx.managedItems = InstanceManager::getManagedItems(data.value("isGeodeCompatible").toBool(),
                                                          data.value("useMegaHack").toBool());
    GD_DEBUG_LOG("transfer", QString("Copying instance -> game save folder (%1): %2")
        .arg(ctx.localAppDataPath, ctx.managedItems.join(", ")));
    InstanceManager::transferManagedItems(instancePath, ctx.localAppDataPath, ctx.managedItems, false,
        [this, instanceName](const QString &file, int current) {
            emit statusUpdate(QString("Preparing instance %1 (%2) — %3 files").arg(instanceName, file).arg(current));
            // Logged every 25 files instead of per-file -- geode/mods or large custom-level
            // folders can easily be thousands of files, which would otherwise flood the console.
            if (current % 25 == 0) GD_DEBUG_LOG("transfer", QString("  ...%1 files copied so far (%2)").arg(current).arg(file));
        });
    GD_DEBUG_LOG("transfer", "Instance prep transfer complete.");

    QJsonObject infoJson; infoJson["instanceName"] = instanceName;
    QFile infoOut(ctx.infoJsonPath);
    infoOut.open(QIODevice::WriteOnly | QIODevice::Truncate);
    infoOut.write(QJsonDocument(infoJson).toJson(QJsonDocument::Indented));
    infoOut.close();

    emit statusUpdate(QString("Launching %1...").arg(instanceName));
    GD_DEBUG_LOG("launch", QString("Spawning process: %1 (cwd=%2)").arg(ctx.exePath, ctx.versionPath));

    m_gameProcess = new QProcess(this);
    m_gameProcess->setWorkingDirectory(ctx.versionPath);
    if (kIsLinux) {
        m_gameProcess->start("wine", {ctx.exePath});
    } else {
        m_gameProcess->setProgram(ctx.exePath);
        m_gameProcess->start();
    }

    m_launching = false;
    m_gameRunning = true;
    emit gameStarted();
    emit logStatusChanged(true);

    const AppSettings settings = Settings::load();
    ctx.syncDelay = qMax(0, settings.syncDelay) * 1000;
    ctx.enableLogOutput = settings.enableLogOutput;
    ctx.processName = !ctx.gameProcessName.isEmpty() ? ctx.gameProcessName : QFileInfo(ctx.exePath).fileName();

    deployGeodeLogModIfNeeded(ctx);

    m_logBuffer.clear();
    if (ctx.enableLogOutput) {
        connect(m_gameProcess, &QProcess::readyReadStandardOutput, this, [this]() {
            const auto lines = QString::fromLocal8Bit(m_gameProcess->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
            for (const QString &l : lines) appendLog(QString("[%1] [stdout] %2").arg(QTime::currentTime().toString(), l));
        });
        connect(m_gameProcess, &QProcess::readyReadStandardError, this, [this]() {
            const auto lines = QString::fromLocal8Bit(m_gameProcess->readAllStandardError()).split('\n', Qt::SkipEmptyParts);
            for (const QString &l : lines) appendLog(QString("[%1] [stderr] %2").arg(QTime::currentTime().toString(), l));
        });
    }

    beginMonitor(ctx);
}

void GameLauncher::beginMonitor(LaunchContext ctx) {
    GD_DEBUG_LOG("launch", QString("Watching process '%1' every 2s").arg(ctx.processName));
    m_monitorTimer = new QTimer(this);
    m_monitorTimer->setInterval(2000);
    connect(m_monitorTimer, &QTimer::timeout, this, [this, ctx]() mutable {
        if (!checkProcessRunning(ctx.processName)) {
            GD_DEBUG_LOG("launch", QString("Process '%1' no longer running").arg(ctx.processName));
            m_monitorTimer->stop();
            m_monitorTimer->deleteLater();
            m_monitorTimer = nullptr;
            watchForExit(ctx);
        }
    });
    m_monitorTimer->start();
}

void GameLauncher::watchForExit(LaunchContext ctx) {
    const bool skipRestart = ctx.data.value("skipRestartCheck").toBool(false) || ctx.syncDelay == 0;
    if (skipRestart) {
        GD_DEBUG_LOG("launch", "Restart check skipped (disabled or 0s delay); syncing now.");
        m_gameRunning = false;
        emit gameStopped();
        emit logStatusChanged(false);
        postLaunchCleanup(ctx);
        return;
    }

    GD_DEBUG_LOG("launch", QString("Waiting %1s to see if the process restarts (Geode reload, etc.) before syncing").arg(ctx.syncDelay / 1000));
    emit statusUpdate(QString("Process ended. Waiting %1s for restart check...").arg(ctx.syncDelay / 1000));

    QTimer::singleShot(ctx.syncDelay, this, [this, ctx]() mutable {
        const bool restarted = checkProcessRunning(ctx.processName);
        if (!restarted) {
            GD_DEBUG_LOG("launch", "No restart detected; proceeding to sync.");
            m_gameRunning = false;
            emit gameStopped();
            emit logStatusChanged(false);
            postLaunchCleanup(ctx);
            return;
        }

        GD_DEBUG_LOG("launch", "Process restarted; resuming watch instead of syncing.");
        if (ctx.enableLogOutput) {
            appendLog(QString("[%1] [info] Game restarted — logging of new process output is not supported by the launcher")
                          .arg(QTime::currentTime().toString()));
        }
        emit statusUpdate("Game restarted, watching...");
        beginMonitor(ctx);
    });
}

void GameLauncher::postLaunchCleanup(LaunchContext ctx) {
    m_syncing = true;
    emit statusUpdate(QString("Syncing data for %1...").arg(ctx.instanceName));
    GD_DEBUG_LOG("transfer", QString("Syncing game save folder -> instance '%1': %2")
        .arg(ctx.instanceName, ctx.managedItems.join(", ")));

    const QString instancePath = Settings::instancesDir() + "/" + ctx.instanceName;
    InstanceManager::transferManagedItems(ctx.localAppDataPath, instancePath, ctx.managedItems, true,
        [this, ctx](const QString &file, int current) {
            emit statusUpdate(QString("Syncing %1 (%2) — %3 files").arg(ctx.instanceName, file).arg(current));
            if (current % 25 == 0) GD_DEBUG_LOG("transfer", QString("  ...%1 files synced so far (%2)").arg(current).arg(file));
        });
    GD_DEBUG_LOG("transfer", "Sync-back complete.");

    if (QFileInfo::exists(ctx.infoJsonPath)) QFile::remove(ctx.infoJsonPath);

    if (ctx.data.value("enableGeodeLogging").toBool(false)) {
        QString modPath = ctx.versionPath + "/geode/mods/pcpapc172.gdlauncher-log.geode";
        if (QFileInfo::exists(modPath)) {
            GD_DEBUG_LOG("launch", "Removing deployed Geode log mod");
            QFile::remove(modPath);
        }
    }

    m_syncing = false;

    const AppSettings settings = Settings::load();
    GD_DEBUG_LOG("launch", QString("Launch/sync cycle finished for '%1'").arg(ctx.instanceName));
    if (settings.closeBehavior == "Close After Game Ends") {
        GD_DEBUG_LOG("launch", "close_behavior=Close After Game Ends; quitting.");
        qApp->quit();
    } else {
        emit launchComplete();
        emit statusUpdate("Ready");
    }
}

void GameLauncher::deployGeodeLogModIfNeeded(const LaunchContext &ctx) {
    if (!(ctx.data.value("enableGeodeLogging").toBool(false) &&
          ctx.data.value("isGeodeCompatible").toBool(false) &&
          !ctx.data.value("geodeLogModVersion").toString().isEmpty()))
        return;

    const QString tag = ctx.data.value("geodeLogModVersion").toString();
    const QByteArray releaseJson = httpGetSync(
        QUrl(QString("https://api.github.com/repos/pcpapc172/gdlauncher-log/releases/tags/%1").arg(tag)));
    if (releaseJson.isEmpty()) return;

    const QJsonObject release = QJsonDocument::fromJson(releaseJson).object();
    QString assetUrl;
    for (const QJsonValue &av : release.value("assets").toArray()) {
        const QJsonObject asset = av.toObject();
        if (asset.value("name").toString().endsWith(".geode")) {
            assetUrl = asset.value("browser_download_url").toString();
            break;
        }
    }
    if (assetUrl.isEmpty()) return;

    const QString geodeModsDir = ctx.versionPath + "/geode/mods";
    QDir().mkpath(geodeModsDir);
    const QString destPath = geodeModsDir + "/pcpapc172.gdlauncher-log.geode";
    if (QFileInfo::exists(destPath)) return;

    const QByteArray modData = httpGetSync(QUrl(assetUrl), 20000);
    if (modData.isEmpty()) return;

    QFile out(destPath);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(modData);
        emit statusUpdate("Deployed geode log mod");
    }
}

void GameLauncher::terminateGame() {
    if (m_gameProcess) {
        if (kIsLinux) {
            QProcess::execute("pkill", {"-9", "-f", "GeometryDash.exe"});
        } else {
            m_gameProcess->kill();
        }
    }
    if (m_monitorTimer) { m_monitorTimer->stop(); m_monitorTimer->deleteLater(); m_monitorTimer = nullptr; }
    m_gameRunning = false;
    if (m_gameProcess) { m_gameProcess->deleteLater(); m_gameProcess = nullptr; }
    emit gameStopped();
    emit logStatusChanged(false);
    emit statusUpdate("Game terminated");
}
