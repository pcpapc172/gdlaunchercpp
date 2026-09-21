#pragma once
#include <QMainWindow>
#include <QVector>
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
class QPlainTextEdit;
class QDialog;

// Port of index.html + renderer.js's main window.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

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

private:
    QTableWidget *m_table;
    QPushButton *m_launchBtn;
    QPushButton *m_createBtn;
    QPushButton *m_editBtn;
    QPushButton *m_deleteBtn;
    QPushButton *m_downloadBtn;
    QPushButton *m_editorBtn;
    QPushButton *m_settingsBtn;
    AnimatedProgressBar *m_progressBar;
    QLabel *m_statusLabel;

    GameLauncher *m_launcher;
    UpdateChecker *m_updateChecker;
    VersionManager *m_versionManager;
    ConsoleWindow *m_consoleWindow = nullptr;
    LogPipeServer *m_logPipe;

    QVector<InstanceInfo> m_instances;
    QString m_selectedInstance;
    AppSettings m_settings;
    QString m_appVersion;
    QStringList m_logBuffer;

    void setUiEnabled(bool enabled);
    void appendLog(const QString &line);
    QString selectedInstanceName() const;
    void runStartupChecks();
};
