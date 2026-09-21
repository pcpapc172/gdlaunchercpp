#pragma once
#include <QDialog>
#include "../gd/SaveEditor.h"

class QComboBox;
class QListWidget;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QPushButton;
class QWidget;

// Port of the Save File Editor modal from index.html/renderer.js (backed by
// editor.js / SaveEditor). The raw-XML "Monaco" editor is a plain text editor
// here rather than a full code editor widget, to keep the app lightweight.
class SaveEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit SaveEditorDialog(QWidget *parent);

    void selectInstance(const QString &name);

private slots:
    void onInstanceChanged();
    void onLevelSelected();
    void onRename();
    void onEditDesc();
    void onApplySong();
    void onApplyStarRequest();
    void onExport(const QString &fmt);
    void onImportReplace();
    void onImportNew();
    void onEditRaw();
    void onSaveRaw();
    void onSave(bool closeAfter);

private:
    QComboBox *m_instanceCombo;
    QListWidget *m_levelsList;
    QWidget *m_actionsPanel;
    QLineEdit *m_nameEdit;
    QLineEdit *m_descEdit;
    QCheckBox *m_customSongCheck;
    QWidget *m_officialSongSection;
    QWidget *m_customSongSection;
    QComboBox *m_officialSongCombo;
    QLineEdit *m_customSongIdEdit;
    QComboBox *m_starRequestCombo;

    QString m_selectedKey;
    QVector<EditorLevel> m_levels;

    void reloadLevels();
    void toggleSongType();
};
