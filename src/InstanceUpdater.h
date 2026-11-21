#pragma once

#include <QString>
#include <QUrl>
#include <atomic>
#include <functional>

namespace InstanceUpdater {
struct Result {
    QString version;
    QString error;
    int downloaded = 0;
    bool available = false;
};

using Progress = std::function<void(const QString &, int)>;

bool validUrl(const QUrl &url);
QString installedVersion(const QString &directory);
bool needsRecovery(const QString &directory);
Result run(const QString &directory, const QUrl &manifest, bool install,
           const std::atomic_bool &cancelled, const Progress &progress = {}, bool onlyIfNew = false);
}
