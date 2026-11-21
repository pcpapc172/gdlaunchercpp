#pragma once
#include <QMainWindow>
#include <QVector>
#include <QMap>
#include <QFuture>
#include <memory>
#include "InstanceUpdater.h"
#include "InstanceManager.h"
#include "Settings.h"
#include "VersionManager.h"

class QTableWidget;
class QPushButton;
class QLabel;
class AnimatedProgressBar;
class GameLauncher;
class UpdateChecker;
class ConsoleWindow;
class LogPipeServer;
class NongManager;
class QPlainTextEdit;
class QDialog;
class QCloseEvent;

// Port of index.html + renderer.js's main window.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    void instanceUpdateListChanged();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void refreshInstances();
    void onSelectionChanged();
    void onLaunch();
    void onCreate();
    void onEdit();
    void onDelete();
    void onSettings();
    void onDownloads();
    void onEditor();
    void onNong();

private:
    QTableWidget *m_table;
    QPushButton *m_launchBtn;
    QPushButton *m_updatesBtn;
    QLabel *m_updatesBadge;
    QPushButton *m_cancelUpdate;
    QPushButton *m_createBtn;
    QPushButton *m_editBtn;
    QPushButton *m_deleteBtn;
    QPushButton *m_downloadBtn;
    QPushButton *m_editorBtn;
    QPushButton *m_nongBtn;
    QPushButton *m_settingsBtn;
    QPushButton *m_runExeBtn = nullptr;
    QPushButton *m_explorerBtn = nullptr;
    AnimatedProgressBar *m_progressBar;
    QLabel *m_statusLabel;

    GameLauncher *m_launcher;
    UpdateChecker *m_updateChecker;
    VersionManager *m_versionManager;
    NongManager *m_nongManager;
    ConsoleWindow *m_consoleWindow = nullptr;
    LogPipeServer *m_logPipe;

    QVector<InstanceInfo> m_instances;
    QString m_selectedInstance;
    AppSettings m_settings;
    QString m_appVersion;
    QStringList m_logBuffer;
    bool m_closePromptOpen = false;

    QMap<QString, InstanceUpdater::Result> m_instanceUpdates;
    std::shared_ptr<std::atomic_bool> m_updateCancel;
    QFuture<InstanceUpdater::Result> m_updateFuture;
    std::shared_ptr<std::atomic_bool> m_scanCancel = std::make_shared<std::atomic_bool>(false);
    bool m_scanningUpdates = false;
    unsigned m_updateRevision = 0;

    void scanInstanceUpdates();
    void showInstanceUpdates();
    void updateBadge();
    void startInstanceUpdate(const InstanceInfo &instance, bool thenLaunch);
    void launchSelected(const QString &name);
    void setUiEnabled(bool enabled);
    void appendLog(const QString &line);
    QString selectedInstanceName() const;
    void runStartupChecks();
    void runLinuxTool(bool explorer);
    QString closeBlockReason() const;
};
