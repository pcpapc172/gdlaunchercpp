#pragma once
#include <QDialog>
#include "../Settings.h"

class QRadioButton;
class QSpinBox;
class QCheckBox;

// Port of the Settings modal from index.html/renderer.js.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent, const AppSettings &current, const QString &appVersion);

    AppSettings resultSettings() const { return m_result; }

signals:
    void checkUpdatesRequested();
    void openDataFolderRequested();

private:
    AppSettings m_result;
    QRadioButton *m_stayOpenRadio;
    QRadioButton *m_closeAfterRadio;
    QRadioButton *m_lightRadio;
    QRadioButton *m_darkRadio;
    QSpinBox *m_syncDelaySpin;
    QCheckBox *m_logOutputCheck;
};
