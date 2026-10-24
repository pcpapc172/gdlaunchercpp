#pragma once
#include <QHash>
#include <QSet>
#include <QString>
class QProcess;

// Tracks a launch's process family, never a machine-wide executable name.
class ProcessTracker {
public:
    ProcessTracker();
    ~ProcessTracker();
    QString token() const { return m_token; }
    void reset();
    void attach(qint64 pid);
    void configureProcess(QProcess *process);
    bool isRunning(const QString &executableName);
    void terminate();
private:
    QSet<qint64> ownedProcesses();
    QString m_token;
    QHash<qint64, quint64> m_known;
    void *m_job = nullptr;
};
