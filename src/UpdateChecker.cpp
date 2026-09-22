#include "UpdateChecker.h"
#include "DebugLog.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QCoreApplication>
#include <QStringList>

#ifdef Q_OS_WIN
extern "C" {
#include "miniz.h"
}
#endif

static const char *kUpdateRepo = "pcpapc172/gdlaunchercpp";

UpdateChecker::UpdateChecker(QWidget *dialogParent, QObject *parent)
    : QObject(parent), m_dialogParent(dialogParent) {}

bool UpdateChecker::isLinux() {
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
}

bool UpdateChecker::versionGreater(const QString &a, const QString &b) {
    const auto pa = a.split('.'), pb = b.split('.');
    for (int i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const int va = i < pa.size() ? pa[i].toInt() : 0;
        const int vb = i < pb.size() ? pb[i].toInt() : 0;
        if (va != vb) return va > vb;
    }
    return false;
}

// We publish plain archives (a zip on Windows, a tar.gz on Linux), not installers, so
// "updating" means extracting the new build somewhere and pointing the user at it rather than
// silently overwriting the currently-running executable (which Windows won't even allow while
// it's in use).
static bool extractZip(const QString &zipPath, const QString &destDir, QString *error) {
#ifdef Q_OS_WIN
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    const QByteArray zipPathBytes = zipPath.toLocal8Bit();
    if (!mz_zip_reader_init_file(&zip, zipPathBytes.constData(), 0)) {
        if (error) *error = "Failed to open update archive";
        return false;
    }
    const mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    QDir().mkpath(destDir);
    bool ok = true;
    for (mz_uint i = 0; i < numFiles; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) { ok = false; break; }
        QString name = QString::fromUtf8(stat.m_filename);
        name.replace('\\', '/');
        const QString outPath = destDir + "/" + name;
        if (mz_zip_reader_is_file_a_directory(&zip, i)) { QDir().mkpath(outPath); continue; }
        QDir().mkpath(QFileInfo(outPath).path());
        const QByteArray outPathBytes = outPath.toLocal8Bit();
        if (!mz_zip_reader_extract_to_file(&zip, i, outPathBytes.constData(), 0)) { ok = false; break; }
    }
    mz_zip_reader_end(&zip);
    if (!ok && error) *error = "Failed to extract update archive";
    return ok;
#else
    Q_UNUSED(zipPath);
    Q_UNUSED(destDir);
    if (error) *error = "Not a zip archive on this platform";
    return false;
#endif
}

static bool extractTarGz(const QString &archivePath, const QString &destDir, QString *error) {
    QDir().mkpath(destDir);
    QProcess p;
    p.start("tar", {"-xzf", archivePath, "-C", destDir});
    if (!p.waitForFinished(60000) || p.exitCode() != 0) {
        if (error) *error = QString::fromUtf8(p.readAllStandardError());
        return false;
    }
    return true;
}

