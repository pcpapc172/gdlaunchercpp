#include "UpdateChecker.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QFile>
#include <QStandardPaths>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QCoreApplication>
#include <QStringList>

UpdateChecker::UpdateChecker(QWidget *dialogParent, QObject *parent)
    : QObject(parent), m_dialogParent(dialogParent) {}

bool UpdateChecker::isLinux() {
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
}

bool UpdateChecker::isFedora() {
    if (!isLinux()) return false;
    QFile f("/etc/os-release");
    if (!f.open(QIODevice::ReadOnly)) return false;
    return f.readAll().contains("ID=fedora");
}

bool UpdateChecker::isRpmOstree() {
    QProcess p;
    p.start("which", {"rpm-ostree"});
    p.waitForFinished(2000);
    return p.exitCode() == 0;
}

bool UpdateChecker::installRpm(const QString &rpmPath) {
    QStringList args = isRpmOstree() ? QStringList{"rpm-ostree", "install", rpmPath}
                                      : QStringList{"dnf", "install", "-y", rpmPath};
    QProcess p;
    p.start("pkexec", args);
    if (!p.waitForFinished(-1)) return false;
    return p.exitCode() == 0;
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

void UpdateChecker::check(bool isManual) {
    QNetworkRequest req{QUrl("https://api.github.com/repos/pcpapc172/gdlauncher/releases/latest")};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    QNetworkReply *reply = m_net.get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, isManual]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (isManual) QMessageBox::critical(m_dialogParent, "Update Error", reply->errorString());
            return;
        }
        const QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        QString tag = data.value("tag_name").toString();
        const QString latestVersion = tag.startsWith('v') ? tag.mid(1) : tag;
        const QString currentVersion = QCoreApplication::applicationVersion();

        if (!versionGreater(latestVersion, currentVersion)) {
            if (isManual) QMessageBox::information(m_dialogParent, "No Updates", "You already have the latest version.");
            return;
        }

        QJsonObject updateAsset;
        const QJsonArray assets = data.value("assets").toArray();
        if (isLinux()) {
            if (isFedora()) {
                for (const QJsonValue &av : assets)
                    if (av.toObject().value("name").toString().endsWith(".rpm")) { updateAsset = av.toObject(); break; }
            }
            if (updateAsset.isEmpty()) {
                for (const QJsonValue &av : assets)
                    if (av.toObject().value("name").toString().endsWith(".deb")) { updateAsset = av.toObject(); break; }
            }
        } else {
            for (const QJsonValue &av : assets) {
                const QString name = av.toObject().value("name").toString();
                if (name.endsWith(".exe") || name.endsWith("-Setup.exe")) { updateAsset = av.toObject(); break; }
            }
        }

        if (updateAsset.isEmpty()) {
            if (isManual)
                QMessageBox::warning(m_dialogParent, "No Compatible Update",
                    QString("Update %1 is available, but no compatible installer was found for your platform.").arg(latestVersion));
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
        connect(dlReply, &QNetworkReply::finished, this, [this, dlReply, out, downloadPath]() {
            out->close();
            dlReply->deleteLater();
            out->deleteLater();
            if (dlReply->error() != QNetworkReply::NoError) return;

            emit progressComplete();

            if (isLinux() && isFedora() && downloadPath.endsWith(".rpm")) {
                emit statusUpdate("Installing update...");
                if (installRpm(downloadPath)) {
                    QMessageBox::information(m_dialogParent, "Update Installed", "Update installed successfully. Restarting…");
                    QCoreApplication::quit();
                }
            } else {
                QDesktopServices::openUrl(QUrl::fromLocalFile(downloadPath));
            }
        });
    });
}
