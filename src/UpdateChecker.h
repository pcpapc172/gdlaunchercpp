#pragma once
#include <QObject>
#include <QString>
#include <QWidget>
#include <QNetworkAccessManager>

// Checks the gdlaunchercpp GitHub releases for a newer version and downloads the asset that
// matches Settings::updatePackageType: a portable zip/tar.gz (extracted for the user to move
// into place) by default, or -- if the user says they installed via NSIS/MSI/.deb/.rpm -- the
// real installer/package, which is downloaded, launched, and followed by closing this app.
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
    // Picks the release asset extension to look for based on Settings::updatePackageType
    // ("portable" -> zip/tar.gz, "nsis" -> .exe, "msi" -> .msi, "deb" -> .deb, "rpm" -> .rpm).
    static QString assetExtensionForPackageType(const QString &packageType);
};
