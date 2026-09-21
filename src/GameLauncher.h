#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <QWidget>

// Port of launchGame/terminateGame/prepareLocalAppData/etc. from main.js.
// On Linux the game runs under Wine; on Windows it runs natively.
class GameLauncher : public QObject {
    Q_OBJECT
public:
    explicit GameLauncher(QWidget *dialogParent, QObject *parent = nullptr);

    bool isGameRunning() const { return m_gameRunning; }
    bool isSyncing() const { return m_syncing; }

    // Kicks off the (mostly synchronous, some async via timers) launch sequence.
    void launchInstance(const QString &instanceName);
    void terminateGame();

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

private:
    QWidget *m_dialogParent;
    bool m_gameRunning = false;
    bool m_syncing = false;
    QProcess *m_gameProcess = nullptr;
    QTimer *m_monitorTimer = nullptr;
    QStringList m_logBuffer;

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
    };

    QString linuxAppDataPath(const QString &saveFolderName) const;
    // Returns {success, found, error}
    struct PrepResult { bool success = false; bool found = false; QString error; };
    PrepResult prepareLocalAppData(const QString &localPath, const QString &infoPath, bool isTour = false);

    bool checkProcessRunning(const QString &processName) const;
    void beginMonitor(LaunchContext ctx);
    void watchForExit(LaunchContext ctx);
    void postLaunchCleanup(LaunchContext ctx);
    void deployGeodeLogModIfNeeded(const LaunchContext &ctx);
    void appendLog(const QString &line);
};
