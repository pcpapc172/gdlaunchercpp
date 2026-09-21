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
class DownloadsDialog : public QDialog {
    Q_OBJECT
public:
    explicit DownloadsDialog(QWidget *parent);

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
    void startDownload(const QString &id, bool repair);
    void deleteVersion(const QString &path, const QString &id);
    void openVersionFolder(const QString &path);
};
