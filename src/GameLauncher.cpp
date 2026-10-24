#include "GameLauncher.h"
#include "Settings.h"
#include "InstanceManager.h"
#include "DebugLog.h"
#include "ProtonManager.h"
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
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QDirIterator>
#include <QElapsedTimer>

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
    : QObject(parent), m_dialogParent(dialogParent) {
    if (kIsLinux) {
        m_protonManager = new ProtonManager(this);
        connect(m_protonManager, &ProtonManager::statusUpdate, this, &GameLauncher::statusUpdate);
        connect(m_protonManager, &ProtonManager::progress, this, &GameLauncher::launchProgress);
        connect(m_protonManager, &ProtonManager::finished, this, [this](bool success, const QString &error) {
            m_launching = false;
            if (!success) {
                emit launchComplete();
                emit statusUpdate("GE-Proton setup failed: " + error);
                QMessageBox::warning(m_dialogParent, "GE-Proton Setup Failed", error);
                return;
            }
            m_protonReady = true;
            if (!m_pendingTool.isEmpty()) { m_launching = true; startTool(); }
            else launchInstance(m_pendingInstance);
        });
    }
}

GameLauncher::~GameLauncher() { m_transferFuture.waitForFinished(); }

void GameLauncher::runExecutable(const QString &executable, bool geodeCompatible) {
    if (!kIsLinux || m_launching || m_gameRunning || m_syncing) return;
    m_launching = true;
    m_pendingTool = executable;
    m_toolGeode = geodeCompatible;
    if (Settings::load().useProtonGE && !m_protonReady) m_protonManager->ensureReady();
    else startTool();
}

void GameLauncher::startTool() {
    const QString executable = m_pendingTool;
    m_pendingTool.clear();
    const bool proton = Settings::load().useProtonGE;
    auto env = proton ? ProtonManager::environment() : QProcessEnvironment::systemEnvironment();
    if (m_toolGeode) env.insert("WINEDLLOVERRIDES", "xinput1_4=n,b");
    auto *process = new QProcess(this);
    m_toolProcess = process;
    process->setProcessEnvironment(env);
    if (QFileInfo(executable).isAbsolute()) process->setWorkingDirectory(QFileInfo(executable).absolutePath());
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        appendLog(QString::fromLocal8Bit(process->readAllStandardOutput()));
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process]() {
        appendLog(QString::fromLocal8Bit(process->readAllStandardError()));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_toolProcess = nullptr;
        m_launching = false;
        emit launchComplete();
        emit statusUpdate("Could not launch executable: " + process->errorString());
        process->deleteLater();
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process](int code, QProcess::ExitStatus status) {
        m_toolProcess = nullptr;
        m_launching = false;
        emit launchComplete();
        emit statusUpdate(code == 0 && status == QProcess::NormalExit ? "Ready" : "Executable exited with an error. See the log for details.");
        process->deleteLater();
    });
    emit launchProgress(-1);
    emit statusUpdate("Running " + QFileInfo(executable).fileName() + (proton ? " with UMULauncher..." : " with Wine..."));
    if (proton) process->start("python3", {ProtonManager::launcherPath(), executable});
    else process->start("wine", {executable});
}

void GameLauncher::transferAsync(const QString &src, const QString &dest, const QStringList &items,
                                bool move, const QString &label, const std::function<void()> &done) {
    emit launchProgress(0);
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, done, label]() {
        const bool success = watcher->result();
        watcher->deleteLater();
        if (success) { done(); return; }
        m_launching = false;
        m_syncing = false;
        emit launchComplete();
        emit statusUpdate(label + " failed. Existing recovery markers have been kept.");
        QMessageBox::warning(m_dialogParent, "Save Transfer Failed",
            "Could not transfer save data. Check disk space and permissions, then retry. Existing recovery markers have been kept.");
    });
    m_transferFuture = QtConcurrent::run([this, src, dest, items, move, label]() {
        int total = 0;
        for (const auto &item : items) {
            const QString path = src + "/" + item;
            if (QFileInfo(path).isFile()) ++total;
            else {
                QDirIterator files(path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                while (files.hasNext()) { files.next(); ++total; }
            }
        }
        QElapsedTimer throttle;
        throttle.start();
        return InstanceManager::transferManagedItems(src, dest, items, move,
            [this, total, label, &throttle](const QString &file, int current) {
                // Keep UI updates live without flooding its event queue for tiny files.
                if (current != 1 && current != total && throttle.elapsed() < 33) return;
                throttle.restart();
                emit statusUpdate(QString("%1 (%2) — %3 / %4 files").arg(label, file).arg(current).arg(total));
                emit launchProgress(total ? current * 100 / total : 100);
            });
    });
    watcher->setFuture(m_transferFuture);
}

