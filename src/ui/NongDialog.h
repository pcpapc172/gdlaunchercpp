#pragma once
#include <QDialog>
#include <QHash>
#include <QVector>
#include "../NongManager.h"

class QLineEdit;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QTimer;

class NongDialog : public QDialog {
    Q_OBJECT
public:
    NongDialog(QWidget *parent, NongManager *manager);
private:
    NongManager *m_manager;
    QString m_directory;
    QLineEdit *m_folder;
    QLabel *m_folderPath;
    QLineEdit *m_search;
    QLabel *m_status;
    QLabel *m_pageLabel;
    QPushButton *m_previous;
    QPushButton *m_next;
    QPushButton *m_refresh;
    QVBoxLayout *m_list;
    QVector<int> m_filtered;
    QHash<QString, QVector<QWidget *>> m_cards;
    int m_page = 0;
    void applyDirectory();
    void filterSongs();
    void renderPage();
    QWidget *buildCard(const NongSong &song);
    void updateProgress(const QString &key, qint64 received, qint64 total);
};
