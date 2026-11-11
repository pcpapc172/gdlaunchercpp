#include "GameLogReader.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

GameLogReader::GameLogReader(QObject *parent) : QObject(parent) {
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &GameLogReader::poll);
}
void GameLogReader::start(const QStringList &directories) {
    stop();
    m_directories = directories;
    m_directories.removeDuplicates();
    // Existing logs are historical. New files and appended lines belong to this run.
    for (const auto &directory : m_directories)
        for (const auto &file : QDir(directory).entryInfoList({"*.log"}, QDir::Files))
            m_files.insert(file.absoluteFilePath(), Cursor{file.size(), {}});
    m_timer.start();
}
void GameLogReader::poll() {
    for (const auto &directory : m_directories) {
        for (const auto &info : QDir(directory).entryInfoList({"*.log"}, QDir::Files)) {
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) continue;
            auto &cursor = m_files[info.absoluteFilePath()];
            if (file.size() < cursor.offset) cursor = {};
            if (!file.seek(cursor.offset)) continue;
            // Bound each tick so a verbose mod cannot freeze the UI.
            cursor.pending += file.read(256 * 1024);
            cursor.offset = file.pos();
            int end;
            while ((end = cursor.pending.indexOf('\n')) >= 0) {
                auto line = cursor.pending.left(end);
                cursor.pending.remove(0, end + 1);
                if (line.endsWith('\r')) line.chop(1);
                if (!line.isEmpty()) emit logLine("[geode] " + QString::fromUtf8(line));
            }
            if (cursor.pending.size() > 256 * 1024) {
                emit logLine("[geode] " + QString::fromUtf8(cursor.pending));
                cursor.pending.clear();
            }
        }
    }
}
void GameLogReader::stop() {
    m_timer.stop();
    if (!m_directories.isEmpty()) poll();
    for (const auto &cursor : m_files)
        if (!cursor.pending.isEmpty()) emit logLine("[geode] " + QString::fromUtf8(cursor.pending));
    m_directories.clear();
    m_files.clear();
}
