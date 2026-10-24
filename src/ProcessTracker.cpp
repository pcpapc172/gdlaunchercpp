#include "ProcessTracker.h"
#include <QDir>
#include <QFile>
#include <QUuid>
#include <QProcess>
#include <QDebug>
#ifdef Q_OS_LINUX
#include <signal.h>
#elif defined(Q_OS_WIN)
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace {
QString baseName(QString path) {
    path.replace('\\', '/');
    return path.section('/', -1);
}
#ifdef Q_OS_LINUX
struct ProcInfo { qint64 parent = 0; quint64 start = 0; bool alive = false; };
ProcInfo readProcess(qint64 pid) {
    QFile file(QString("/proc/%1/stat").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto fields = file.readAll();
    // comm is parenthesized and may itself contain spaces or parentheses.
    const auto tail = fields.mid(fields.lastIndexOf(')') + 2).simplified().split(' ');
    if (tail.size() < 20) return {};
    return {tail[1].toLongLong(), tail[19].toULongLong(), tail[0] != "Z" && tail[0] != "X"};
}
#endif
}

ProcessTracker::ProcessTracker() { reset(); }
ProcessTracker::~ProcessTracker() {
#ifdef Q_OS_WIN
    if (m_job) CloseHandle(static_cast<HANDLE>(m_job));
#endif
}
void ProcessTracker::reset() {
    m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_known.clear();
#ifdef Q_OS_WIN
    if (m_job) CloseHandle(static_cast<HANDLE>(m_job));
    m_job = CreateJobObjectW(nullptr, nullptr);
#endif
}
void ProcessTracker::attach(qint64 pid) {
#ifdef Q_OS_LINUX
    const auto info = readProcess(pid);
    if (info.alive) m_known.insert(pid, info.start);
#elif defined(Q_OS_WIN)
    HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (process) {
        if (m_job && AssignProcessToJobObject(static_cast<HANDLE>(m_job), process)) m_known.insert(pid, 0);
        CloseHandle(process);
    }
#else
    Q_UNUSED(pid);
#endif
}
void ProcessTracker::configureProcess(QProcess *process) {
#ifdef Q_OS_WIN
    // Associate the process with its job before it can spawn a fast emulator
    // child and exit. Descendants then join the job automatically.
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_SUSPENDED;
    });
#endif
    QObject::connect(process, &QProcess::started, process, [this, process]() {
        const qint64 pid = process->processId();
        attach(pid);
#ifdef Q_OS_WIN
        if (!m_known.contains(pid)) {
            qWarning() << "Could not associate the game with its process job";
            process->kill();
            return;
        }
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        bool resumed = false;
        if (snapshot != INVALID_HANDLE_VALUE) {
            THREADENTRY32 entry{};
            entry.dwSize = sizeof(entry);
            if (Thread32First(snapshot, &entry)) do {
                if (entry.th32OwnerProcessID != static_cast<DWORD>(pid)) continue;
                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
                if (thread) { resumed |= ResumeThread(thread) != static_cast<DWORD>(-1); CloseHandle(thread); }
            } while (Thread32Next(snapshot, &entry));
            CloseHandle(snapshot);
        }
        if (!resumed) process->kill();
#endif
    });
}
QSet<qint64> ProcessTracker::ownedProcesses() {
    QSet<qint64> owned;
#ifdef Q_OS_LINUX
    QHash<qint64, ProcInfo> snapshot;
    const QByteArray marker = "GDLAUNCHER_SESSION=" + m_token.toUtf8();
    for (const auto &entry : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const auto pid = entry.toLongLong(&ok);
        if (!ok) continue;
        const auto info = readProcess(pid);
        if (!info.alive) continue;
        snapshot.insert(pid, info);
        if (m_known.contains(pid) && m_known.value(pid) == info.start) owned.insert(pid);
        QFile env("/proc/" + entry + "/environ");
        if (env.open(QIODevice::ReadOnly) && env.readAll().split('\0').contains(marker)) owned.insert(pid);
    }
    bool changed;
    do {
        changed = false;
        for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
            if (!owned.contains(it.key()) && owned.contains(it->parent)) {
                owned.insert(it.key());
                changed = true;
            }
        }
    } while (changed);
    m_known.clear();
    for (auto pid : owned) m_known.insert(pid, snapshot.value(pid).start);
#elif defined(Q_OS_WIN)
    if (!m_job) return owned;
    DWORD count = 64;
    for (;;) {
        QByteArray buffer(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + count * sizeof(ULONG_PTR), '\0');
        auto *list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST *>(buffer.data());
        if (QueryInformationJobObject(static_cast<HANDLE>(m_job), JobObjectBasicProcessIdList,
                                      list, static_cast<DWORD>(buffer.size()), nullptr)) {
            for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) owned.insert(list->ProcessIdList[i]);
            break;
        }
        if (GetLastError() != ERROR_MORE_DATA) break;
        count *= 2;
    }
#endif
    return owned;
}
bool ProcessTracker::isRunning(const QString &executableName) {
    for (auto pid : ownedProcesses()) {
#ifdef Q_OS_LINUX
        QFile cmd(QString("/proc/%1/cmdline").arg(pid));
        if (!cmd.open(QIODevice::ReadOnly)) continue;
        for (const auto &arg : cmd.readAll().split('\0')) {
            if (baseName(QString::fromLocal8Bit(arg)).compare(executableName, Qt::CaseInsensitive) == 0) return true;
        }
#elif defined(Q_OS_WIN)
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
        if (!process) continue;
        wchar_t path[32768];
        DWORD length = 32768;
        const bool match = QueryFullProcessImageNameW(process, 0, path, &length) &&
            baseName(QString::fromWCharArray(path, length)).compare(executableName, Qt::CaseInsensitive) == 0;
        CloseHandle(process);
        if (match) return true;
#endif
    }
    return false;
}
void ProcessTracker::terminate() {
#ifdef Q_OS_LINUX
    for (auto pid : ownedProcesses()) {
        // Recheck birth time before signaling, in case a PID was recycled.
        if (readProcess(pid).start == m_known.value(pid)) ::kill(static_cast<pid_t>(pid), SIGKILL);
    }
#elif defined(Q_OS_WIN)
    if (m_job) TerminateJobObject(static_cast<HANDLE>(m_job), 1);
#endif
}
