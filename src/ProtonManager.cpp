#include "ProtonManager.h"
#include "Settings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSysInfo>
#include <QStandardPaths>

ProtonManager::ProtonManager(QObject *parent) : QObject(parent) {}

QString ProtonManager::protonDir() { return Settings::baseDir() + "/Runtimes/GE-Proton"; }
QString ProtonManager::launcherPath() { return Settings::baseDir() + "/Runtimes/umu/umu-run"; }
QString ProtonManager::prefixDir() { return Settings::baseDir() + "/ProtonPrefix"; }

QProcessEnvironment ProtonManager::environment() {
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("PROTONPATH", protonDir());
    env.insert("WINEPREFIX", prefixDir());
    env.insert("GAMEID", "0");
    env.insert("PROTON_VERB", "waitforexitandrun");
    return env;
}

void ProtonManager::ensureReady() {
    if (m_busy) return;
    m_busy = true;
    if (QStandardPaths::findExecutable("python3").isEmpty() || QStandardPaths::findExecutable("tar").isEmpty()) {
        finish(false, "GE-Proton requires Python 3.10 or newer and tar. Please install them and try again.");
        return;
    }
    auto *python = new QProcess(this);
    connect(python, &QProcess::errorOccurred, this, [this, python](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { python->deleteLater(); finish(false, "Could not start python3."); }
    });
    connect(python, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, python](int code, QProcess::ExitStatus status) {
        python->deleteLater();
        if (code != 0 || status != QProcess::NormalExit) {
            finish(false, "UMULauncher requires Python 3.10 or newer. Please upgrade python3 and try again.");
            return;
        }
        ensureComponent(true);
    });
    python->start("python3", {"-c", "import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)"});
}

void ProtonManager::finish(bool success, const QString &error) {
    m_staging.reset();
    m_busy = false;
    emit progress(success ? 100 : 0);
    emit finished(success, error);
}

void ProtonManager::ensureComponent(bool proton) {
    const QString required = proton ? protonDir() + "/proton" : launcherPath();
    if (QFileInfo::exists(required)) {
        if (proton) ensureComponent(false);
        else initializePrefix();
        return;
    }
    const QString cpu = QSysInfo::currentCpuArchitecture();
    if (cpu != "x86_64" && cpu != "arm64") {
        finish(false, "GE-Proton is only available for x86_64 and aarch64 Linux.");
        return;
    }
    emit statusUpdate(proton ? "Finding the latest GE-Proton release..." : "Finding the portable Proton launcher...");
    emit progress(-1);
    const QString repo = proton ? "GloriousEggroll/proton-ge-custom" : "Open-Wine-Components/umu-launcher";
    QNetworkRequest req{QUrl("https://api.github.com/repos/" + repo + "/releases/latest")};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(60000);
    auto *reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, proton, cpu]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { finish(false, reply->errorString()); return; }
        const auto assets = QJsonDocument::fromJson(reply->readAll()).object().value("assets").toArray();
        QString url;
        for (const auto &value : assets) {
            const auto asset = value.toObject();
            const QString name = asset.value("name").toString();
            const bool architecture = cpu == "arm64" ? name.contains("aarch64") : !name.contains("aarch64");
            if ((proton && name.startsWith("GE-Proton") && name.endsWith(".tar.gz") && architecture) ||
                (!proton && name.endsWith("-zipapp.tar"))) {
                url = asset.value("browser_download_url").toString();
                break;
            }
        }
        if (QUrl(url).scheme() != "https") { finish(false, "No compatible Proton release archive was found."); return; }
        download(url, proton);
    });
}

void ProtonManager::download(const QString &url, bool proton) {
    const QString root = Settings::baseDir() + "/Runtimes";
    if (!QDir().mkpath(root)) { finish(false, "Could not create the Proton runtime directory."); return; }
    m_staging = std::make_unique<QTemporaryDir>(root + "/.install-XXXXXX");
    if (!m_staging->isValid()) { finish(false, "Could not create a temporary runtime directory."); return; }
    auto *file = new QFile(m_staging->path() + "/archive.tar", this);
    if (!file->open(QIODevice::WriteOnly)) { delete file; finish(false, "Could not write the Proton download."); return; }
    emit statusUpdate(proton ? "Downloading GE-Proton..." : "Downloading the portable Proton launcher...");
    emit progress(0);
    QNetworkRequest req{QUrl(url)};
    req.setTransferTimeout(60000);
    auto *reply = m_net.get(req);
    auto writeFailed = std::make_shared<bool>(false);
    auto consume = [reply, file, writeFailed]() {
        const auto bytes = reply->readAll();
        if (file->write(bytes) != bytes.size()) { *writeFailed = true; reply->abort(); }
    };
    connect(reply, &QNetworkReply::readyRead, this, consume);
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        emit progress(total > 0 ? static_cast<int>(received * 80 / total) : -1);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, consume, writeFailed, proton]() {
        if (reply->error() == QNetworkReply::NoError) consume();
        const bool flushed = file->flush();
        file->close();
        file->deleteLater();
        reply->deleteLater();
        if (*writeFailed || !flushed) { finish(false, "Failed to write the Proton archive. Check available disk space."); return; }
        if (reply->error() != QNetworkReply::NoError) { finish(false, reply->errorString()); return; }
        extract(proton);
    });
}

