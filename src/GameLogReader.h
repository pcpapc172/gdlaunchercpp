#pragma once
#include <QObject>
#include <QHash>
#include <QTimer>

class GameLogReader : public QObject {
    Q_OBJECT
public:
    explicit GameLogReader(QObject *parent = nullptr);
    void start(const QStringList &directories);
    void stop();
    void poll();
signals:
    void logLine(const QString &line);
private:
    struct Cursor { qint64 offset = 0; QByteArray pending; };
    QStringList m_directories;
    QHash<QString, Cursor> m_files;
    QTimer m_timer;
};
