#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <QWidget>
#include "ProcessTracker.h"
#include <functional>
#include <QFuture>
#include "BackupManager.h"

class ProtonManager;

// Port of launchGame/terminateGame/prepareLocalAppData/etc. from main.js.
// On Linux the game runs under Wine or GE-Proton/UMU; on Windows it runs natively.
class GameLauncher : public QObject {
    Q_OBJECT
public:
    explicit GameLauncher(QWidget *dialogParent, QObject *parent = nullptr);
    ~GameLauncher() override;

    bool isGameRunning() const { return m_gameRunning; }
    bool isSyncing() const { return m_syncing; }
    bool isLaunching() const { return m_launching; }
    bool isBackingUp() const { return m_backingUp; }
    bool isToolRunning() const { return m_toolProcess != nullptr; }

    // Kicks off the (mostly synchronous, some async via timers) launch sequence.
    void launchInstance(const QString &instanceName);
    void terminateGame();
    void runExecutable(const QString &executable, bool geodeCompatible = false);

    // Used on first run to offer importing pre-existing local save data.
    // Returns true if unmanaged files were found (and handled).
    bool handleFirstRunImport();

signals:
    void statusUpdate(const QString &message);
    void launchComplete();     // UI should re-enable buttons
    void gameStarted();
    void gameStopped();
    void logLine(const QString &line);
    void logStatusChanged(bool running);
    void launchProgress(int percentage);
    void closeRequested();

private:
    QWidget *m_dialogParent;
    bool m_gameRunning = false;
    bool m_syncing = false;
    bool m_launching = false;
    bool m_backingUp = false;
    QProcess *m_gameProcess = nullptr;
    QProcess *m_toolProcess = nullptr;
    QTimer *m_monitorTimer = nullptr;
    QStringList m_logBuffer;
    ProtonManager *m_protonManager = nullptr;
    bool m_protonReady = false;
    QString m_pendingInstance;
    QString m_pendingTool;
    bool m_toolGeode = false;
    ProcessTracker m_processTracker;
    QFuture<bool> m_transferFuture;
    QFuture<BackupResult> m_backupFuture;

    struct LaunchContext {
        QString instanceName;
        QJsonObject data;
        QString versionPath;
        QString exePath;
        QString localAppDataPath;
        QString infoJsonPath;
        QStringList managedItems;
        int syncDelay = 5000;
        bool enableLogOutput = false;
        QString processName;
        // The real Geometry Dash executable's filename. Usually equal to exePath's filename,
        // except when useSteamEmu is on: exePath is then the emulator we actually spawn, but
        // restart-detection/monitoring still needs to watch for the game process itself
        // (SmartSteamEmu is expected to launch it, not stay resident under its own name).
        QString gameProcessName;
        QString launchError;
    };

    QString linuxAppDataPath(const QString &saveFolderName, bool useProton = false) const;
    // Returns {success, found, error}
    struct PrepResult { bool success = false; bool found = false; QString error; };
    PrepResult prepareLocalAppData(const QString &localPath, const QString &infoPath, bool isTour = false);

    // A second, redundant recovery marker written directly into the live save folder itself
    // (never treated as managed save data -- excluded from getAllManagedItems() so it's never
    // copied or swept by transferManagedItems), in case the primary one in the launcher's own
    // AppData dir is ever lost. Carries enough (instanceName + the flags that decide which
    // items are "managed") to recover without needing to reread the instance's own
    // instance.json, which may itself be the thing that's gone missing.
    static QString localMarkerPath(const QString &localAppDataPath);
    static void writeLocalMarker(const QString &localAppDataPath, const QString &instanceName,
                                  const QJsonObject &data);
    static void removeLocalMarker(const QString &localAppDataPath);

    void beginMonitor(LaunchContext ctx);
    void startPrepared(LaunchContext ctx, bool useProton);
    void backupBeforeLaunch(LaunchContext ctx, bool useProton);
    void finishCleanup(LaunchContext ctx);
    void transferAsync(const QString &src, const QString &dest, const QStringList &items,
                       bool move, const QString &label, const std::function<void()> &done);
    void watchForExit(LaunchContext ctx);
    void postLaunchCleanup(LaunchContext ctx);
    void deployGeodeLogModIfNeeded(const LaunchContext &ctx);
    void appendLog(const QString &line);
    void startTool();
};
