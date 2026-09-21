#include "SaveEditorDialog.h"
#include "../InstanceManager.h"
#include "../Settings.h"
#include "Theme.h"
#include "Animations.h"
#include "XmlHighlighter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QListWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QIcon>
#include <QStyle>
#include <QApplication>
#include <QFont>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

namespace {
const QStringList kOfficialSongs = {
    "Practice: Stay Inside Me - OcularNebula", "Stereo Madness - Foreverbound", "Back on Track - DJVI",
    "Polargeist - Step", "Dry Out - DJVI", "Base after Base - DJVI", "Cant Let Go - DJVI",
    "Jumper - Waterflame", "Time Machine - Waterflame", "Cycles - DJVI", "xStep - DJVI",
    "Clutterfunk - Waterflame", "Theory of Everything - DJ-Nate", "Electroman Adventures - Waterflame",
    "Clubstep - DJ-Nate", "Electrodynamix - DJ-Nate", "Hexagon Force - Waterflame",
    "Blast Processing - Waterflame", "Theory of Everything 2 - DJ-Nate", "Geometrical Dominator - Waterflame",
    "Deadlocked - F-777", "Fingerdash - MDK", "Dash - MDK", "Explorers - Hinkik",
    "The Seven Seas - F-777", "Viking Arena - F-777", "Airborne Robots - F-777", "Secret - RobTop",
    "Payload - Dex Arson", "Beast Mode - Dex Arson", "Machina - Dex Arson", "Years - Dex Arson",
    "Frontlines - Dex Arson", "Space Pirates - Waterflame", "Striker - Waterflame", "Embers - Dex Arson",
    "Round 1 - Dex Arson", "Monster Dance Off - F-777", "Press Start - MDK", "Nock Em - Bossfight",
    "Power Trip - Boom Kitty"
};
}

