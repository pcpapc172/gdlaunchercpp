#pragma once
#include <QObject>
#include <QVector>

class QLocalServer;
class QLocalSocket;

// Port of the Geode advanced-logging named pipe server from main.js.
// QLocalServer maps to a Windows named pipe on Windows and a Unix domain
// socket on Linux/macOS -- exactly analogous to Node's net.createServer with
// a \\.\pipe\ path.
class LogPipeServer : public QObject {
    Q_OBJECT
public:
    explicit LogPipeServer(QObject *parent = nullptr);

    void start();
    void stop();

signals:
    void logLine(const QString &line);

private:
    QLocalServer *m_server = nullptr;
    QVector<QLocalSocket *> m_connections;

    void handleNewConnection();
};
