#pragma once
#include <QDialog>

// Port of the "What's New" changelog modal from index.html/renderer.js,
// shown whenever the app's version has changed since the last run.
class ChangelogDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChangelogDialog(QWidget *parent, const QString &appVersion, const QStringList &entries);
};