SaveEditorDialog::SaveEditorDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Save File Editor");
    setWindowIcon(QIcon(":/icon.png"));
    resize(900, 650);
    QStyle *style = QApplication::style();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin);
    root->setSpacing(UiMetrics::kSpacing);

    auto *heading = new QLabel("Save File Editor", this);
    heading->setObjectName("heading");
    root->addWidget(heading);

    auto *instanceRow = new QHBoxLayout();
    instanceRow->addWidget(new QLabel("Select Instance:", this));
    m_instanceCombo = new QComboBox(this);
    instanceRow->addWidget(m_instanceCombo, 1);
    root->addLayout(instanceRow);

    auto *split = new QHBoxLayout();
    m_levelsList = new QListWidget(this);
    m_levelsList->setMaximumWidth(280);
    split->addWidget(m_levelsList);

    m_actionsPanel = new QWidget(this);
    auto *panelLayout = new QVBoxLayout(m_actionsPanel);

    auto *nameRow = new QHBoxLayout();
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setReadOnly(true);
    auto *renameBtn = new QPushButton("Rename", this);
    nameRow->addWidget(m_nameEdit);
    nameRow->addWidget(renameBtn);
    panelLayout->addLayout(nameRow);

    auto *descRow = new QHBoxLayout();
    m_descEdit = new QLineEdit(this);
    m_descEdit->setReadOnly(true);
    auto *descBtn = new QPushButton("Edit Desc", this);
    descRow->addWidget(m_descEdit);
    descRow->addWidget(descBtn);
    panelLayout->addLayout(descRow);

    m_customSongCheck = new QCheckBox("Custom Song (Newgrounds)", this);
    panelLayout->addWidget(m_customSongCheck);

    m_officialSongSection = new QWidget(this);
    auto *officialLayout = new QVBoxLayout(m_officialSongSection);
    officialLayout->setContentsMargins(0, 0, 0, 0);
    m_officialSongCombo = new QComboBox(this);
    for (int i = 0; i < kOfficialSongs.size(); ++i) m_officialSongCombo->addItem(kOfficialSongs[i], i - 1);
    officialLayout->addWidget(m_officialSongCombo);
    panelLayout->addWidget(m_officialSongSection);

    m_customSongSection = new QWidget(this);
    auto *customLayout = new QVBoxLayout(m_customSongSection);
    customLayout->setContentsMargins(0, 0, 0, 0);
    m_customSongIdEdit = new QLineEdit(this);
    m_customSongIdEdit->setPlaceholderText("Newgrounds Song ID (1-99999999)");
    customLayout->addWidget(m_customSongIdEdit);
    m_customSongSection->setVisible(false);
    panelLayout->addWidget(m_customSongSection);

    auto *applySongBtn = new QPushButton("Apply Song", this);
    panelLayout->addWidget(applySongBtn);

    panelLayout->addWidget(new QLabel("Requested Stars:", this));
    m_starRequestCombo = new QComboBox(this);
    m_starRequestCombo->addItem("Auto (No Request)", 0);
    for (int i = 1; i <= 10; ++i) m_starRequestCombo->addItem(QString("%1 Star(s)").arg(i), i);
    panelLayout->addWidget(m_starRequestCombo);

    auto *exportRow = new QHBoxLayout();
    auto *exportTxtBtn = new QPushButton("Export .txt", this);
    auto *exportGmdBtn = new QPushButton("Export .gmd", this);
    exportRow->addWidget(exportTxtBtn);
    exportRow->addWidget(exportGmdBtn);
    panelLayout->addLayout(exportRow);

    auto *importReplaceBtn = new QPushButton(style->standardIcon(QStyle::SP_BrowserReload), "Import (Replace)", this);
    panelLayout->addWidget(importReplaceBtn);
    panelLayout->addStretch();

    m_actionsPanel->setVisible(false);
    split->addWidget(m_actionsPanel, 1);
    root->addLayout(split);

    auto *bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(UiMetrics::kTightSpacing);
    auto *importNewBtn = new QPushButton(style->standardIcon(QStyle::SP_FileDialogNewFolder), "Import New", this);
    auto *editRawBtn = new QPushButton(style->standardIcon(QStyle::SP_FileDialogDetailedView), "Edit Raw XML", this);
    auto *saveBtn = new QPushButton("Save", this);
    saveBtn->setObjectName("primary");
    auto *saveExitBtn = new QPushButton("Save & Exit", this);
    saveExitBtn->setObjectName("primary");
    auto *exitBtn = new QPushButton("Exit without Saving", this);
    exitBtn->setObjectName("danger");
    bottomRow->addWidget(importNewBtn);
    bottomRow->addWidget(editRawBtn);
    bottomRow->addStretch();
    bottomRow->addWidget(saveBtn);
    bottomRow->addWidget(saveExitBtn);
    bottomRow->addWidget(exitBtn);
    root->addLayout(bottomRow);

    for (const InstanceInfo &inst : InstanceManager::getInstances()) m_instanceCombo->addItem(inst.name);

    connect(m_instanceCombo, &QComboBox::currentTextChanged, this, &SaveEditorDialog::onInstanceChanged);
    connect(m_levelsList, &QListWidget::currentRowChanged, this, &SaveEditorDialog::onLevelSelected);
    connect(renameBtn, &QPushButton::clicked, this, &SaveEditorDialog::onRename);
    connect(descBtn, &QPushButton::clicked, this, &SaveEditorDialog::onEditDesc);
    connect(applySongBtn, &QPushButton::clicked, this, &SaveEditorDialog::onApplySong);
    connect(m_starRequestCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SaveEditorDialog::onApplyStarRequest);
    connect(m_customSongCheck, &QCheckBox::toggled, this, &SaveEditorDialog::toggleSongType);
    connect(exportTxtBtn, &QPushButton::clicked, this, [this]() { onExport("txt"); });
    connect(exportGmdBtn, &QPushButton::clicked, this, [this]() { onExport("gmd"); });
    connect(importReplaceBtn, &QPushButton::clicked, this, &SaveEditorDialog::onImportReplace);
    connect(importNewBtn, &QPushButton::clicked, this, &SaveEditorDialog::onImportNew);
    connect(editRawBtn, &QPushButton::clicked, this, &SaveEditorDialog::onEditRaw);
    connect(saveBtn, &QPushButton::clicked, this, [this]() { onSave(false); });
    connect(saveExitBtn, &QPushButton::clicked, this, [this]() { onSave(true); });
    connect(exitBtn, &QPushButton::clicked, this, &QDialog::reject);

    if (m_instanceCombo->count() > 0) onInstanceChanged();

    Animations::fadeIn(this);
}

void SaveEditorDialog::selectInstance(const QString &name) {
    m_instanceCombo->setCurrentText(name);
    onInstanceChanged();
}

