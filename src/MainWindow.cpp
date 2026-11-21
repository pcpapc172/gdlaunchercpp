#include "MainWindow.h"
#include "GameLauncher.h"
#include "UpdateChecker.h"
#include "ConsoleWindow.h"
#include "LogPipeServer.h"
#include "NongManager.h"
#include "ui/NongDialog.h"
#include "InstanceManager.h"
#include "ui/InstanceDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/DownloadsDialog.h"
#include "ui/SaveEditorDialog.h"
#include "ui/WelcomeDialog.h"
#include "ui/ChangelogDialog.h"
#include "ui/CloseGuard.h"
#include <QScopedValueRollback>
#include <cstdlib>
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
#include <QFileDialog>
#include <algorithm>
#include <tuple>
#include <QDir>
#include <QFileInfo>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrentRun>

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
    m_updatesBtn = new QPushButton(style->standardIcon(QStyle::SP_BrowserReload), "Updates", this);
    m_updatesBtn->setStyleSheet("padding-right: 26px;");
    m_updatesBadge = new QLabel(m_updatesBtn);
    m_updatesBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_updatesBadge->setAlignment(Qt::AlignCenter);
    m_updatesBadge->setMinimumSize(20, 20);
    m_updatesBadge->setStyleSheet("background: #c62828; color: white; border-radius: 10px; font-weight: bold;");
    auto *badgeLayout = new QHBoxLayout(m_updatesBtn);
    badgeLayout->setContentsMargins(0, 0, 6, 0);
    badgeLayout->addStretch();
    badgeLayout->addWidget(m_updatesBadge, 0, Qt::AlignVCenter);
    m_updatesBadge->hide();
    m_createBtn = new QPushButton(style->standardIcon(QStyle::SP_FileDialogNewFolder), "Create", this);
    m_editBtn = new QPushButton(style->standardIcon(QStyle::SP_FileDialogDetailedView), "Edit", this);
    m_deleteBtn = new QPushButton(style->standardIcon(QStyle::SP_TrashIcon), "Delete", this);
    m_deleteBtn->setObjectName("danger");
    m_downloadBtn = new QPushButton(style->standardIcon(QStyle::SP_ArrowDown), "Downloads", this);
    m_editorBtn = new QPushButton(style->standardIcon(QStyle::SP_FileDialogContentsView), "Save Editor", this);
    m_nongBtn = new QPushButton(style->standardIcon(QStyle::SP_MediaVolume), "NONG Downloader", this);
    m_settingsBtn = new QPushButton(style->standardIcon(QStyle::SP_ComputerIcon), "Settings", this);
    for (QPushButton *b : {m_launchBtn, m_createBtn, m_editBtn, m_deleteBtn, m_downloadBtn, m_updatesBtn, m_editorBtn, m_nongBtn})
        btnCol->addWidget(b);
#ifdef Q_OS_LINUX
    m_runExeBtn = new QPushButton(style->standardIcon(QStyle::SP_MediaPlay), "Launch executable", this);
    m_explorerBtn = new QPushButton(style->standardIcon(QStyle::SP_DirOpenIcon), "Open Explorer", this);
    m_runExeBtn->setToolTip("Run an executable using the Wine or GE-Proton backend selected in Settings");
    m_explorerBtn->setToolTip("Open Windows Explorer in the selected Wine or GE-Proton prefix");
    btnCol->addWidget(m_runExeBtn);
    btnCol->addWidget(m_explorerBtn);
    connect(m_runExeBtn, &QPushButton::clicked, this, [this]() { runLinuxTool(false); });
    connect(m_explorerBtn, &QPushButton::clicked, this, [this]() { runLinuxTool(true); });
