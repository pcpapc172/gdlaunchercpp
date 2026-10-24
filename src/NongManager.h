#pragma once
#include <QObject>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVector>
#include <QUrl>

struct NongSong {
    QString recordId, songId, name, description, format, levels, credit, searchText;
    QUrl url;
    qint64 downloads = 0;
};
struct NongOperation {
    qint64 received = 0;
    qint64 total = -1;
    QNetworkReply *reply = nullptr;
};

// Owned by MainWindow: catalogue and downloads survive closing the dialog.
class NongManager : public QObject {
    Q_OBJECT
public:
    explicit NongManager(QObject *parent = nullptr,
                         const QUrl &catalogUrl = QUrl("https://api.songfilehub.com/v2/songs"));
    const QVector<NongSong> &songs() const { return m_songs; }
    void fetchCatalog();
    bool isFetching() const { return m_fetching; }
    static QVector<NongSong> parseCatalog(const QByteArray &json, QString *error = nullptr);
    static QString defaultDirectory();
    static QString resolveDirectory(const QString &input);
    static QString operationKey(const QString &directory, const QString &songId);
    static bool validSongId(const QString &id);
    static bool hasSong(const QString &directory, const QString &songId);
    static bool hasOriginal(const QString &directory, const QString &songId);
    bool isDownloading(const QString &key) const { return m_operations.contains(key); }
    NongOperation operation(const QString &key) const { return m_operations.value(key); }
    void downloadSong(const NongSong &song, const QString &directory);
    void cancelDownload(const QString &key);
    bool restoreOriginal(const QString &directory, const QString &songId, QString *error);

signals:
    void catalogReady();
    void catalogError(const QString &error);
    void downloadStarted(const QString &key);
    void downloadProgress(const QString &key, qint64 received, qint64 total);
    void downloadFinished(const QString &key, bool success, const QString &message);

private:
    QNetworkAccessManager m_net;
    QUrl m_catalogUrl;
    QVector<NongSong> m_songs;
    QHash<QString, NongOperation> m_operations;
    bool m_fetching = false;
    static bool installFile(const QString &directory, const QString &songId,
                            const QString &format, const QString &temporary, QString *error);
};