QString GameLauncher::linuxAppDataPath(const QString &saveFolderName, bool useProton) const {
    return Settings::gameSaveDir(saveFolderName, useProton);
}

QString GameLauncher::localMarkerPath(const QString &localAppDataPath) {
    return localAppDataPath + "/.gdlauncher-instance.json";
}

void GameLauncher::writeLocalMarker(const QString &localAppDataPath, const QString &instanceName,
                                     const QJsonObject &data) {
    QJsonObject marker;
    marker["instanceName"] = instanceName;
    marker["saveFolderName"] = data.value("saveFolderName").toString();
    marker["isGeodeCompatible"] = data.value("isGeodeCompatible").toBool(false);
    marker["useMegaHack"] = data.value("useMegaHack").toBool(false);
    marker["launchedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QFile f(localMarkerPath(localAppDataPath));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(marker).toJson(QJsonDocument::Indented));
        f.close();
    }
}

void GameLauncher::removeLocalMarker(const QString &localAppDataPath) {
    QFile::remove(localMarkerPath(localAppDataPath));
}

GameLauncher::PrepResult GameLauncher::prepareLocalAppData(const QString &localPath, const QString &infoPath, bool isTour) {
    PrepResult res;
    GD_DEBUG_LOG("transfer", QString("Scanning %1 for unmanaged/leftover save data").arg(localPath));
    const QStringList allItems = InstanceManager::getAllManagedItems();
    QStringList foundInAppData;
    for (const QString &item : allItems) {
        if (QFileInfo::exists(localPath + "/" + item)) foundInAppData << item;
    }

    if (foundInAppData.isEmpty() && !QFileInfo::exists(infoPath) && !QFileInfo::exists(localMarkerPath(localPath))) {
        GD_DEBUG_LOG("transfer", "Nothing found there; skipping.");
        removeLocalMarker(localPath); // stale, since there's nothing left for it to describe
        res.success = true; res.found = false; return res;
    }
    GD_DEBUG_LOG("transfer", QString("Found: %1").arg(foundInAppData.join(", ")));

    // The primary marker (launcher's own AppData dir) always wins when present. Otherwise, if
    // it's missing but the redundant one written directly into this save folder is still here,
    // it means the launcher's own data dir was lost/wiped/corrupted between the crash and now
    // -- fall back to it rather than dumping the user straight into the "unmanaged data found"
    // prompt for what's actually a perfectly identifiable previous instance.
    const bool hasPrimary = QFileInfo::exists(infoPath);
    const QString localMarker = localMarkerPath(localPath);
    const bool hasLocalOnly = !hasPrimary && QFileInfo::exists(localMarker);

    if (hasPrimary || hasLocalOnly) {
        if (isTour) { res.success = true; res.found = false; return res; }
        const QString sourcePath = hasPrimary ? infoPath : localMarker;
        QFile f(sourcePath);
        if (!f.open(QIODevice::ReadOnly)) { res.success = false; res.error = "Failed to read recovery marker"; return res; }
        QJsonObject info = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        if (hasLocalOnly) {
            GD_DEBUG_LOG("transfer", QString("Primary recovery marker missing; falling back to the one in %1").arg(localPath));
        }
        const QString targetDir = Settings::instancesDir() + "/" + info.value("instanceName").toString();
        if (!QFileInfo::exists(targetDir + "/instance.json")) {
            GD_DEBUG_LOG("transfer", QString("Stale recovery marker pointed at missing instance '%1'; discarding.")
                .arg(info.value("instanceName").toString()));
            if (hasPrimary) QFile::remove(infoPath);
            removeLocalMarker(localPath);
            res.success = true;
            return res;
        }
        // The local marker carries the managed-file flags itself; the primary
        // marker gets those flags from instance.json and records the live save path.
        QJsonObject prevData = info;
        if (hasPrimary) {
            QFile pf(targetDir + "/instance.json");
            pf.open(QIODevice::ReadOnly);
            prevData = QJsonDocument::fromJson(pf.readAll()).object();
            pf.close();
        }
        GD_DEBUG_LOG("transfer", QString("Recovering leftover data back into instance '%1' (previous run likely crashed before syncing)")
            .arg(info.value("instanceName").toString()));
        const QString recoveryPath = hasPrimary
            ? info.value("localAppDataPath").toString(linuxAppDataPath(prevData.value("saveFolderName").toString()))
            : localPath;
        const bool recovered = InstanceManager::transferManagedItems(
            recoveryPath, targetDir,
            InstanceManager::getManagedItems(prevData.value("isGeodeCompatible").toBool(),
                                              prevData.value("useMegaHack").toBool()),
            true);
        if (!recovered) { res.error = "Could not recover save data. Check disk space and permissions before retrying."; return res; }
        if (hasPrimary) QFile::remove(infoPath);
        removeLocalMarker(recoveryPath);
        GD_DEBUG_LOG("transfer", "Recovery transfer complete.");
        // The previous run may have used the other backend's prefix. Check this
        // launch's save folder too before copying over any unrelated saves there.
        res = prepareLocalAppData(localPath, infoPath, isTour);
        res.found = true;
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
        if (!InstanceManager::transferManagedItems(localPath, newInstancePath, foundInAppData, true)) {
            res.error = "Could not import save data. Check disk space and permissions.";
            return res;
        }
        GD_DEBUG_LOG("transfer", "Import complete.");
        res.success = true; res.found = true;
    } else if (box.clickedButton() == trashBtn) {
        const QString trashFolder = Settings::trashDir() + "/UnmanagedData_" +
                                     QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH-mm-ss-zzz");
        GD_DEBUG_LOG("transfer", QString("Moving unmanaged data to trash: %1").arg(trashFolder));
        QDir().mkpath(trashFolder);
        if (!InstanceManager::transferManagedItems(localPath, trashFolder, foundInAppData, true)) {
            res.error = "Could not move unmanaged data to trash.";
            return res;
        }
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
    if (m_launching) return;
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

    const bool useProton = kIsLinux && Settings::load().useProtonGE;
    m_pendingTool.clear();
    if (useProton && !m_protonReady) {
        m_pendingInstance = instanceName;
        m_protonManager->ensureReady();
        return;
    }

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

    ctx.localAppDataPath = linuxAppDataPath(data.value("saveFolderName").toString(), useProton);
    ctx.infoJsonPath = Settings::baseDir() + "/info.json";

    const PrepResult prep = prepareLocalAppData(ctx.localAppDataPath, ctx.infoJsonPath);
    if (!prep.success) {
        m_launching = false;
        emit launchComplete();
        emit statusUpdate(prep.error.isEmpty() ? "Failed to prepare save data" : prep.error);
        return;
    }

    if (!QDir().mkpath(ctx.localAppDataPath)) {
        m_launching = false;
        emit launchComplete();
        emit statusUpdate("Launch failed: could not create the game save directory");
        return;
    }

    InstanceManager::ensureInstanceIntegrity(instancePath, data.value("isGeodeCompatible").toBool(),
                                              data.value("useMegaHack").toBool());

    ctx.managedItems = InstanceManager::getManagedItems(data.value("isGeodeCompatible").toBool(),
                                                          data.value("useMegaHack").toBool());
    GD_DEBUG_LOG("transfer", QString("Copying instance -> game save folder (%1): %2")
        .arg(ctx.localAppDataPath, ctx.managedItems.join(", ")));
    transferAsync(instancePath, ctx.localAppDataPath, ctx.managedItems, false,
                  "Preparing " + instanceName, [this, ctx, useProton]() { startPrepared(ctx, useProton); });
}

void GameLauncher::startPrepared(LaunchContext ctx, bool useProton) {
    const QString instanceName = ctx.instanceName;
    const QJsonObject data = ctx.data;
    QJsonObject infoJson; infoJson["instanceName"] = instanceName;
    infoJson["localAppDataPath"] = ctx.localAppDataPath;
    QFile infoOut(ctx.infoJsonPath);
    infoOut.open(QIODevice::WriteOnly | QIODevice::Truncate);
    infoOut.write(QJsonDocument(infoJson).toJson(QJsonDocument::Indented));
    infoOut.close();
    writeLocalMarker(ctx.localAppDataPath, instanceName, data);
    GD_DEBUG_LOG("transfer", QString("Wrote recovery marker into %1").arg(ctx.localAppDataPath));

    emit statusUpdate(QString("Launching %1...").arg(instanceName));
    GD_DEBUG_LOG("launch", QString("Spawning process: %1 (cwd=%2)").arg(ctx.exePath, ctx.versionPath));

    m_processTracker.reset();
    m_gameProcess = new QProcess(this);
    m_processTracker.configureProcess(m_gameProcess);
    m_gameProcess->setWorkingDirectory(ctx.versionPath);
    if (kIsLinux) {
        QProcessEnvironment env = useProton ? ProtonManager::environment() : QProcessEnvironment::systemEnvironment();
        if (data.value("isGeodeCompatible").toBool(false)) {
            env.insert("WINEDLLOVERRIDES", "xinput1_4=n,b");
        }
        env.insert("GDLAUNCHER_SESSION", m_processTracker.token());
        m_gameProcess->setProcessEnvironment(env);
        if (useProton) m_gameProcess->start("python3", {ProtonManager::launcherPath(), ctx.exePath});
        else m_gameProcess->start("wine", {ctx.exePath});
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
        connect(m_gameProcess, &QProcess::readyReadStandardOutput, this, [this, process = m_gameProcess]() {
            const auto lines = QString::fromLocal8Bit(process->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
            for (const QString &l : lines) appendLog(QString("[%1] [stdout] %2").arg(QTime::currentTime().toString(), l));
        });
        connect(m_gameProcess, &QProcess::readyReadStandardError, this, [this, process = m_gameProcess]() {
            const auto lines = QString::fromLocal8Bit(process->readAllStandardError()).split('\n', Qt::SkipEmptyParts);
            for (const QString &l : lines) appendLog(QString("[%1] [stderr] %2").arg(QTime::currentTime().toString(), l));
        });
    }

    beginMonitor(ctx);
}

void GameLauncher::beginMonitor(LaunchContext ctx) {
    GD_DEBUG_LOG("launch", QString("Watching launched process '%1'").arg(ctx.processName));
    m_monitorTimer = new QTimer(this);
    m_monitorTimer->setInterval(250);
    connect(m_monitorTimer, &QTimer::timeout, this, [this, ctx, seenGame = false]() mutable {
        const bool running = m_processTracker.isRunning(ctx.processName);
        if (running) seenGame = true;
        // UMU may need time to enter the runtime before the Windows process appears.
        if (!seenGame && m_gameProcess && m_gameProcess->state() != QProcess::NotRunning) return;
        if (!running) {
            GD_DEBUG_LOG("launch", QString("Process '%1' no longer running").arg(ctx.processName));
            m_monitorTimer->stop();
            m_monitorTimer->deleteLater();
            m_monitorTimer = nullptr;
            if (!seenGame) {
                const QString error = m_gameProcess && m_gameProcess->error() == QProcess::FailedToStart
                    ? m_gameProcess->errorString() : "The game process did not start. Enable Log Output for details.";
                m_gameRunning = false;
                emit gameStopped();
                emit logStatusChanged(false);
                ctx.launchError = "Launch failed: " + error;
                postLaunchCleanup(ctx);
                return;
            }
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
        const bool restarted = m_processTracker.isRunning(ctx.processName);
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
    transferAsync(ctx.localAppDataPath, instancePath, ctx.managedItems, true,
                  "Syncing " + ctx.instanceName, [this, ctx]() { finishCleanup(ctx); });
}

void GameLauncher::finishCleanup(LaunchContext ctx) {
    if (m_gameProcess) {
        // UMU can still be finishing runtime cleanup after the Windows process
        // exits. Let the wrapper finish instead of killing it during sync-back.
        if (m_gameProcess->state() == QProcess::NotRunning) m_gameProcess->deleteLater();
        else connect(m_gameProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                     m_gameProcess, &QObject::deleteLater);
        m_gameProcess = nullptr;
    }

    if (QFileInfo::exists(ctx.infoJsonPath)) QFile::remove(ctx.infoJsonPath);
    removeLocalMarker(ctx.localAppDataPath);

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
    if (ctx.launchError.isEmpty() && settings.closeBehavior == "Close After Game Ends") {
        GD_DEBUG_LOG("launch", "close_behavior=Close After Game Ends; quitting.");
        qApp->quit();
    } else {
        emit launchComplete();
        emit statusUpdate(ctx.launchError.isEmpty() ? "Ready" : ctx.launchError);
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
    m_processTracker.terminate();
    if (m_gameProcess) m_gameProcess->kill();
    // Keep monitoring so the normal exit path syncs this instance's saves back.
    emit statusUpdate("Stopping the launched game...");
}
