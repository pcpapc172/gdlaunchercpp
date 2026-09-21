#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

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
};

// Port of the version-management functions from main.js (getVersions, fetchRemoteVersions,
// downloadVersion, calculateAllVersionSizes, etc). Networking is async via signals.
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

    // Async: downloads + extracts a version described by a JSON object {id, path, url, name}
    void downloadVersion(const QJsonObject &version);

signals:
    void remoteVersionsReady(const QJsonArray &versions);
    void downloadStarted(const QString &id);
    void downloadProgress(const QString &id, qint64 received, qint64 total);
    void downloadFinished(const QString &id, bool success, const QString &message);

private:
    QNetworkAccessManager m_net;
};
