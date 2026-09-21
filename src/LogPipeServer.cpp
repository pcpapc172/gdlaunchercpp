#include "LogPipeServer.h"
#include <QLocalServer>
#include <QLocalSocket>

static const char *PIPE_NAME = "gdlauncher-log";

LogPipeServer::LogPipeServer(QObject *parent) : QObject(parent) {}

void LogPipeServer::start() {
    if (m_server) return;

    m_server = new QLocalServer(this);
    QLocalServer::removeServer(PIPE_NAME); // clean up a stale socket file on Unix
    connect(m_server, &QLocalServer::newConnection, this, &LogPipeServer::handleNewConnection);

    if (!m_server->listen(PIPE_NAME)) {
        emit logLine(QString("[log] Pipe server error: %1").arg(m_server->errorString()));
        return;
    }
    emit logLine("[log] Named pipe server listening on gdlauncher-log");
}

void LogPipeServer::stop() {
    if (!m_server) return;
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
    m_connections.clear();
}

void LogPipeServer::handleNewConnection() {
    while (m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        emit logLine("[log] Geode mod connected to pipe");
        m_connections.push_back(socket);

        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            const QByteArray data = socket->readAll();
            const auto lines = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const QString trimmed = line.trimmed();
                if (!trimmed.isEmpty()) emit logLine(trimmed);
            }
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            emit logLine("[log] Geode mod disconnected from pipe");
            m_connections.removeAll(socket);
            socket->deleteLater();
        });
        connect(socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
            // Errors are surfaced via the socket's disconnect handler; nothing else to do here.
        });
    }
}
