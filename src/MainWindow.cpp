#include "MainWindow.h"
#include "GameLauncher.h"
#include "UpdateChecker.h"
#include "ConsoleWindow.h"
#include "LogPipeServer.h"
#include "InstanceManager.h"
#include "ui/InstanceDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/DownloadsDialog.h"
#include "ui/SaveEditorDialog.h"
#include "ui/WelcomeDialog.h"
#include "ui/ChangelogDialog.h"
#include "ui/Theme.h"
#include "ui/Animations.h"
#include "DebugLog.h"

#include <QWidget>
#include <QCloseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QCoreApplication>
#include <QTimer>
#include <QJsonDocument>
#include <QStyle>
#include <QApplication>
#include <QIcon>

static QString formatSize(qint64 bytes) {
    if (bytes < 0) return "…";
    double b = static_cast<double>(bytes);
    static const char *units[] = {"B", "KB", "MB", "GB"};
    int i = 0;
    while (b >= 1024.0 && i < 3) { b /= 1024.0; i++; }
    return QString::number(b, 'f', 2) + " " + units[i];
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(kDebugLoggingEnabled ? "GDLauncher (Debug)" : "GDLauncher");
    setWindowIcon(QIcon(":/icon.png"));
    resize(1000, 750);
    setMinimumSize(700, 500);

    Settings::ensureDirs();
    m_settings = Settings::load();
    m_appVersion = QCoreApplication::applicationVersion();

    QStyle *style = QApplication::style();

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin);
    root->setSpacing(UiMetrics::kSpacing);

    auto *heading = new QLabel("Instances", this);
    heading->setObjectName("heading");
    root->addWidget(heading);

    auto *mainRow = new QHBoxLayout();
    mainRow->setSpacing(UiMetrics::kSpacing);

    m_table = new QTableWidget(0, 3, this);
    m_table->setObjectName("panel");
    m_table->setHorizontalHeaderLabels({"Name", "Save Size", "Version"});
    // Give all three columns equal width instead of Name/Size auto-fitting to their tiny
    // content and Version (the only usually-long column) eating the rest via stretch-last.
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    mainRow->addWidget(m_table, 1);

    auto *btnCol = new QVBoxLayout();
    btnCol->setSpacing(UiMetrics::kTightSpacing);
    m_launchBtn = new QPushButton(style->standardIcon(QStyle::SP_MediaPlay), "Launch", this);
    m_launchBtn->setObjectName("primary");
    m_createBtn = new QPushButton(style->standardIcon(QStyle::SP_FileDialogNewFolder), "Create", this);
    m_editBtn = new QPushButton(style->standardIcon(QStyle::SP_FileDialogDetailedView), "Edit", this);
    m_deleteBtn = new QPushButton(style->standardIcon(QStyle::SP_TrashIcon), "Delete", this);
    m_deleteBtn->setObjectName("danger");
    m_downloadBtn = new QPushButton(style->standardIcon(QStyle::SP_ArrowDown), "Downloads", this);
    m_editorBtn = new QPushButton(style->standardIcon(QStyle::SP_FileDialogContentsView), "Save Editor", this);
    m_settingsBtn = new QPushButton(style->standardIcon(QStyle::SP_ComputerIcon), "Settings", this);
    for (QPushButton *b : {m_launchBtn, m_createBtn, m_editBtn, m_deleteBtn, m_downloadBtn, m_editorBtn, m_settingsBtn})
        btnCol->addWidget(b);
    btnCol->addStretch();
    mainRow->addLayout(btnCol);

    root->addLayout(mainRow, 1);

    m_progressBar = new AnimatedProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(false);
    m_progressBar->setValue(0);
    root->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Ready", this);
    m_statusLabel->setObjectName("statusLabel");
    root->addWidget(m_statusLabel);

    m_launchBtn->setEnabled(false);
    m_editBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);

    m_launcher = new GameLauncher(this, this);
    m_updateChecker = new UpdateChecker(this, this);
    // Owned here (not by DownloadsDialog) so an in-flight download/extraction survives the
    // dialog being closed and reopened.
    m_versionManager = new VersionManager(this);
    m_logPipe = new LogPipeServer(this);

    connect(m_versionManager, &VersionManager::downloadFinished, this, [this](const QString &id, bool success, const QString &message) {
        if (success) m_statusLabel->setText(QString("Finished installing %1").arg(id));
        else if (message != "Cancelled") m_statusLabel->setText(QString("Download failed: %1").arg(message));
        else m_statusLabel->setText(QString("Cancelled %1").arg(id));
    });
    connect(m_versionManager, &VersionManager::remoteVersionsReady, this, [](const QJsonArray &versions) {
        GD_DEBUG_LOG("versions", QString("Fetched %1 remote version(s)").arg(versions.size()));
        for (const QJsonValue &v : versions) {
            const QJsonObject o = v.toObject();
            GD_DEBUG_LOG("versions", QString("  %1 -- installed=%2")
                .arg(o.value("id").toString(), o.value("isInstalled").toBool() ? "yes" : "no"));
        }
    });
    connect(m_versionManager, &VersionManager::downloadStarted, this, [](const QString &id) {
        GD_DEBUG_LOG("download", QString("Started downloading %1").arg(id));
    });
    connect(m_versionManager, &VersionManager::downloadProgress, this, [](const QString &id, qint64 recv, qint64 total) {
        if (total > 0 && recv == total) GD_DEBUG_LOG("download", QString("%1 download complete (%2 bytes)").arg(id).arg(total));
    });
    connect(m_versionManager, &VersionManager::extractionStarted, this, [](const QString &id) {
        GD_DEBUG_LOG("extract", QString("Extracting %1").arg(id));
    });
    connect(m_versionManager, &VersionManager::downloadFinished, this, [](const QString &id, bool success, const QString &message) {
        GD_DEBUG_LOG("download", QString("%1 finished: success=%2 message=%3").arg(id, success ? "yes" : "no", message));
    });

    if constexpr (kDebugLoggingEnabled) {
        connect(&DebugLog::instance(), &DebugLog::message, this, &MainWindow::appendLog);
        GD_DEBUG_LOG("startup", QString("GDLauncher %1 starting (debug logging build)").arg(m_appVersion));
        GD_DEBUG_LOG("startup", QString("Data directory: %1").arg(Settings::baseDir()));
    }

    connect(m_table, &QTableWidget::itemSelectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_launchBtn, &QPushButton::clicked, this, &MainWindow::onLaunch);
    connect(m_createBtn, &QPushButton::clicked, this, &MainWindow::onCreate);
    connect(m_editBtn, &QPushButton::clicked, this, &MainWindow::onEdit);
    connect(m_deleteBtn, &QPushButton::clicked, this, &MainWindow::onDelete);
    connect(m_downloadBtn, &QPushButton::clicked, this, &MainWindow::onDownloads);
    connect(m_editorBtn, &QPushButton::clicked, this, &MainWindow::onEditor);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::onSettings);

    connect(m_launcher, &GameLauncher::statusUpdate, this, [this](const QString &msg) { m_statusLabel->setText(msg); });
    connect(m_launcher, &GameLauncher::launchComplete, this, [this]() {
        setUiEnabled(true);
        m_progressBar->setAnimatedValue(0);
        refreshInstances();
    });
    connect(m_launcher, &GameLauncher::gameStarted, this, [this]() { m_progressBar->setAnimatedValue(100); });
    connect(m_launcher, &GameLauncher::gameStopped, this, [this]() { setUiEnabled(true); });
    connect(m_launcher, &GameLauncher::logLine, this, &MainWindow::appendLog);
    connect(m_launcher, &GameLauncher::logStatusChanged, this, [this](bool running) {
        if (m_consoleWindow) m_consoleWindow->setRunning(running);
    });
    connect(m_logPipe, &LogPipeServer::logLine, this, &MainWindow::appendLog);

    connect(m_updateChecker, &UpdateChecker::statusUpdate, this, [this](const QString &msg) { m_statusLabel->setText(msg); });
    connect(m_updateChecker, &UpdateChecker::progress, this, [this](int p) { m_progressBar->setAnimatedValue(p); });
    connect(m_updateChecker, &UpdateChecker::progressComplete, this, [this]() {
        m_progressBar->setAnimatedValue(100);
        QTimer::singleShot(2000, this, [this]() { m_progressBar->setAnimatedValue(0); });
    });

    refreshInstances();
    Animations::fadeIn(this);

    QTimer::singleShot(3000, this, [this]() { m_updateChecker->check(false); });
    QTimer::singleShot(0, this, &MainWindow::runStartupChecks);

    if constexpr (kDebugLoggingEnabled) {
        // A debug build's whole point is visibility, so open the live log console
        // immediately rather than making the user dig for a button to see it.
        m_consoleWindow = new ConsoleWindow(nullptr);
        m_consoleWindow->show();
        GD_DEBUG_LOG("startup", "Live log console opened.");
    }
}

