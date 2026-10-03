#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <memory>

// Downloads the official GE-Proton and portable UMU releases once, then reuses them.
class ProtonManager : public QObject {
    Q_OBJECT
public:
    explicit ProtonManager(QObject *parent = nullptr);
    void ensureReady();
    static QString protonDir();
    static QString launcherPath();
    static QString prefixDir();
    static QProcessEnvironment environment();

signals:
    void statusUpdate(const QString &message);
    void progress(int percentage); // -1 means indeterminate
    void finished(bool success, const QString &error);

private:
    QNetworkAccessManager m_net;
    std::unique_ptr<QTemporaryDir> m_staging;
    bool m_busy = false;
    void ensureComponent(bool proton);
    void download(const QString &url, bool proton);
    void extract(bool proton);
    void initializePrefix();
    void finish(bool success, const QString &error = QString());
};