#endif
    btnCol->addStretch();
    btnCol->addWidget(m_settingsBtn);
    mainRow->addLayout(btnCol);

    root->addLayout(mainRow, 1);

    m_progressBar = new AnimatedProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(false);
    m_progressBar->setValue(0);
    auto *progressRow = new QHBoxLayout;
    progressRow->addWidget(m_progressBar, 1);
    m_cancelUpdate = new QPushButton("Cancel Update", this);
    m_cancelUpdate->hide();
    progressRow->addWidget(m_cancelUpdate);
    root->addLayout(progressRow);
    connect(m_cancelUpdate, &QPushButton::clicked, this, [this]() {
        if (m_updateCancel) m_updateCancel->store(true);
        m_cancelUpdate->setEnabled(false);
    });

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
    m_nongManager = new NongManager(this);
    connect(m_nongManager, &NongManager::downloadFinished, this, [this](const QString &, bool success, const QString &message) {
        m_statusLabel->setText(success ? "NONG song downloaded" : "NONG download: " + message);
    });
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
    connect(m_updatesBtn, &QPushButton::clicked, this, &MainWindow::showInstanceUpdates);
    connect(m_createBtn, &QPushButton::clicked, this, &MainWindow::onCreate);
    connect(m_editBtn, &QPushButton::clicked, this, &MainWindow::onEdit);
    connect(m_deleteBtn, &QPushButton::clicked, this, &MainWindow::onDelete);
    connect(m_downloadBtn, &QPushButton::clicked, this, &MainWindow::onDownloads);
    connect(m_editorBtn, &QPushButton::clicked, this, &MainWindow::onEditor);
    connect(m_nongBtn, &QPushButton::clicked, this, &MainWindow::onNong);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::onSettings);

    connect(m_launcher, &GameLauncher::statusUpdate, this, [this](const QString &msg) { m_statusLabel->setText(msg); });
    connect(m_launcher, &GameLauncher::closeRequested, this, [this]() { close(); });
    connect(m_launcher, &GameLauncher::launchProgress, this, [this](int percentage) {
        m_progressBar->setRange(0, percentage < 0 ? 0 : 100);
        if (percentage >= 0) m_progressBar->setAnimatedValue(percentage);
    });
    connect(m_launcher, &GameLauncher::launchComplete, this, [this]() {
        setUiEnabled(true);
        m_progressBar->setRange(0, 100);
        m_progressBar->setAnimatedValue(0);
        refreshInstances();
    });
    connect(m_launcher, &GameLauncher::gameStarted, this, [this]() {
        m_progressBar->setAnimatedValue(100);
        // "Enable Log Output" in Settings promises a live terminal when launching -- the
        // console window has to actually exist and be shown for that to be true, not just
        // silently accumulate lines nobody can see.
        if (m_settings.enableLogOutput && m_consoleWindow) {
            m_consoleWindow->show();
            m_consoleWindow->raise();
            m_consoleWindow->activateWindow();
        }
    });
    // Controls stay disabled until asynchronous save sync emits launchComplete.
    connect(m_launcher, &GameLauncher::logLine, this, &MainWindow::appendLog);
    connect(m_launcher, &GameLauncher::logStatusChanged, this, [this](bool running) {
        if (m_consoleWindow) m_consoleWindow->setRunning(running);
    });
    connect(m_logPipe, &LogPipeServer::logLine, this, &MainWindow::appendLog);
#ifdef Q_OS_WIN
    m_logPipe->start();
#endif

    connect(m_updateChecker, &UpdateChecker::statusUpdate, this, [this](const QString &msg) { m_statusLabel->setText(msg); });
    connect(m_updateChecker, &UpdateChecker::progress, this, [this](int p) { m_progressBar->setAnimatedValue(p); });
    connect(m_updateChecker, &UpdateChecker::progressComplete, this, [this]() {
        m_progressBar->setAnimatedValue(100);
        QTimer::singleShot(2000, this, [this]() { m_progressBar->setAnimatedValue(0); });
    });

    refreshInstances();
    auto *instanceUpdateTimer = new QTimer(this);
    connect(instanceUpdateTimer, &QTimer::timeout, this, [this]() { scanInstanceUpdates(); });
    instanceUpdateTimer->start(15 * 60 * 1000);

    QTimer::singleShot(3000, this, [this]() {
        if (!m_updateCancel && !m_launcher->isLaunching() && !m_launcher->isGameRunning()) m_updateChecker->check(false);
    });
    QTimer::singleShot(0, this, &MainWindow::runStartupChecks);

    // The console window needs to exist in every build, not just a debug-logging one --
    // GameLauncher::logLine/logStatusChanged and LogPipeServer both feed it regardless of
    // build type, and "Enable Log Output" in Settings is a normal user-facing option that
    // has to actually work in a plain Release build. It used to only ever get constructed
    // for kDebugLoggingEnabled builds, so turning that setting on anywhere else did nothing
    // visible: appendLog() had nowhere to put the lines.
    m_consoleWindow = new ConsoleWindow(nullptr);
    m_consoleWindow->initLines(m_logBuffer);
    if (kDebugLoggingEnabled || m_settings.enableLogOutput) {
        m_consoleWindow->show();
        GD_DEBUG_LOG("startup", "Live log console opened.");
    }
}

