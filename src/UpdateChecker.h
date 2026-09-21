#pragma once
#include <QObject>
#include <QString>
#include <QWidget>
#include <QNetworkAccessManager>

// Port of checkForUpdates/installRpm/isFedora/isRpmOstree from main.js.
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
    static bool isFedora();
    static bool isRpmOstree();
    static bool installRpm(const QString &rpmPath);
    // Very small semver-ish compare: returns true if a > b.
    static bool versionGreater(const QString &a, const QString &b);
};
