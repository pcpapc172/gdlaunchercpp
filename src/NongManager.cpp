#include "NongManager.h"
#include "Settings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QTemporaryFile>
#include <QUuid>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <memory>

namespace {
const QStringList formats = {"mp3", "ogg"};
QString songPath(const QString &directory, const QString &id, const QString &format) {
    return QDir(directory).filePath(id + "." + format);
}
QString backupPath(const QString &directory, const QString &id, const QString &format) {
    return songPath(directory, id, format) + ".original";
}
struct RenamedFile { QString from, to; bool preserve; };
bool rollback(const QVector<RenamedFile> &files) {
    bool success = true;
    for (auto it = files.crbegin(); it != files.crend(); ++it) {
        if (!QFile::rename(it->to, it->from)) success = false;
    }
    return success;
}
bool stageCurrent(const QString &directory, const QString &id, bool preserveOriginal,
                  QVector<RenamedFile> *renamed, QString *error) {
    for (const auto &format : formats) {
        const QString path = songPath(directory, id, format);
        if (!QFileInfo::exists(path)) continue;
        const QString backup = preserveOriginal ? backupPath(directory, id, format)
            : path + ".replacing-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!QFile::rename(path, backup)) {
            const bool recovered = rollback(*renamed);
            *error = "Could not rename the existing song. Check permissions and close any application using it.";
            if (!recovered) *error += " Some files remain under their backup names in the song folder.";
            return false;
        }
        renamed->push_back({path, backup, preserveOriginal});
    }
    return true;
}
}

NongManager::NongManager(QObject *parent, const QUrl &catalogUrl) : QObject(parent), m_catalogUrl(catalogUrl) {}

