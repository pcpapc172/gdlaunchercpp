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

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QCoreApplication>
#include <QTimer>
#include <QJsonDocument>

static QString formatSize(qint64 bytes) {
    if (bytes < 0) return "…";
    double b = static_cast<double>(bytes);
    static const char *units[] = {"B", "KB", "MB", "GB"};
    int i = 0;
    while (b >= 1024.0 && i < 3) { b /= 1024.0; i++; }
    return QString::number(b, 'f', 2) + " " + units[i];
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Game Instance Launcher");
    resize(1000, 750);
    setMinimumSize(700, 500);

    Settings::ensureDirs();
    m_settings = Settings::load();
    m_appVersion = QCoreApplication::applicationVersion();

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);

    auto *mainRow = new QHBoxLayout();

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({"Name", "Save Size", "Version"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainRow->addWidget(m_table, 1);

    auto *btnCol = new QVBoxLayout();
    m_launchBtn = new QPushButton("Launch", this);
    m_createBtn = new QPushButton("Create", this);
    m_editBtn = new QPushButton("Edit", this);
    m_deleteBtn = new QPushButton("Delete", this);
    m_downloadBtn = new QPushButton("Downloads", this);
    m_editorBtn = new QPushButton("Save Editor", this);
    m_settingsBtn = new QPushButton("Settings", this);
    for (QPushButton *b : {m_launchBtn, m_createBtn, m_editBtn, m_deleteBtn, m_downloadBtn, m_editorBtn, m_settingsBtn})
        btnCol->addWidget(b);
    btnCol->addStretch();
    mainRow->addLayout(btnCol);

    root->addLayout(mainRow, 1);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(false);
    m_progressBar->setValue(0);
    root->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Ready", this);
    root->addWidget(m_statusLabel);

    m_launchBtn->setEnabled(false);
    m_editBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);

    m_launcher = new GameLauncher(this, this);
    m_updateChecker = new UpdateChecker(this, this);
    m_logPipe = new LogPipeServer(this);

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
        m_progressBar->setValue(0);
        refreshInstances();
    });
    connect(m_launcher, &GameLauncher::gameStarted, this, [this]() { m_progressBar->setValue(100); });
    connect(m_launcher, &GameLauncher::gameStopped, this, [this]() { setUiEnabled(true); });
    connect(m_launcher, &GameLauncher::logLine, this, &MainWindow::appendLog);
    connect(m_launcher, &GameLauncher::logStatusChanged, this, [this](bool running) {
        if (m_consoleWindow) m_consoleWindow->setRunning(running);
    });
    connect(m_logPipe, &LogPipeServer::logLine, this, &MainWindow::appendLog);

    connect(m_updateChecker, &UpdateChecker::statusUpdate, this, [this](const QString &msg) { m_statusLabel->setText(msg); });
    connect(m_updateChecker, &UpdateChecker::progress, this, [this](int p) { m_progressBar->setValue(p); });
    connect(m_updateChecker, &UpdateChecker::progressComplete, this, [this]() {
        m_progressBar->setValue(100);
        QTimer::singleShot(2000, this, [this]() { m_progressBar->setValue(0); });
    });

    refreshInstances();

    QTimer::singleShot(3000, this, [this]() { m_updateChecker->check(false); });

    if (m_settings.lastRunVersion.isEmpty()) {
        m_settings.lastRunVersion = m_appVersion;
        Settings::save(m_settings);
        if (m_launcher->handleFirstRunImport()) refreshInstances();
    } else if (m_settings.lastRunVersion != m_appVersion) {
        m_settings.lastRunVersion = m_appVersion;
        Settings::save(m_settings);
        QMessageBox::information(this, QString("What's New v%1").arg(m_appVersion),
            "Ported to a native C++/Qt6 build for a smaller footprint and lower resource usage.");
    }
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
    setUiEnabled(false);
    m_progressBar->setValue(10);
    m_statusLabel->setText(QString("Preparing %1...").arg(m_selectedInstance));
    m_launcher->launchInstance(m_selectedInstance);
}

void MainWindow::onCreate() {
    InstanceDialog dlg(this, false);
    if (dlg.exec() != QDialog::Accepted) return;
    const InstanceSaveResult res = InstanceManager::createInstance(dlg.data());
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

    InstanceDialog dlg(this, true, inst->data);
    QString originalName = m_selectedInstance;
    connect(&dlg, &InstanceDialog::openInEditorRequested, this, [this, originalName]() {
        QTimer::singleShot(0, this, [this, originalName]() {
            SaveEditorDialog editor(this);
            editor.selectInstance(originalName);
            editor.exec();
        });
    });
    if (dlg.exec() != QDialog::Accepted) return;
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
    if (QMessageBox::question(this, "Delete instance",
            QString("Are you sure you want to move '%1' to Trash?").arg(m_selectedInstance)) != QMessageBox::Yes)
        return;
    const InstanceSaveResult res = InstanceManager::deleteInstance(m_selectedInstance);
    if (res.success) {
        m_selectedInstance.clear();
        refreshInstances();
    } else {
        QMessageBox::warning(this, "Failed to delete", res.error);
    }
}

void MainWindow::onSettings() {
    SettingsDialog dlg(this, m_settings, m_appVersion);
    connect(&dlg, &SettingsDialog::checkUpdatesRequested, this, [this]() { m_updateChecker->check(true); });
    connect(&dlg, &SettingsDialog::openDataFolderRequested, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::baseDir()));
    });
    if (dlg.exec() == QDialog::Accepted) {
        m_settings = dlg.resultSettings();
        Settings::save(m_settings);
    }
}

void MainWindow::onDownloads() {
    DownloadsDialog dlg(this);
    dlg.exec();
    refreshInstances();
}

void MainWindow::onEditor() {
    SaveEditorDialog dlg(this);
    dlg.exec();
}
