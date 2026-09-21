#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <memory>
#include <atomic>

struct LocalVersion {
    QString id;     // "category/version"
    QString path;   // same as id
    QString category;
    QString version;
    qint64 size = -1;
};

struct VersionDefaults {
    QString executable = "GeometryDash.exe";
    QString steamEmulator = "SmartSteamEmu.exe";
    bool geodeCompatible = false;
    bool useMegaHack = true;
    bool useSteamEmu = false;
    bool skipRestartCheck = false;
};

// A version download/extraction in flight, keyed by version id. VersionManager is owned by
// MainWindow (not the Downloads dialog) precisely so this survives the dialog being closed
// and reopened -- closing the dialog must not cancel work in progress.
struct VersionOperation {
    enum class Phase { Downloading, Extracting };
    Phase phase = Phase::Downloading;
    qint64 current = 0;
    qint64 total = 0;
};

// Port of the version-management functions from main.js (getVersions, fetchRemoteVersions,
// downloadVersion, calculateAllVersionSizes, etc). Networking is async via signals. Downloads
// and extractions keep running regardless of which (if any) dialog is currently displaying them.
class VersionManager : public QObject {
    Q_OBJECT
public:
    explicit VersionManager(QObject *parent = nullptr);

    static QVector<LocalVersion> getVersions();
    static qint64 getVersionSize(const QString &versionPath);
    static VersionDefaults getVersionDefaults(const QString &versionPath);
    static bool deleteVersion(const QString &versionPath, QString *error = nullptr);

    // Async: emits remoteVersionsReady(json array) with isInstalled/size populated where possible
    void fetchRemoteVersions();

    // Async: downloads + extracts a version described by a JSON object {id, path, url, name}.
    // Keeps running even if nothing is listening to the progress signals.
    void downloadVersion(const QJsonObject &version);

    // Aborts an in-flight download, or requests that an in-flight extraction stop at its next
    // chunk boundary and clean up the partially-extracted directory.
    void cancelOperation(const QString &id);

    // Current state of an in-flight operation for `id`, if any -- lets a freshly (re)opened
    // Downloads dialog resume showing the right progress instead of a stale "Download" button.
    bool hasActiveOperation(const QString &id) const;
    VersionOperation activeOperation(const QString &id) const;

    // Async: resolves a human-readable size for each version (local folder size if
    // installed, remote Content-Length via HTTP HEAD otherwise) and emits sizeResolved
    // once per entry as it completes, so the UI can update incrementally.
    void resolveSizes(const QJsonArray &versions);

signals:
    void remoteVersionsReady(const QJsonArray &versions);
    void sizeResolved(const QString &id, const QString &size);
    void downloadStarted(const QString &id);
    void downloadProgress(const QString &id, qint64 received, qint64 total);
    // Emitted once the archive has fully downloaded and extraction (which runs on a
    // background thread so the UI never blocks) begins.
    void extractionStarted(const QString &id);
    void extractionProgress(const QString &id, qint64 extracted, qint64 total);
    void downloadFinished(const QString &id, bool success, const QString &message);

private:
    QNetworkAccessManager m_net;
    QHash<QString, QNetworkReply *> m_activeReplies;
    QHash<QString, std::shared_ptr<std::atomic<bool>>> m_cancelFlags;
    QHash<QString, VersionOperation> m_operations;
};