MainWindow::~MainWindow() {
    m_scanCancel->store(true);
    if (m_updateCancel) {
        m_updateCancel->store(true);
        m_updateFuture.waitForFinished();
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
            "Installed versions find updates automatically from pcpapc172.ir.",
            "Updates shows the version label and update number for each release.",
            "Geode Alpha 2.2091 can update to Geode v6.0.0-alpha.3."
        };
        ChangelogDialog dlg(this, m_appVersion, entries);
        dlg.exec();
    }
}

QString MainWindow::closeBlockReason() const {
    if (m_updateCancel) return "An instance update is in progress.";
    if (BackupManager::isBusy()) return "A backup is being copied, restored, or deleted.";
    if (m_launcher) {
        if (m_launcher->isBackingUp()) return "An instance backup is being created.";
        if (m_launcher->isSyncing()) return "Save files are being copied or moved back into the instance.";
        if (m_launcher->isToolRunning()) return "A Windows executable or Explorer session is still running.";
        if (m_launcher->isLaunching()) return "GDLauncher is preparing an instance or copying files.";
        if (m_launcher->isGameRunning()) return "A game instance is still open or being checked for a restart.";
    }
    if (m_versionManager && m_versionManager->hasActiveOperations()) return "Version files are being downloaded or extracted.";
    if (m_nongManager && m_nongManager->hasActiveDownloads()) return "Song files are being downloaded.";
    return {};
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_closePromptOpen) { event->ignore(); return; }
    const QString reason = closeBlockReason();
    const auto settings = Settings::load();
    if (settings.closeGuardEnabled && !reason.isEmpty()) {
        event->ignore();
        QScopedValueRollback<bool> prompting(m_closePromptOpen, true);
        if (!CloseGuard::confirmForceClose(this, reason, settings.allowForceClose, settings.confirmForceCloseTwice)) return;
        if (!closeBlockReason().isEmpty()) {
            // Explicit force-close must not wait in a worker's destructor or
            // silently run a final sync. Keep recovery markers for next launch.
            std::_Exit(EXIT_SUCCESS);
        }
        event->accept();
    }

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
    if (m_runExeBtn) m_runExeBtn->setEnabled(enabled);
    if (m_explorerBtn) m_explorerBtn->setEnabled(enabled);
    for (QPushButton *b : {m_createBtn, m_downloadBtn, m_updatesBtn, m_editorBtn, m_nongBtn, m_settingsBtn}) b->setEnabled(enabled);
    onSelectionChanged();
    if (!enabled) { m_launchBtn->setEnabled(false); m_editBtn->setEnabled(false); m_deleteBtn->setEnabled(false); }
}

QString MainWindow::selectedInstanceName() const { return m_selectedInstance; }

void MainWindow::runLinuxTool(bool explorer) {
    QString executable = "explorer.exe";
    if (!explorer) {
        executable = QFileDialog::getOpenFileName(this, "Launch executable", QString(), "Windows executables (*.exe);;All files (*)");
        if (executable.isEmpty()) return;
    }
    setUiEnabled(false);
    m_launcher->runExecutable(executable);
}

void MainWindow::refreshInstances() {
    ++m_updateRevision;
    m_instances = InstanceManager::getInstances();
    for (auto it = m_instanceUpdates.begin(); it != m_instanceUpdates.end();) {
        const auto found = std::find_if(m_instances.cbegin(), m_instances.cend(),
                                       [&](const InstanceInfo &i) { return i.name == it.key(); });
        if (found == m_instances.cend() || updateManifest(found->data).isEmpty() ||
            (it->error.isEmpty() &&
             InstanceUpdater::matchesInstalledRelease(InstanceManager::installationDir(found->data), *it) &&
             !InstanceUpdater::needsRecovery(InstanceManager::installationDir(found->data))))
            it = m_instanceUpdates.erase(it);
        else ++it;
    }
    updateBadge();
    QTimer::singleShot(250, this, [this]() { scanInstanceUpdates(); });
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
    const bool sel = !m_selectedInstance.isEmpty() && !m_launcher->isGameRunning()
        && !m_launcher->isLaunching() && !m_launcher->isSyncing() && !m_updateCancel;
    m_launchBtn->setEnabled(sel);
    m_editBtn->setEnabled(sel);
    m_deleteBtn->setEnabled(sel);
}