void UpdateChecker::check(bool isManual) {
    QNetworkRequest req{QUrl(QString("https://api.github.com/repos/%1/releases/latest").arg(kUpdateRepo))};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    QNetworkReply *reply = m_net.get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, isManual]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            GD_DEBUG_LOG("update", QString("Update check failed: %1").arg(reply->errorString()));
            if (isManual) QMessageBox::critical(m_dialogParent, "Update Error", reply->errorString());
            return;
        }
        const QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        QString tag = data.value("tag_name").toString();
        const QString latestVersion = tag.startsWith('v') ? tag.mid(1) : tag;
        // A per-commit CI build's version is "<number>-<short-sha>" (see main.cpp); strip the
        // suffix before comparing so QString::toInt() on the last segment (e.g. "5-a1b2c3d")
        // doesn't silently parse as 0 in versionGreater().
        const QString currentVersion = QCoreApplication::applicationVersion().section('-', 0, 0);
        GD_DEBUG_LOG("update", QString("Latest release: %1 (current: %2)").arg(latestVersion, currentVersion));

        if (!versionGreater(latestVersion, currentVersion)) {
            if (isManual) QMessageBox::information(m_dialogParent, "No Updates", "You already have the latest version.");
            return;
        }

        // Matches what release.yml actually publishes: gdlauncher-windows-x86_64.zip and
        // gdlauncher-linux-x86_64.tar.gz.
        QJsonObject updateAsset;
        const QJsonArray assets = data.value("assets").toArray();
        for (const QJsonValue &av : assets) {
            const QString name = av.toObject().value("name").toString();
            const bool matchesPlatform = isLinux() ? name.contains("linux", Qt::CaseInsensitive)
                                                    : name.contains("windows", Qt::CaseInsensitive);
            const bool matchesExtension = isLinux() ? name.endsWith(".tar.gz") : name.endsWith(".zip");
            if (matchesPlatform && matchesExtension) { updateAsset = av.toObject(); break; }
        }

        if (updateAsset.isEmpty()) {
            if (isManual)
                QMessageBox::warning(m_dialogParent, "No Compatible Update",
                    QString("Update %1 is available, but no compatible build was found for your platform.").arg(latestVersion));
            return;
        }

        const int response = QMessageBox::information(m_dialogParent, "Update Available",
            QString("Version %1 is available.\n\nCurrent: %2\nLatest: %3\n\nFile: %4")
                .arg(latestVersion, currentVersion, latestVersion, updateAsset.value("name").toString()),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
        if (response != QMessageBox::Ok) return;

        const QString assetName = updateAsset.value("name").toString();
        const QString downloadUrl = updateAsset.value("browser_download_url").toString();
        const QString downloadPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" + assetName;
        GD_DEBUG_LOG("update", QString("Downloading %1").arg(downloadUrl));

        emit statusUpdate("Downloading update...");
        emit progressStart();

        QFile *out = new QFile(downloadPath, this);
        out->open(QIODevice::WriteOnly | QIODevice::Truncate);

        QNetworkRequest dlReq{QUrl(downloadUrl)};
        QNetworkReply *dlReply = m_net.get(dlReq);
        connect(dlReply, &QNetworkReply::readyRead, this, [dlReply, out]() { out->write(dlReply->readAll()); });
        connect(dlReply, &QNetworkReply::downloadProgress, this, [this](qint64 recv, qint64 total) {
            if (total > 0) emit progress(static_cast<int>(recv * 100 / total));
        });
        connect(dlReply, &QNetworkReply::finished, this, [this, dlReply, out, downloadPath, assetName, latestVersion]() {
            out->close();
            dlReply->deleteLater();
            out->deleteLater();
            if (dlReply->error() != QNetworkReply::NoError) {
                GD_DEBUG_LOG("update", QString("Download failed: %1").arg(dlReply->errorString()));
                QMessageBox::critical(m_dialogParent, "Update Error", dlReply->errorString());
                return;
            }

            emit progressComplete();
            emit statusUpdate("Extracting update...");

            const QString extractDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                                        QString("/gdlauncher-update-%1").arg(latestVersion);
            if (QFileInfo::exists(extractDir)) QDir(extractDir).removeRecursively();

            QString err;
            const bool ok = assetName.endsWith(".zip") ? extractZip(downloadPath, extractDir, &err)
                                                        : extractTarGz(downloadPath, extractDir, &err);
            QFile::remove(downloadPath);

            if (!ok) {
                GD_DEBUG_LOG("update", QString("Extraction failed: %1").arg(err));
                QMessageBox::critical(m_dialogParent, "Update Error", QString("Failed to extract the update: %1").arg(err));
                return;
            }

            GD_DEBUG_LOG("update", QString("Update extracted to %1").arg(extractDir));
            emit statusUpdate("Ready");
            QMessageBox::information(m_dialogParent, "Update Downloaded",
                QString("Version %1 has been downloaded and extracted. This will now open that "
                        "folder -- close GDLauncher and replace your existing installation with "
                        "the files there.").arg(latestVersion));
            QDesktopServices::openUrl(QUrl::fromLocalFile(extractDir));
        });
    });
}