void MainWindow::runStartupChecks() {
    if (m_settings.lastRunVersion.isEmpty()) {
        m_settings.lastRunVersion = m_appVersion;
        Settings::save(m_settings);

        auto *welcome = new WelcomeDialog(this);
        connect(welcome, &WelcomeDialog::importRequested, this, [this, welcome]() {
            const bool foundFiles = m_launcher->handleFirstRunImport();
            if (foundFiles) refreshInstances();
            welcome->continueAfterImport(foundFiles);
        });
        welcome->exec();
        welcome->deleteLater();
    } else if (m_settings.lastRunVersion != m_appVersion) {
        m_settings.lastRunVersion = m_appVersion;
        Settings::save(m_settings);

        const QStringList entries = {
            "Ported the launcher to a native C++/Qt6 build for a smaller footprint and lower resource usage.",
            "Fixed the Downloads list incorrectly marking every version as installed.",
            "Refreshed the interface with a consistent theme, iconography, and animated transitions.",
            "The app now keeps its own data directory, separate from the previous Electron build."
        };
        ChangelogDialog dlg(this, m_appVersion, entries);
        dlg.exec();
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // ConsoleWindow is a separate top-level widget with no parent (so it can be shown/hidden
    // independently), so closing the main window alone leaves it open -- and since it's still a
    // visible top-level window, Qt's quitOnLastWindowClosed never fires and the process lingers.
    if (m_consoleWindow) {
        GD_DEBUG_LOG("ui", "Main window closing; closing the log console with it.");
        m_consoleWindow->close();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::appendLog(const QString &line) {
    m_logBuffer.push_back(line);
    if (m_logBuffer.size() > 2000) m_logBuffer.removeFirst();
    if (m_consoleWindow) m_consoleWindow->addLine(line);
}

void MainWindow::setUiEnabled(bool enabled) {
    for (QPushButton *b : {m_createBtn, m_downloadBtn, m_editorBtn, m_settingsBtn}) b->setEnabled(enabled);
    onSelectionChanged();
    if (!enabled) { m_launchBtn->setEnabled(false); m_editBtn->setEnabled(false); m_deleteBtn->setEnabled(false); }
}

QString MainWindow::selectedInstanceName() const { return m_selectedInstance; }

void MainWindow::refreshInstances() {
    m_instances = InstanceManager::getInstances();
    GD_DEBUG_LOG("instances", QString("Detected %1 instance(s)").arg(m_instances.size()));
    m_table->setRowCount(m_instances.size());
    for (int i = 0; i < m_instances.size(); ++i) {
        const InstanceInfo &inst = m_instances[i];
        m_table->setItem(i, 0, new QTableWidgetItem(inst.name));
        m_table->setItem(i, 1, new QTableWidgetItem(formatSize(inst.size)));
        m_table->setItem(i, 2, new QTableWidgetItem(inst.version));
        if (inst.name == m_selectedInstance) m_table->selectRow(i);
    }
    onSelectionChanged();

    // Kick off background size calculation, mirroring the async refresh in renderer.js.
    QTimer::singleShot(0, this, [this]() {
        const auto sizes = InstanceManager::calculateInstanceSizes();
        for (int i = 0; i < m_instances.size(); ++i) {
            for (const auto &s : sizes) {
                if (s.first == m_instances[i].name) { m_instances[i].size = s.second; break; }
            }
            if (i < m_table->rowCount())
                m_table->setItem(i, 1, new QTableWidgetItem(formatSize(m_instances[i].size)));
        }
    });
}

void MainWindow::onSelectionChanged() {
    const auto selected = m_table->selectionModel() ? m_table->selectionModel()->selectedRows() : QModelIndexList();
    if (selected.isEmpty()) {
        m_selectedInstance.clear();
    } else {
        const int row = selected.first().row();
        if (row >= 0 && row < m_instances.size()) m_selectedInstance = m_instances[row].name;
    }
    const bool sel = !m_selectedInstance.isEmpty() && !m_launcher->isGameRunning();
    m_launchBtn->setEnabled(sel);
    m_editBtn->setEnabled(sel);
    m_deleteBtn->setEnabled(sel);
}

void MainWindow::onLaunch() {
    if (m_selectedInstance.isEmpty()) return;
    GD_DEBUG_LOG("launch", QString("Launch requested for instance '%1'").arg(m_selectedInstance));
    setUiEnabled(false);
    m_progressBar->setAnimatedValue(10);
    m_statusLabel->setText(QString("Preparing %1...").arg(m_selectedInstance));
    m_launcher->launchInstance(m_selectedInstance);
}

void MainWindow::onCreate() {
    GD_DEBUG_LOG("ui", "Create button clicked; opening Create Instance dialog");
    InstanceDialog dlg(this, false);
    const int result = dlg.exec();
    GD_DEBUG_LOG("ui", QString("Create Instance dialog closed (%1)").arg(result == QDialog::Accepted ? "Save" : "Cancel"));
    if (result != QDialog::Accepted) return;
    const InstanceSaveResult res = InstanceManager::createInstance(dlg.data());
    GD_DEBUG_LOG("instances", QString("Create '%1': success=%2%3")
        .arg(dlg.data().value("name").toString(), res.success ? "yes" : "no",
             res.success ? "" : (" error=" + res.error)));
    if (res.success) {
        m_selectedInstance = dlg.data().value("name").toString();
        refreshInstances();
    } else {
        QMessageBox::warning(this, "Failed", res.error);
    }
}

void MainWindow::onEdit() {
    if (m_selectedInstance.isEmpty()) return;
    const InstanceInfo *inst = nullptr;
    for (const auto &i : m_instances) if (i.name == m_selectedInstance) { inst = &i; break; }
    if (!inst) return;

    GD_DEBUG_LOG("ui", QString("Edit button clicked for '%1'; opening Edit Instance dialog").arg(m_selectedInstance));
    InstanceDialog dlg(this, true, inst->data);
    QString originalName = m_selectedInstance;
    connect(&dlg, &InstanceDialog::openInEditorRequested, this, [this, originalName]() {
        GD_DEBUG_LOG("ui", QString("'Edit Save File in Editor' clicked for '%1'; opening Save Editor").arg(originalName));
        QTimer::singleShot(0, this, [this, originalName]() {
            SaveEditorDialog editor(this);
            editor.selectInstance(originalName);
            editor.exec();
            GD_DEBUG_LOG("ui", "Save Editor closed");
        });
    });
    const int result = dlg.exec();
    GD_DEBUG_LOG("ui", QString("Edit Instance dialog closed (%1)").arg(result == QDialog::Accepted ? "Save" : "Cancel"));
    if (result != QDialog::Accepted) return;
    const InstanceSaveResult res = InstanceManager::editInstance(originalName, dlg.data());
    if (res.success) {
        m_selectedInstance = dlg.data().value("name").toString();
        refreshInstances();
    } else {
        QMessageBox::warning(this, "Failed", res.error);
    }
}

void MainWindow::onDelete() {
    if (m_selectedInstance.isEmpty()) return;
    GD_DEBUG_LOG("ui", QString("Delete button clicked for '%1'").arg(m_selectedInstance));
    if (QMessageBox::question(this, "Delete instance",
            QString("Are you sure you want to move '%1' to Trash?").arg(m_selectedInstance)) != QMessageBox::Yes) {
        GD_DEBUG_LOG("ui", "Delete cancelled by user");
        return;
    }
    GD_DEBUG_LOG("instances", QString("Deleting instance '%1'").arg(m_selectedInstance));
    const InstanceSaveResult res = InstanceManager::deleteInstance(m_selectedInstance);
    if (res.success) {
        m_selectedInstance.clear();
        refreshInstances();
    } else {
        QMessageBox::warning(this, "Failed to delete", res.error);
    }
}

void MainWindow::onSettings() {
    GD_DEBUG_LOG("ui", "Settings button clicked; opening Settings dialog");
    SettingsDialog dlg(this, m_settings, m_appVersion);
    connect(&dlg, &SettingsDialog::checkUpdatesRequested, this, [this]() {
        GD_DEBUG_LOG("ui", "'Check for Updates' clicked");
        m_updateChecker->check(true);
    });
    connect(&dlg, &SettingsDialog::openDataFolderRequested, this, []() {
        GD_DEBUG_LOG("ui", "'Open Data Folder' clicked");
        QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::baseDir()));
    });
    const int result = dlg.exec();
    GD_DEBUG_LOG("ui", QString("Settings dialog closed (%1)").arg(result == QDialog::Accepted ? "Save and Close" : "dismissed"));
    if (result == QDialog::Accepted) {
        m_settings = dlg.resultSettings();
        Settings::save(m_settings);
        Theme::apply(Theme::fromSettingsString(m_settings.theme));
        GD_DEBUG_LOG("ui", QString("Settings saved: theme=%1 close_behavior=%2 sync_delay=%3 log_output=%4")
            .arg(m_settings.theme, m_settings.closeBehavior).arg(m_settings.syncDelay)
            .arg(m_settings.enableLogOutput ? "on" : "off"));
    }
}

void MainWindow::onDownloads() {
    GD_DEBUG_LOG("ui", "Downloads button clicked; opening Downloads dialog");
    DownloadsDialog dlg(this, m_versionManager);
    dlg.exec();
    GD_DEBUG_LOG("ui", "Downloads dialog closed");
    refreshInstances();
}

void MainWindow::onEditor() {
    GD_DEBUG_LOG("ui", "Save Editor button clicked; opening Save Editor dialog");
    SaveEditorDialog dlg(this);
    dlg.exec();
    GD_DEBUG_LOG("ui", "Save Editor dialog closed");
}