void SaveEditorDialog::onInstanceChanged() {
    const QString name = m_instanceCombo->currentText();
    if (name.isEmpty()) return;
    const SaveEditorResult res = SaveEditor::initSession(Settings::instancesDir(), name);
    if (!res.success) {
        QMessageBox::warning(this, "Error", res.error);
        m_actionsPanel->setVisible(false);
        return;
    }
    reloadLevels();
}

void SaveEditorDialog::reloadLevels() {
    m_levelsList->clear();
    m_levels = SaveEditor::getLevels();
    for (const EditorLevel &lvl : m_levels) m_levelsList->addItem(lvl.name);
    m_actionsPanel->setVisible(false);
}

void SaveEditorDialog::onLevelSelected() {
    const int row = m_levelsList->currentRow();
    if (row < 0 || row >= m_levels.size()) return;
    const EditorLevel &lvl = m_levels[row];
    m_selectedKey = lvl.key;

    m_nameEdit->setText(lvl.name);
    m_descEdit->setText(QByteArray::fromBase64(lvl.description.toUtf8()));
    m_customSongCheck->setChecked(lvl.isCustomSong);
    toggleSongType();
    if (lvl.isCustomSong) m_customSongIdEdit->setText(QString::number(lvl.songId));
    else m_officialSongCombo->setCurrentIndex(m_officialSongCombo->findData(lvl.songId));
    m_starRequestCombo->setCurrentIndex(static_cast<int>(lvl.starRequest));

    m_actionsPanel->setVisible(true);
}

void SaveEditorDialog::toggleSongType() {
    const bool isCustom = m_customSongCheck->isChecked();
    m_officialSongSection->setVisible(!isCustom);
    m_customSongSection->setVisible(isCustom);
}

void SaveEditorDialog::onRename() {
    if (m_selectedKey.isEmpty()) return;
    bool ok = false;
    const QString newName = QInputDialog::getText(this, "Rename Level", "New Name:", QLineEdit::Normal, m_nameEdit->text(), &ok);
    if (!ok || newName.trimmed().isEmpty()) return;
    if (SaveEditor::renameLevel(m_selectedKey, newName.trimmed()).success) {
        reloadLevels();
        QMessageBox::information(this, "Renamed", "Level renamed! Click Save to commit changes.");
    }
}

void SaveEditorDialog::onEditDesc() {
    if (m_selectedKey.isEmpty()) return;
    bool ok = false;
    const QString newDesc = QInputDialog::getMultiLineText(this, "Edit Description", "Description:", m_descEdit->text(), &ok);
    if (!ok) return;
    if (newDesc.length() > 140) { QMessageBox::warning(this, "Too long", "Description is too long (max 140 characters)."); return; }
    const QString encoded = QString::fromLatin1(newDesc.toUtf8().toBase64());
    if (SaveEditor::updateDescription(m_selectedKey, encoded).success) {
        m_descEdit->setText(newDesc);
        QMessageBox::information(this, "Updated", "Description updated! Click Save to commit changes.");
    }
}

void SaveEditorDialog::onApplySong() {
    if (m_selectedKey.isEmpty()) return;
    const bool isCustom = m_customSongCheck->isChecked();
    qint64 songId = 0;
    if (isCustom) {
        bool ok = false;
        songId = m_customSongIdEdit->text().toLongLong(&ok);
        if (!ok || songId < 1 || songId > 99999999) {
            QMessageBox::warning(this, "Invalid song", "Please enter a valid Newgrounds song ID (1-99999999)");
            return;
        }
    } else {
        songId = m_officialSongCombo->currentData().toLongLong();
    }
    if (SaveEditor::setSong(m_selectedKey, songId, isCustom).success) {
        QMessageBox::information(this, "Updated", "Song updated! Click Save to commit changes.");
        reloadLevels();
    }
}

void SaveEditorDialog::onApplyStarRequest() {
    if (m_selectedKey.isEmpty()) return;
    SaveEditor::updateStarRequest(m_selectedKey, m_starRequestCombo->currentData().toLongLong());
}

void SaveEditorDialog::onExport(const QString &fmt) {
    if (m_selectedKey.isEmpty()) return;
    const QString path = QFileDialog::getSaveFileName(this, "Export Level", m_nameEdit->text() + "." + fmt,
                                                        QString("%1 files (*.%2)").arg(fmt.toUpper(), fmt));
    if (path.isEmpty()) return;
    const SaveEditorResult res = SaveEditor::exportLevel(m_selectedKey, fmt, path);
    if (res.success) QMessageBox::information(this, "Exported", "Exported!");
    else QMessageBox::warning(this, "Export failed", res.error);
}