bool NongManager::validSongId(const QString &id) {
    static const QRegularExpression pattern("^[A-Za-z0-9_-]{1,64}$");
    return pattern.match(id).hasMatch();
}
QString NongManager::defaultDirectory() {
#ifdef Q_OS_LINUX
    return Settings::gameSaveDir("GeometryDash", Settings::load().useProtonGE);
#else
    return Settings::gameSaveDir("GeometryDash");
#endif
}
QString NongManager::resolveDirectory(const QString &input) {
    QString path = input.trimmed();
    path.replace('\\', '/');
    const QString local = QFileInfo(defaultDirectory()).absolutePath();
    path.replace(QRegularExpression("%LOCALAPPDATA%", QRegularExpression::CaseInsensitiveOption), local);
    const auto env = QProcessEnvironment::systemEnvironment();
    static const QRegularExpression variable("%([^%]+)%");
    auto matches = variable.globalMatch(path);
    QStringList names;
    while (matches.hasNext()) names << matches.next().captured(1);
    for (const auto &name : names) {
        QString value;
        for (const auto &key : env.keys()) if (key.compare(name, Qt::CaseInsensitive) == 0) { value = env.value(key); break; }
        if (value.isEmpty()) return {};
        path.replace("%" + name + "%", value, Qt::CaseInsensitive);
    }
    if (path == "~") path = QDir::homePath();
    else if (path.startsWith("~/")) path.replace(0, 1, QDir::homePath());
    if (!QDir::isAbsolutePath(path)) return {};
    return QDir::cleanPath(path);
}
QString NongManager::operationKey(const QString &directory, const QString &songId) {
    const QFileInfo folder(directory);
    QString path = folder.canonicalFilePath();
    if (path.isEmpty()) path = folder.absoluteFilePath();
    path = QDir::cleanPath(path) + "/" + songId;
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}
bool NongManager::hasSong(const QString &directory, const QString &songId) {
    if (!validSongId(songId)) return false;
    for (const auto &format : formats) if (QFileInfo::exists(songPath(directory, songId, format))) return true;
    return false;
}
bool NongManager::hasOriginal(const QString &directory, const QString &songId) {
    if (!validSongId(songId)) return false;
    for (const auto &format : formats) if (QFileInfo::exists(backupPath(directory, songId, format))) return true;
    return false;
}
QVector<NongSong> NongManager::parseCatalog(const QByteArray &json, QString *error) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) *error = parseError.error != QJsonParseError::NoError
            ? "SongFileHub returned invalid JSON: " + parseError.errorString()
            : "SongFileHub returned an invalid song catalogue: expected a JSON array.";
        return {};
    }
    QVector<NongSong> songs;
    for (const auto &value : document.array()) {
        const auto object = value.toObject();
        NongSong song;
        song.recordId = object.value("_id").toString();
        song.songId = object.value("songID").isDouble() ? QString::number(object.value("songID").toInteger()) : object.value("songID").toString();
        song.format = object.value("filetype").toString().toLower();
        if (!validSongId(song.songId) || !validSongId(song.recordId) || !formats.contains(song.format)) continue;
        song.name = object.value("name").toString().trimmed();
        song.description = object.value("songName").toString();
        song.credit = object.value("createdBy").toString();
        QStringList levels;
        for (const auto &level : object.value("levelID").toArray()) {
            levels << (level.isString() ? level.toString() : QString::number(level.toInteger()));
        }
        song.levels = levels.join(", ");
        song.downloads = object.value("downloads").toInteger();
        // The public site uses these same endpoints. Construct the download URL
        // from validated fields so catalogue text cannot select a local path.
        song.url = QUrl(QString("https://api.songfilehub.com/song/%1?download=true&name=%2").arg(song.recordId, song.songId));
        song.searchText = (song.name + " " + song.description + " " + song.songId + " " + song.levels + " " + song.credit).toLower();
        songs.push_back(song);
    }
    std::stable_sort(songs.begin(), songs.end(), [](const NongSong &a, const NongSong &b) { return a.downloads > b.downloads; });
    if (songs.isEmpty() && !document.array().isEmpty() && error) *error = "SongFileHub returned no supported song records.";
    return songs;
}
void NongManager::fetchCatalog() {
    if (m_fetching) return;
    m_fetching = true;
    QNetworkRequest request{m_catalogUrl};
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(60000);
    auto *reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_fetching = false;
            emit catalogError(reply->errorString());
            return;
        }
        const QByteArray json = reply->readAll();
        auto *watcher = new QFutureWatcher<QPair<QVector<NongSong>, QString>>(this);
        connect(watcher, &QFutureWatcher<QPair<QVector<NongSong>, QString>>::finished, this, [this, watcher]() {
            const auto result = watcher->result();
            watcher->deleteLater();
            m_fetching = false;
            if (!result.second.isEmpty()) { emit catalogError(result.second); return; }
            m_songs = result.first;
            emit catalogReady();
        });
        watcher->setFuture(QtConcurrent::run([json]() {
            QString error;
            auto songs = parseCatalog(json, &error);
            return qMakePair(songs, error);
        }));
    });
}
bool NongManager::installFile(const QString &directory, const QString &id, const QString &format,
                              const QString &temporary, QString *error) {
    QVector<RenamedFile> renamed;
    if (!stageCurrent(directory, id, !hasOriginal(directory, id), &renamed, error)) return false;
    if (!QFile::rename(temporary, songPath(directory, id, format))) {
        const bool recovered = rollback(renamed);
        *error = "Could not install the downloaded song. The existing song was kept.";
        if (!recovered) *error += " Some files remain under their backup names in the song folder.";
        return false;
    }
    for (const auto &file : renamed) if (!file.preserve) QFile::remove(file.to);
    return true;
}
void NongManager::downloadSong(const NongSong &song, const QString &directory) {
    QString key = operationKey(directory, song.songId);
    if (m_operations.contains(key)) return;
    if (!validSongId(song.songId) || !formats.contains(song.format)) {
        emit downloadFinished(key, false, "Invalid song ID or format."); return;
    }
    if (!QDir().mkpath(directory)) { emit downloadFinished(key, false, "Could not create the Geometry Dash song folder."); return; }
    key = operationKey(directory, song.songId);
    if (m_operations.contains(key)) return;
    auto file = std::make_shared<QTemporaryFile>(QDir(directory).filePath(".nong-XXXXXX"));
    if (!file->open()) { emit downloadFinished(key, false, "Could not write to the Geometry Dash song folder."); return; }
    QNetworkRequest request(song.url);
    request.setTransferTimeout(60000);
    auto *reply = m_net.get(request);
    m_operations.insert(key, NongOperation{0, -1, reply});
    emit downloadStarted(key);
    auto writeFailed = std::make_shared<bool>(false);
    auto consume = [reply, file, writeFailed]() {
        if (reply->property("nongCancelled").toBool()) return;
        const auto bytes = reply->readAll();
        if (file->write(bytes) != bytes.size()) { *writeFailed = true; reply->abort(); }
    };
    connect(reply, &QNetworkReply::readyRead, this, consume);
    connect(reply, &QNetworkReply::downloadProgress, this, [this, key](qint64 received, qint64 total) {
        auto it = m_operations.find(key);
        if (it == m_operations.end()) return;
        it->received = received;
        it->total = total;
        emit downloadProgress(key, received, total);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, writeFailed, consume, song, directory, key]() {
        if (!m_operations.contains(key)) return;
        const bool cancelled = reply->property("nongCancelled").toBool();
        if (!cancelled && reply->error() == QNetworkReply::NoError) consume();
        const bool flushed = file->flush();
        m_operations.remove(key);
        reply->deleteLater();
        QString error;
        if (cancelled) error = "Cancelled";
        else if (*writeFailed || !flushed) error = "Could not write the song. Check available disk space.";
        else if (reply->error() == QNetworkReply::OperationCanceledError) error = "Cancelled";
        else if (reply->error() != QNetworkReply::NoError) error = reply->errorString();
        else {
            file->seek(0);
            const auto header = file->read(4);
            const bool ogg = header.startsWith("OggS");
            const bool mp3 = header.startsWith("ID3") || (header.size() >= 2 &&
                static_cast<unsigned char>(header[0]) == 0xff && (static_cast<unsigned char>(header[1]) & 0xe0) == 0xe0);
            if ((song.format == "ogg" && !ogg) || (song.format == "mp3" && !mp3)) error = "SongFileHub did not return a valid " + song.format.toUpper() + " file. Your existing song was kept.";
        }
        file->close();
        if (error.isEmpty()) installFile(directory, song.songId, song.format, file->fileName(), &error);
        emit downloadFinished(key, error.isEmpty(), error);
    });
}
void NongManager::cancelDownload(const QString &key) {
    if (auto *reply = m_operations.value(key).reply) {
        reply->setProperty("nongCancelled", true);
        reply->abort();
    }
}
bool NongManager::restoreOriginal(const QString &directory, const QString &id, QString *error) {
    if (!validSongId(id) || m_operations.contains(operationKey(directory, id)) || !hasOriginal(directory, id)) {
        *error = "No original song is available, or a download is still in progress.";
        return false;
    }
    QVector<RenamedFile> current;
    if (!stageCurrent(directory, id, false, &current, error)) return false;
    QVector<RenamedFile> originals;
    for (const auto &format : formats) {
        const QString backup = backupPath(directory, id, format);
        if (!QFileInfo::exists(backup)) continue;
        const QString target = songPath(directory, id, format);
        if (!QFile::rename(backup, target)) {
            const bool recoveredOriginals = rollback(originals);
            const bool recoveredCurrent = rollback(current);
            *error = "Could not restore the original song. Check folder permissions.";
            if (!recoveredOriginals || !recoveredCurrent) *error += " Some files remain under their backup names in the song folder.";
            return false;
        }
        originals.push_back({backup, target, true});
    }
    for (const auto &file : current) QFile::remove(file.to);
    return true;
}