void ProtonManager::extract(bool proton) {
    emit statusUpdate(proton ? "Unpacking GE-Proton..." : "Unpacking the Proton launcher...");
    emit progress(-1);
    const QString archive = m_staging->path() + "/archive.tar";
    const QString destination = m_staging->path() + "/unpacked";
    if (!QDir().mkpath(destination)) { finish(false, "Could not create the extraction directory."); return; }
    auto *listing = new QProcess(this);
    connect(listing, &QProcess::errorOccurred, this, [this, listing](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { listing->deleteLater(); finish(false, "Could not start tar."); }
    });
    connect(listing, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, listing, proton, archive, destination](int code, QProcess::ExitStatus status) {
        listing->deleteLater();
        if (code != 0 || status != QProcess::NormalExit) { finish(false, "Invalid Proton archive: " + QString::fromUtf8(listing->readAllStandardError())); return; }
        const auto entries = QString::fromUtf8(listing->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
        for (const auto &entry : entries) {
            if (entry.startsWith('/') || entry.split('/').contains("..")) { finish(false, "The Proton archive contains an unsafe path."); return; }
        }
        auto *extractor = new QProcess(this);
        auto extracted = std::make_shared<int>(0);
        connect(extractor, &QProcess::readyReadStandardOutput, this, [this, extractor, extracted, total = entries.size()]() {
            *extracted += extractor->readAllStandardOutput().count('\n');
            emit progress(80 + qMin(19, total > 0 ? static_cast<int>(*extracted * 19 / total) : 0));
        });
        connect(extractor, &QProcess::errorOccurred, this, [this, extractor](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) { extractor->deleteLater(); finish(false, "Could not start tar."); }
        });
        connect(extractor, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, extractor, proton, destination](int code, QProcess::ExitStatus status) {
            extractor->deleteLater();
            if (code != 0 || status != QProcess::NormalExit) { finish(false, "Could not unpack Proton: " + QString::fromUtf8(extractor->readAllStandardError())); return; }
            const QString marker = proton ? "proton" : "umu-run";
            QString source;
            if (QFileInfo::exists(destination + "/" + marker)) source = destination;
            for (const auto &dir : QDir(destination).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (QFileInfo::exists(destination + "/" + dir + "/" + marker)) source = destination + "/" + dir;
            }
            if (source.isEmpty()) { finish(false, "The downloaded runtime is missing " + marker + "."); return; }
            const QString target = proton ? protonDir() : QFileInfo(launcherPath()).absolutePath();
            if (QFileInfo::exists(target) && !QDir(target).removeRecursively()) { finish(false, "Could not replace an incomplete runtime."); return; }
            if (!QDir().rename(source, target)) { finish(false, "Could not install the downloaded runtime."); return; }
            m_staging.reset();
            if (proton) ensureComponent(false);
            else initializePrefix();
        });
        extractor->start("tar", {"-xvf", archive, "--no-same-owner", "-C", destination});
    });
    listing->start("tar", {"-tf", archive});
}

void ProtonManager::initializePrefix() {
    if (QFileInfo::exists(prefixDir() + "/system.reg")) { finish(true); return; }
    if (!QDir().mkpath(prefixDir())) { finish(false, "Could not create the Proton prefix."); return; }
    emit statusUpdate("Preparing GE-Proton and its runtime for first launch...");
    emit progress(-1);
    auto *process = new QProcess(this);
    process->setProcessEnvironment(environment());
    connect(process, &QProcess::readyReadStandardOutput, this, [process]() { process->readAllStandardOutput(); });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { process->deleteLater(); finish(false, "Could not start python3 for the Proton launcher."); }
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process](int code, QProcess::ExitStatus status) {
        const QString error = QString::fromUtf8(process->readAllStandardError()).right(2000);
        process->deleteLater();
        if (code != 0 || status != QProcess::NormalExit || !QFileInfo::exists(prefixDir() + "/system.reg")) {
            finish(false, "Could not initialize GE-Proton: " + error);
            return;
        }
        finish(true);
    });
    process->start("python3", {launcherPath(), ""});
}