void SaveEditorDialog::onImportReplace() { onImportNew(); }

void SaveEditorDialog::onImportNew() {
    const QString path = QFileDialog::getOpenFileName(this, "Import Level", QString(), "GD Levels (*.txt *.gmd)");
    if (path.isEmpty()) return;
    const bool replacing = sender() && sender()->objectName() == "importReplace";
    const QString key = replacing && !m_selectedKey.isEmpty() ? m_selectedKey : "new";
    const SaveEditorResult res = SaveEditor::importLevel(key, path);
    if (res.success) {
        reloadLevels();
        QMessageBox::information(this, "Imported", "Imported to session! (Click Save to commit)");
    } else {
        QMessageBox::warning(this, "Import failed", res.error);
    }
}

void SaveEditorDialog::onEditRaw() {
    // This edits the WHOLE save session (every level's dict, exactly what the Electron build's
    // Monaco editor showed via editor-get-xml/editor-save-xml) -- not just the currently
    // selected level. A single level's raw data is edited via getRaw()/saveAll() elsewhere
    // (the "raw" the level list exposes for import/export), which is a different, narrower op.
    if (!SaveEditor::hasSession()) return;

    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("Raw Save Data Editor");
    dlg->setWindowIcon(QIcon(":/icon.png"));
    dlg->resize(900, 650);
    auto *layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin);
    layout->setSpacing(UiMetrics::kSpacing);

    auto *edit = new QPlainTextEdit(dlg);
    edit->setReadOnly(true);
    edit->setPlainText("Loading full save data...");
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    // A VS Code Dark+-styled surface regardless of the app's own theme, since that's what the
    // highlighter's colors are tuned for.
    edit->setStyleSheet("QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; "
                         "border: 1px solid #3c3c3c; border-radius: 6px; padding: 8px; }");
    QFont monoFont("Consolas");
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSize(10);
    edit->setFont(monoFont);
    new XmlHighlighter(edit->document());
    layout->addWidget(edit, 1);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    box->button(QDialogButtonBox::Ok)->setText("Apply Changes");
    box->button(QDialogButtonBox::Ok)->setObjectName("primary");
    box->setEnabled(false);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, dlg, [this, dlg, edit, box]() {
        box->setEnabled(false);
        edit->setReadOnly(true);
        const QString newXml = edit->toPlainText();

        auto *watcher = new QFutureWatcher<SaveEditorResult>(dlg);
        connect(watcher, &QFutureWatcher<SaveEditorResult>::finished, dlg, [this, dlg, box, watcher]() {
            const SaveEditorResult res = watcher->result();
            watcher->deleteLater();
            if (res.success) {
                dlg->accept();
                reloadLevels();
                QMessageBox::information(this, "Updated", "Save data updated in session! (Click Save to commit to disk)");
            } else {
                QMessageBox::warning(this, "Failed to parse XML", res.error);
                box->setEnabled(true);
            }
        });
        watcher->setFuture(QtConcurrent::run([newXml]() { return SaveEditor::saveXml(newXml); }));
    });

    // buildPretty() over the whole save can take a moment for large saves with many custom
    // levels (their raw level strings are embedded inline), so it runs off the UI thread --
    // this is what was freezing the app before.
    auto *loadWatcher = new QFutureWatcher<QPair<bool, QString>>(dlg);
    connect(loadWatcher, &QFutureWatcher<QPair<bool, QString>>::finished, dlg, [edit, box, loadWatcher]() {
        const auto result = loadWatcher->result();
        loadWatcher->deleteLater();
        if (result.first) {
            edit->setPlainText(result.second);
            edit->setReadOnly(false);
            box->setEnabled(true);
        } else {
            edit->setPlainText("Failed to load save data.");
        }
    });
    loadWatcher->setFuture(QtConcurrent::run([]() {
        QString xml;
        const SaveEditorResult res = SaveEditor::getXml(&xml);
        return qMakePair(res.success, xml);
    }));

    dlg->exec();
}

void SaveEditorDialog::onSaveRaw() {}

void SaveEditorDialog::onSave(bool closeAfter) {
    const SaveEditorResult res = SaveEditor::persist(Settings::instancesDir());
    if (res.success) {
        QMessageBox::information(this, "Saved", "Session saved to disk!");
        if (closeAfter) accept();
    } else {
        QMessageBox::warning(this, "Save failed", res.error);
    }
}