void MainWindow::onLaunch() {
    for (const auto &instance : m_instances) {
        if (instance.name != m_selectedInstance) continue;
        const QString manifest = updateManifest(instance.data);
        if (!manifest.isEmpty() && instance.data.value("updateBeforeLaunch").toBool(true))
            startInstanceUpdate(instance, true);
        else
            launchSelected(instance.name);
        return;
    }
}

void MainWindow::launchSelected(const QString &name) {
    setUiEnabled(false);
    m_progressBar->setAnimatedValue(10);
    m_statusLabel->setText(QString("Preparing %1…").arg(name));
    m_launcher->launchInstance(name);
}

void MainWindow::updateBadge() {
    int count = 0;
    for (const auto &update : m_instanceUpdates)
        if (update.available && update.error.isEmpty()) ++count;
    m_updatesBadge->setText(count > 99 ? "99+" : QString::number(count));
    m_updatesBadge->setVisible(count > 0);
    m_updatesBtn->setAccessibleName(count ? QString("Updates (%1 available)").arg(count) : "Updates");
}

QString MainWindow::updateManifest(const QJsonObject &data) const {
    const QString local = InstanceManager::updateManifest(data);
    if (!local.isEmpty()) return local;
    if (data.value("versionType").toString() != "local") return {};
    return m_updateIndex.manifests.value(data.value("version").toString()).toString(QUrl::FullyEncoded);
}

void MainWindow::scanInstanceUpdates() {
    if (m_scanningUpdates || m_updateCancel) return;
    struct Source { QString name; QString directory; QUrl overrideUrl; QString versionId; };
    struct Scan {
        InstanceUpdater::Index index;
        QMap<QString, InstanceUpdater::Result> results;
    };
    QList<Source> sources;
    for (const auto &instance : m_instances) {
        if (!instance.valid) continue;
        const QString config = InstanceManager::updateManifest(instance.data);
        const QString versionId = instance.data.value("versionType").toString() == "local"
            ? instance.data.value("version").toString() : QString();
        sources.append({instance.name, InstanceManager::installationDir(instance.data),
                        QUrl(config, QUrl::StrictMode), versionId});
    }
    m_scanningUpdates = true;
    emit instanceUpdateListChanged();
    const unsigned revision = m_updateRevision;
    auto *watcher = new QFutureWatcher<Scan>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, revision]() {
        m_scanningUpdates = false;
        if (revision == m_updateRevision) {
            const auto scan = watcher->result();
            if (scan.index.error.isEmpty()) m_updateIndex = scan.index;
            else m_updateIndex.error = scan.index.error;
            m_instanceUpdates = scan.results;
            updateBadge();
        } else {
            QTimer::singleShot(0, this, [this]() { scanInstanceUpdates(); });
        }
        watcher->deleteLater();
        emit instanceUpdateListChanged();
    });
    watcher->setFuture(QtConcurrent::run([sources, cancelled = m_scanCancel, cached = m_updateIndex.manifests]() {
        const QUrl indexUrl("https://pcpapc172.ir/archive/updates.json");
        Scan scan;
        scan.index = InstanceUpdater::readIndex(indexUrl, *cancelled);
        if (scan.index.missing)
            scan.index.manifests.insert("2.2/2.2091-geodealpha", QUrl("https://pcpapc172.ir/archive/update.json"));
        const auto &manifests = scan.index.error.isEmpty() ? scan.index.manifests : cached;
        QMap<std::tuple<QString, QUrl, QString>, InstanceUpdater::Result> checked;
        for (const auto &source : sources) {
            if (cancelled->load()) break;
            const QUrl url = source.overrideUrl.isEmpty() ? manifests.value(source.versionId) : source.overrideUrl;
            if (url.isEmpty()) continue;
            const auto key = std::make_tuple(source.directory, url, source.versionId);
            if (!checked.contains(key))
                checked.insert(key, InstanceUpdater::run(source.directory, url, false, *cancelled, {}, false, source.versionId));
            scan.results.insert(source.name, checked.value(key));
        }
        return scan;
    }));
}

