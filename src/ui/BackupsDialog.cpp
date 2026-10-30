#include "BackupsDialog.h"
#include "CloseGuard.h"
#include "Theme.h"
#include "../Settings.h"
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>
#include <QElapsedTimer>
#include <cstdlib>

BackupsDialog::BackupsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Instance Backups");
    setWindowIcon(QIcon(":/icon.png"));
    resize(1000, 650);
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel("Instance Backups", this);
    heading->setObjectName("heading");
    layout->addWidget(heading);
    auto *folder = new QLabel("Backup folder: " + Settings::backupsDir(), this);
    folder->setTextFormat(Qt::PlainText);
    folder->setWordWrap(true);
    layout->addWidget(folder);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Search instance, date, folder, or backup type...");
    m_search->setObjectName("backupSearch");
    layout->addWidget(m_search);
    m_table = new QTableWidget(0, 5, this);
    m_table->setObjectName("backupTable");
    m_table->setHorizontalHeaderLabels({"Instance", "Created", "Contents", "Type", "Folder"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSortingEnabled(true);
    layout->addWidget(m_table, 1);
    auto *actions = new QHBoxLayout;
    m_restore = new QPushButton("Restore", this);
    m_restore->setObjectName("backupRestore");
    m_fork = new QPushButton("Fork to new instance", this);
    m_fork->setObjectName("backupFork");
    m_delete = new QPushButton("Delete selected", this);
    m_delete->setObjectName("danger");
    m_refresh = new QPushButton("Refresh", this);
    auto *open = new QPushButton("Open folder", this);
    auto *close = new QPushButton("Close", this);
    for (auto *button : {m_restore, m_fork, m_delete, m_refresh, open}) actions->addWidget(button);
    actions->addStretch();
    actions->addWidget(close);
    layout->addLayout(actions);
    m_progress = new QProgressBar(this);
    m_progress->hide();
    layout->addWidget(m_progress);
    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    connect(m_search, &QLineEdit::textChanged, this, &BackupsDialog::render);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &BackupsDialog::updateActions);
    connect(m_refresh, &QPushButton::clicked, this, &BackupsDialog::refresh);
    connect(m_restore, &QPushButton::clicked, this, &BackupsDialog::restore);
    connect(m_fork, &QPushButton::clicked, this, &BackupsDialog::fork);
    connect(m_delete, &QPushButton::clicked, this, &BackupsDialog::remove);
    connect(close, &QPushButton::clicked, this, &BackupsDialog::reject);
    connect(open, &QPushButton::clicked, this, [this]() {
        const auto paths = selectedPaths();
        QDesktopServices::openUrl(QUrl::fromLocalFile(paths.size() == 1 ? paths.first() : Settings::backupsDir()));
    });
    connect(this, &BackupsDialog::progress, this, [this](const QString &file, int current, int total) {
        m_status->setText(QString("%1 — %2 / %3 files").arg(file).arg(current).arg(total));
        m_progress->setRange(0, total > 0 ? 100 : 0);
        if (total > 0) m_progress->setValue(static_cast<int>(qint64(current) * 100 / total));
    });
    refresh();
}
BackupsDialog::~BackupsDialog() { m_future.waitForFinished(); }
QStringList BackupsDialog::selectedPaths() const {
    QStringList paths;
    for (const auto &row : m_table->selectionModel()->selectedRows())
        paths << m_table->item(row.row(), 0)->data(Qt::UserRole).toString();
    return paths;
}
void BackupsDialog::refresh() {
    m_backups = BackupManager::list();
    render();
    m_status->setText(QString("%1 backups. Select one to restore or fork; Ctrl-click or Ctrl+A selects several for deletion.").arg(m_backups.size()));
}
void BackupsDialog::render() {
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    const auto terms = m_search->text().toLower().simplified().split(' ', Qt::SkipEmptyParts);
    for (const auto &backup : m_backups) {
        const QString type = backup.reason == "before_restore" ? "Before restore" : "Scheduled";
        const QString contents = backup.scope == "managed" ? "Managed saves" : "Entire instance";
        const auto text = (backup.instanceName + " " + backup.createdAt + " " + backup.path + " " + type + " " + contents).toLower();
        bool match = true;
        for (const auto &term : terms) if (!text.contains(term)) { match = false; break; }
        if (!match) continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        const QStringList cells{backup.instanceName, backup.createdAt, contents, type, QFileInfo(backup.path).fileName()};
        for (int i = 0; i < cells.size(); ++i) m_table->setItem(row, i, new QTableWidgetItem(cells[i]));
        m_table->item(row, 0)->setData(Qt::UserRole, backup.path);
    }
    m_table->setSortingEnabled(true);
    updateActions();
}
void BackupsDialog::updateActions() {
    const auto paths = selectedPaths();
    m_restore->setEnabled(!m_busy && paths.size() == 1);
    m_fork->setEnabled(!m_busy && paths.size() == 1);
    m_delete->setEnabled(!m_busy && !paths.isEmpty());
    m_refresh->setEnabled(!m_busy);
}
void BackupsDialog::run(const std::function<BackupResult(const BackupManager::Progress &)> &operation,
                        const QString &success) {
    m_busy = true;
    m_table->setEnabled(false);
    m_search->setEnabled(false);
    updateActions();
    m_progress->show();
    m_progress->setRange(0, 0);
    m_status->setText("Preparing backup operation...");
    auto *watcher = new QFutureWatcher<BackupResult>(this);
    connect(watcher, &QFutureWatcher<BackupResult>::finished, this, [this, watcher, success]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        m_table->setEnabled(true);
        m_search->setEnabled(true);
        m_progress->hide();
        refresh();
        m_status->setText(result.success ? success + (result.warning.isEmpty() ? "" : " " + result.warning) : result.error);
        if (!result.success) QMessageBox::critical(this, "Backup Operation Failed", result.error);
    });
    m_future = QtConcurrent::run([this, operation]() {
        QElapsedTimer timer; timer.start();
        return operation([this, &timer](const QString &file, int current, int total) {
            if (current != 1 && current != total && timer.elapsed() < 33) return;
            timer.restart();
            emit progress(file, current, total);
        });
    });
    watcher->setFuture(m_future);
}
void BackupsDialog::restore() {
    const auto paths = selectedPaths();
    if (paths.size() != 1) return;
    if (QMessageBox::warning(this, "Restore Backup",
        "Restore this backup to its original instance?\n\nThe current instance will be replaced. A new full safety backup of its current files must succeed first.",
        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok) return;
    if (QMessageBox::warning(this, "Confirm Restore",
        "Confirm replacing the current instance with the selected backup. All instances and Windows tools must be closed.",
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    const auto path = paths.first();
    run([path](const BackupManager::Progress &progress) { return BackupManager::restore(path, progress); },
        "Backup restored. A full copy of the previous instance was saved as a Before restore backup.");
}
void BackupsDialog::fork() {
    const auto paths = selectedPaths();
    if (paths.size() != 1) return;
    bool accepted = false;
    const auto name = QInputDialog::getText(this, "Fork Backup", "New instance name:", QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    const auto path = paths.first();
    run([path, name](const BackupManager::Progress &progress) { return BackupManager::fork(path, name, progress); }, "New instance created from the backup.");
}
void BackupsDialog::remove() {
    const auto paths = selectedPaths();
    if (paths.isEmpty()) return;
    if (QMessageBox::warning(this, "Delete Backups", QString("Permanently delete %1 selected backup(s)?").arg(paths.size()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    run([paths](const BackupManager::Progress &) {
        BackupResult result;
        for (const auto &path : paths) if (!BackupManager::remove(path, &result.error)) return result;
        result.success = true;
        return result;
    }, "Selected backups deleted.");
}
bool BackupsDialog::canClose() {
    if (!m_busy) return true;
    const auto settings = Settings::load();
    if (!settings.closeGuardEnabled) return true;
    if (CloseGuard::confirmForceClose(this, "A backup is being copied, restored, or deleted.",
                                      settings.allowForceClose, settings.confirmForceCloseTwice)) std::_Exit(EXIT_SUCCESS);
    return false;
}
void BackupsDialog::closeEvent(QCloseEvent *event) {
    if (!canClose()) event->ignore();
    else QDialog::closeEvent(event);
}
void BackupsDialog::reject() { if (canClose()) QDialog::reject(); }
