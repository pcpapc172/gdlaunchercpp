#include "DebugLog.h"
#include <QDateTime>
#include <QDebug>

DebugLog &DebugLog::instance() {
    static DebugLog inst;
    return inst;
}

void DebugLog::log(const QString &category, const QString &message) {
    const QString line = QString("[%1] [%2] %3")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), category, message);
    qDebug().noquote() << line;
    emit this->message(line);
}
