#pragma once

#include <QString>
#include <QMap>
#include <QUrl>
#include <atomic>
#include <functional>

namespace InstanceUpdater {
struct Result {
    QString version;
    QString error;
    int downloaded = 0;
    bool available = false;
    int updateNumber = 0;
};

struct Index {
    QMap<QString, QUrl> manifests;
    QString error;
    bool missing = false;
};

Index readIndex(const QUrl &url, const std::atomic_bool &cancelled);
using Progress = std::function<void(const QString &, int)>;

bool validUrl(const QUrl &url);
void migrateInstalledRelease(const QString &directory);
QString installedVersion(const QString &directory);
int installedUpdateNumber(const QString &directory);
bool matchesInstalledRelease(const QString &directory, const Result &release);
bool needsRecovery(const QString &directory);
Result run(const QString &directory, const QUrl &manifest, bool install,
           const std::atomic_bool &cancelled, const Progress &progress = {}, bool onlyIfNew = false,
           const QString &installationId = {});
}
