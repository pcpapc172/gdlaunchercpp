#pragma once
#include <QObject>
#include <QString>
#include <QWidget>
#include <QNetworkAccessManager>

// Checks the gdlaunchercpp GitHub releases for a newer version, downloads the matching
// platform archive, and extracts it for the user to install manually (we publish plain
// zip/tar.gz builds, not installers).
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QWidget *dialogParent, QObject *parent = nullptr);

    void check(bool isManual);

signals:
    void statusUpdate(const QString &message);
    void progressStart();
    void progress(int percentage);
    void progressComplete();

private:
    QWidget *m_dialogParent;
    QNetworkAccessManager m_net;

    static bool isLinux();
    // Very small semver-ish compare: returns true if a > b.
    static bool versionGreater(const QString &a, const QString &b);
};
