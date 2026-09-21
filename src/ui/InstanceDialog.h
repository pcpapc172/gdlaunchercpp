#pragma once
#include <QDialog>
#include <QJsonObject>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QRadioButton;
class QWidget;
class QPushButton;

// Port of the "Create/Edit Instance" modal from index.html/renderer.js.
class InstanceDialog : public QDialog {
    Q_OBJECT
public:
    explicit InstanceDialog(QWidget *parent, bool editMode, const QJsonObject &existingData = QJsonObject());

    QJsonObject data() const;

signals:
    void openInEditorRequested();

private slots:
    void updateVersionTypeUI();
    void updateGeodeLogUI();
    void browseForExe();
    void fetchLogModVersions();
    void onLocalVersionChanged();
    void onSave();

private:
    bool m_editMode;
    QJsonObject m_existing;

    QLineEdit *m_nameEdit;
    QRadioButton *m_localRadio;
    QRadioButton *m_customRadio;
    QWidget *m_localSection;
    QWidget *m_customSection;
    QComboBox *m_localVersionCombo;
    QLineEdit *m_exePathEdit;
    QLineEdit *m_saveFolderEdit;
    QCheckBox *m_geodeCheck;
    QCheckBox *m_megahackCheck;
    QCheckBox *m_steamEmuCheck;
    QCheckBox *m_skipRestartCheck;
    QWidget *m_geodeLogSection;
    QCheckBox *m_geodeLogCheck;
    QWidget *m_geodeLogVersionSection;
    QComboBox *m_geodeLogVersionCombo;
    QPushButton *m_openInEditorBtn;

    void populateVersions();
    void loadVersionDefaults();
    QJsonObject m_result;
};
