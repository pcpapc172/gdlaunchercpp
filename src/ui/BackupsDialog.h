#pragma once
#include <QDialog>
#include <QFuture>
#include "../BackupManager.h"
class QTableWidget;
class QLineEdit;
class QLabel;
class QPushButton;
class QProgressBar;
class QCloseEvent;

class BackupsDialog : public QDialog {
    Q_OBJECT
public:
    explicit BackupsDialog(QWidget *parent = nullptr);
    ~BackupsDialog() override;
signals:
    void progress(const QString &file, int current, int total);
protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;
private:
    QTableWidget *m_table;
    QLineEdit *m_search;
    QLabel *m_status;
    QProgressBar *m_progress;
    QPushButton *m_restore;
    QPushButton *m_fork;
    QPushButton *m_delete;
    QPushButton *m_refresh;
    bool m_busy = false;
    QVector<BackupInfo> m_backups;
    QFuture<BackupResult> m_future;
    QStringList selectedPaths() const;
    void refresh();
    void render();
    void updateActions();
    void restore();
    void fork();
    void remove();
    void run(const std::function<BackupResult(const BackupManager::Progress &)> &operation,
             const QString &success);
    bool canClose();
};