void MainWindow::showInstanceUpdates() {
    QDialog dialog(this);
    dialog.setWindowTitle("Instance Updates");
    dialog.resize(640, 360);
    auto *layout = new QVBoxLayout(&dialog);
    auto *table = new QTableWidget(0, 3, &dialog);
    table->setHorizontalHeaderLabels({"Instance", "Installed", "Available"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table);
    QStringList names;
    auto *details = new QLabel(&dialog);
    details->setTextFormat(Qt::PlainText);
    details->setWordWrap(true);
    layout->addWidget(details);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *refresh = buttons->addButton("Check Now", QDialogButtonBox::ActionRole);
    auto *install = buttons->addButton("Update Selected", QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    const auto populate = [&]() {
        const QString selectedName = names.value(table->currentRow(), m_selectedInstance);
        names.clear();
        table->setRowCount(0);
        QStringList errors;
        for (const auto &instance : m_instances) {
            const bool recovery = InstanceUpdater::needsRecovery(InstanceManager::installationDir(instance.data));
            if (!recovery && updateManifest(instance.data).isEmpty()) continue;
            const auto update = m_instanceUpdates.value(instance.name);
            if (!update.error.isEmpty()) errors.append(instance.name + ": " + update.error);
            if ((!update.available || !update.error.isEmpty()) && !recovery) continue;
            const int row = table->rowCount();
            table->insertRow(row);
            auto *name = new QTableWidgetItem(instance.name);
            const QString installationId = instance.data.value("versionType").toString() == "local"
                ? instance.data.value("version").toString() : QString();
            name->setToolTip(installationId);
            table->setItem(row, 0, name);
            QString installed = instance.version;
            const int installedNumber = InstanceUpdater::installedUpdateNumber(InstanceManager::installationDir(instance.data));
            if (installedNumber > 0) installed += QString(" (Update %1)").arg(installedNumber);
            table->setItem(row, 1, new QTableWidgetItem(installed));
            QString available = recovery ? "Recovery required" : update.version;
            if (!recovery && update.updateNumber > 0) available += QString(" (Update %1)").arg(update.updateNumber);
            auto *release = new QTableWidgetItem(available);
            release->setToolTip(available);
            table->setItem(row, 2, release);
            names.append(instance.name);
        }
        QStringList messages;
        if (m_scanningUpdates) messages.append("Checking for updates…");
        else if (!m_updateIndex.error.isEmpty()) messages.append("Could not check for updates: " + m_updateIndex.error);
        else if (names.isEmpty() && errors.isEmpty()) messages.append("No instance updates available.");
        messages.append(errors);
        details->setText(messages.join('\n'));
        refresh->setEnabled(!m_scanningUpdates);
        install->setEnabled(!names.isEmpty() && !m_launcher->isGameRunning() && !m_launcher->isLaunching());
        if (!names.isEmpty()) table->selectRow(qMax(0, names.indexOf(selectedName)));
    };
    connect(this, &MainWindow::instanceUpdateListChanged, &dialog, populate);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(install, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(refresh, &QPushButton::clicked, &dialog, [this]() { scanInstanceUpdates(); });
    populate();
    if (!m_scanningUpdates) scanInstanceUpdates();
    if (dialog.exec() == QDialog::Accepted && table->currentRow() >= 0) {
        const QString name = names.value(table->currentRow());
        for (const auto &instance : m_instances) {
            if (instance.name == name) { startInstanceUpdate(instance, false); break; }
        }
    }
}

void MainWindow::startInstanceUpdate(const InstanceInfo &instance, bool thenLaunch) {
    if (m_updateCancel || m_launcher->isGameRunning() || m_launcher->isLaunching() ||
        m_launcher->isSyncing() || m_launcher->isToolRunning()) return;
    const QString name = instance.name;
    const QString directory = InstanceManager::installationDir(instance.data);
    const QString installationId = instance.data.value("versionType").toString() == "local"
        ? instance.data.value("version").toString() : QString();
    const QUrl manifest(updateManifest(instance.data), QUrl::StrictMode);
    if (m_versionManager->hasActiveOperations()) {
        if (thenLaunch && !InstanceUpdater::needsRecovery(directory)) launchSelected(name);
        else QMessageBox::information(this, "Version Download Active", "Wait for the version download to finish before updating.");
        return;
    }
    const QString root = QFileInfo(directory).canonicalFilePath();
    const auto overlaps = [&](const QString &path) {
        const QString abs = QFileInfo(path).canonicalFilePath();
        if (abs.isEmpty() || root.isEmpty()) return false;
#ifdef Q_OS_WIN
        constexpr auto sensitivity = Qt::CaseInsensitive;
#else
        constexpr auto sensitivity = Qt::CaseSensitive;
#endif
        return root.compare(abs, sensitivity) == 0 || root.startsWith(abs + '/', sensitivity) ||
               abs.startsWith(root + '/', sensitivity);
    };
    if (root.isEmpty() || QDir(root).isRoot() || overlaps(Settings::instancesDir()) ||
        overlaps(Settings::backupsDir()) || overlaps(Settings::trashDir()) ||
        overlaps(Settings::gameSaveDir(instance.data.value("saveFolderName").toString(), false)) ||
        overlaps(Settings::gameSaveDir(instance.data.value("saveFolderName").toString(), true))) {
        QMessageBox::warning(this, "Invalid Update Folder", "Select an installation folder separate from your saves and backups.");
        return;
    }
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    m_updateCancel = cancel;
    ++m_updateRevision;
    setUiEnabled(false);
    m_cancelUpdate->setEnabled(true);
    m_cancelUpdate->show();
    m_progressBar->setRange(0, 0);
    m_statusLabel->setText("Checking for an instance update…");
    auto *watcher = new QFutureWatcher<InstanceUpdater::Result>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, name, directory, thenLaunch]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_updateCancel.reset();
        m_cancelUpdate->hide();
        m_progressBar->setRange(0, 100);
        m_progressBar->setAnimatedValue(0);
        setUiEnabled(true);
        if (!result.error.isEmpty()) {
            m_statusLabel->setText(result.error == "Cancelled" ? "Update cancelled" : "Instance update failed");
            if (result.error == "Cancelled") return;
            if (thenLaunch && !InstanceUpdater::needsRecovery(directory)) {
                if (QMessageBox::question(this, "Update Failed", result.error + "\n\nLaunch the current installation?",
                                          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
                    launchSelected(name);
            } else {
                QMessageBox::warning(this, "Update Failed", result.error);
            }
            return;
        }
        refreshInstances();
        if (thenLaunch) launchSelected(name);
        else if (result.version.isEmpty()) m_statusLabel->setText("Installation recovered");
        else m_statusLabel->setText(QString("Version %1 ready (%2 files downloaded)").arg(result.version).arg(result.downloaded));
    });
    const QPointer<MainWindow> window(this);
    m_updateFuture = QtConcurrent::run([directory, installationId, manifest, cancel, window, thenLaunch]() {
        return InstanceUpdater::run(directory, manifest, true, *cancel,
            [window](const QString &message, int percent) {
                if (!window) return;
                QMetaObject::invokeMethod(window.data(), [window, message, percent]() {
                    if (!window || !window->m_updateCancel) return;
                    window->m_statusLabel->setText(message);
                    window->m_progressBar->setRange(0, percent < 0 ? 0 : 100);
                    if (percent >= 0) window->m_progressBar->setAnimatedValue(percent);
                    if (message == "Applying instance update…") window->m_cancelUpdate->setEnabled(false);
                }, Qt::QueuedConnection);
            }, thenLaunch, installationId);
    });
    watcher->setFuture(m_updateFuture);
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
    m_settings = Settings::load();
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
    refreshInstances();
    GD_DEBUG_LOG("ui", QString("Settings dialog closed (%1)").arg(result == QDialog::Accepted ? "Save and Close" : "dismissed"));
    if (result == QDialog::Accepted) {
        m_settings = dlg.resultSettings();
        Settings::save(m_settings);
        Theme::apply(Theme::fromSettingsString(m_settings.theme));
        if (m_settings.enableLogOutput && m_consoleWindow) m_consoleWindow->show();
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

void MainWindow::onNong() {
    NongDialog dialog(this, m_nongManager);
    dialog.exec();
    m_settings = Settings::load();
}
