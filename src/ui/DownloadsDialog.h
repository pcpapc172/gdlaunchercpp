#pragma once
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include "../VersionManager.h"

class QLineEdit;
class QLabel;
class QVBoxLayout;
class QGridLayout;
class QScrollArea;

// Port of the Downloads modal (version manager) from index.html/renderer.js.
// The VersionManager itself is owned by MainWindow, not this dialog, so an
// in-flight download/extraction keeps running when the dialog is closed and
// reopened rather than being cancelled.
class DownloadsDialog : public QDialog {
    Q_OBJECT
public:
    DownloadsDialog(QWidget *parent, VersionManager *versionManager);

private:
    VersionManager *m_versionManager;
    QLineEdit *m_searchEdit;
    QWidget *m_statsWidget;
    QWidget *m_listWidget;
    QVBoxLayout *m_listLayout;
    QJsonArray m_versions;
    QHash<QString, QWidget *> m_cardWidgets;

    void refresh();
    void renderStats();
    void renderList(const QString &filter = QString());
    QWidget *buildCard(const QJsonObject &v);
    void applyOperationState(QWidget *card, const QString &id);
    void startDownload(const QString &id, bool repair);
    void deleteVersion(const QString &path, const QString &id);
    void openVersionFolder(const QString &path);
};
