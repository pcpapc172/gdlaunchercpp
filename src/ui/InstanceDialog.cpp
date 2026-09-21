#include "InstanceDialog.h"
#include "../VersionManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QPushButton>
#include <QLabel>
#include <QButtonGroup>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QEventLoop>
#include <QUrl>
#include <QTimer>

InstanceDialog::InstanceDialog(QWidget *parent, bool editMode, const QJsonObject &existingData)
    : QDialog(parent), m_editMode(editMode), m_existing(existingData) {
    setWindowTitle(editMode ? "Edit Instance" : "Create Instance");
    setMinimumWidth(420);

    auto *root = new QVBoxLayout(this);

    auto *form = new QFormLayout();
    m_nameEdit = new QLineEdit(this);
    form->addRow("Instance Name:", m_nameEdit);

    auto *typeRow = new QHBoxLayout();
    m_localRadio = new QRadioButton("From Versions Folder", this);
    m_customRadio = new QRadioButton("Custom Executable", this);
    m_localRadio->setChecked(true);
    auto *group = new QButtonGroup(this);
    group->addButton(m_localRadio);
    group->addButton(m_customRadio);
    typeRow->addWidget(m_localRadio);
    typeRow->addWidget(m_customRadio);
    form->addRow("Version Type:", typeRow);

    m_localSection = new QWidget(this);
    auto *localLayout = new QHBoxLayout(m_localSection);
    localLayout->setContentsMargins(0, 0, 0, 0);
    m_localVersionCombo = new QComboBox(this);
    localLayout->addWidget(m_localVersionCombo);
    form->addRow("Version:", m_localSection);

    m_customSection = new QWidget(this);
    auto *customLayout = new QHBoxLayout(m_customSection);
    customLayout->setContentsMargins(0, 0, 0, 0);
    m_exePathEdit = new QLineEdit(this);
    m_exePathEdit->setReadOnly(true);
    auto *browseBtn = new QPushButton("Browse...", this);
    customLayout->addWidget(m_exePathEdit);
    customLayout->addWidget(browseBtn);
    form->addRow("Executable Path:", m_customSection);

    m_saveFolderEdit = new QLineEdit("GeometryDash", this);
    form->addRow("Save Folder Name:", m_saveFolderEdit);

    root->addLayout(form);

    m_geodeCheck = new QCheckBox("Geode Compatible", this);
    m_megahackCheck = new QCheckBox("Use MegaHack / BetterInfo Files", this);
    m_steamEmuCheck = new QCheckBox("Launch via SmartSteamEmu.exe", this);
    m_skipRestartCheck = new QCheckBox("Skip Restart Check (GEODE MIGHT CRASH)", this);
    for (QCheckBox *c : {m_geodeCheck, m_megahackCheck, m_steamEmuCheck, m_skipRestartCheck}) root->addWidget(c);

    m_geodeLogSection = new QWidget(this);
    auto *geodeLogLayout = new QVBoxLayout(m_geodeLogSection);
    geodeLogLayout->setContentsMargins(0, 8, 0, 0);
    m_geodeLogCheck = new QCheckBox("Enable Advanced Logging (Geode)", this);
    geodeLogLayout->addWidget(m_geodeLogCheck);
    m_geodeLogVersionSection = new QWidget(this);
    auto *verLayout = new QVBoxLayout(m_geodeLogVersionSection);
    verLayout->setContentsMargins(0, 4, 0, 0);
    verLayout->addWidget(new QLabel("Mod Version:", this));
    m_geodeLogVersionCombo = new QComboBox(this);
    verLayout->addWidget(m_geodeLogVersionCombo);
    geodeLogLayout->addWidget(m_geodeLogVersionSection);
    root->addWidget(m_geodeLogSection);

    m_openInEditorBtn = new QPushButton("Edit Save File in Editor", this);
    m_openInEditorBtn->setVisible(editMode);
    root->addWidget(m_openInEditorBtn);

    auto *btnRow = new QHBoxLayout();
    auto *cancelBtn = new QPushButton("Cancel", this);
    auto *saveBtn = new QPushButton("Save", this);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(saveBtn);
    root->addLayout(btnRow);

    connect(m_localRadio, &QRadioButton::toggled, this, &InstanceDialog::updateVersionTypeUI);
    connect(m_geodeCheck, &QCheckBox::toggled, this, &InstanceDialog::updateGeodeLogUI);
    connect(m_geodeLogCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_geodeLogVersionSection->setVisible(checked);
        if (checked && m_geodeLogVersionCombo->count() <= 1) fetchLogModVersions();
    });
    connect(browseBtn, &QPushButton::clicked, this, &InstanceDialog::browseForExe);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &InstanceDialog::onSave);
    connect(m_localVersionCombo, &QComboBox::currentTextChanged, this, &InstanceDialog::onLocalVersionChanged);
    connect(m_openInEditorBtn, &QPushButton::clicked, this, [this]() { emit openInEditorRequested(); accept(); });

    populateVersions();

    if (editMode) {
        m_nameEdit->setText(existingData.value("name").toString());
        const bool isLocal = existingData.value("versionType").toString("local") == "local";
        m_localRadio->setChecked(isLocal);
        m_customRadio->setChecked(!isLocal);
        if (isLocal) m_localVersionCombo->setCurrentText(existingData.value("version").toString());
        else m_exePathEdit->setText(existingData.value("executablePath").toString());
        m_saveFolderEdit->setText(existingData.value("saveFolderName").toString());
        m_geodeCheck->setChecked(existingData.value("isGeodeCompatible").toBool());
        m_megahackCheck->setChecked(existingData.value("useMegaHack").toBool());
        m_steamEmuCheck->setChecked(existingData.value("useSteamEmu").toBool());
        m_skipRestartCheck->setChecked(existingData.value("skipRestartCheck").toBool());
        m_geodeLogCheck->setChecked(existingData.value("enableGeodeLogging").toBool());
        if (m_geodeLogCheck->isChecked()) {
            fetchLogModVersions();
            m_geodeLogVersionCombo->setCurrentText(existingData.value("geodeLogModVersion").toString());
        }
    } else {
        m_saveFolderEdit->setText("GeometryDash");
    }

    updateVersionTypeUI();
    updateGeodeLogUI();
}

void InstanceDialog::populateVersions() {
    m_localVersionCombo->clear();
    m_localVersionCombo->addItem("Select a version...", "");
    for (const LocalVersion &v : VersionManager::getVersions()) m_localVersionCombo->addItem(v.path, v.path);
}

void InstanceDialog::updateVersionTypeUI() {
    const bool isLocal = m_localRadio->isChecked();
    m_localSection->setVisible(isLocal);
    m_customSection->setVisible(!isLocal);
    if (!m_editMode && isLocal) m_saveFolderEdit->setText("GeometryDash");
}

void InstanceDialog::updateGeodeLogUI() {
    const bool show = m_geodeCheck->isChecked();
    m_geodeLogSection->setVisible(show);
    if (!show) {
        m_geodeLogCheck->setChecked(false);
        m_geodeLogVersionSection->setVisible(false);
    }
}

void InstanceDialog::browseForExe() {
    const QString f = QFileDialog::getOpenFileName(this, "Select Executable", QString(), "Executables (*.exe)");
    if (f.isEmpty()) return;
    m_exePathEdit->setText(f);
    const QString folderName = QFileInfo(f).completeBaseName();
    m_saveFolderEdit->setText(folderName);
}

void InstanceDialog::fetchLogModVersions() {
    m_geodeLogVersionCombo->clear();
    m_geodeLogVersionCombo->addItem("Select version...", "");

    QNetworkAccessManager mgr;
    QNetworkRequest req{QUrl("https://api.github.com/repos/pcpapc172/gdlauncher-log/releases")};
    QNetworkReply *reply = mgr.get(req);
    QEventLoop loop;
    QTimer timeout; timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(8000);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        m_geodeLogVersionCombo->clear();
        m_geodeLogVersionCombo->addItem("Failed to load versions", "");
        reply->deleteLater();
        return;
    }
    const QJsonArray releases = QJsonDocument::fromJson(reply->readAll()).array();
    reply->deleteLater();
    for (const QJsonValue &rv : releases) {
        const QJsonObject release = rv.toObject();
        for (const QJsonValue &av : release.value("assets").toArray()) {
            const QJsonObject asset = av.toObject();
            if (asset.value("name").toString().endsWith(".geode")) {
                const QString tag = release.value("tag_name").toString();
                m_geodeLogVersionCombo->addItem(QString("%1 — %2").arg(tag, release.value("published_at").toString()), tag);
                break;
            }
        }
    }
}

void InstanceDialog::onLocalVersionChanged() {
    // In the original, version.json defaults only auto-fill when creating, not editing.
    if (m_editMode) return;
    loadVersionDefaults();
}

void InstanceDialog::loadVersionDefaults() {
    const QString selected = m_localVersionCombo->currentData().toString();
    if (selected.isEmpty()) return;
    const VersionDefaults defaults = VersionManager::getVersionDefaults(selected);
    if (!defaults.executable.isEmpty()) {
        QString folderName = defaults.executable;
        const int dot = folderName.lastIndexOf('.');
        if (dot > 0) folderName = folderName.left(dot);
        m_saveFolderEdit->setText(folderName);
    }
}

void InstanceDialog::onSave() {
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) { QMessageBox::warning(this, "Invalid name", "Invalid name."); return; }

    QJsonObject data;
    data["name"] = name;
    const bool isLocal = m_localRadio->isChecked();
    data["versionType"] = isLocal ? "local" : "custom";
    data["saveFolderName"] = m_saveFolderEdit->text().trimmed();
    data["isGeodeCompatible"] = m_geodeCheck->isChecked();
    data["useMegaHack"] = m_megahackCheck->isChecked();
    data["useSteamEmu"] = m_steamEmuCheck->isChecked();
    data["skipRestartCheck"] = m_skipRestartCheck->isChecked();
    data["enableGeodeLogging"] = m_geodeLogCheck->isChecked();
    data["geodeLogModVersion"] = m_geodeLogVersionCombo->currentData().toString();

    if (isLocal) {
        const QString v = m_localVersionCombo->currentData().toString();
        if (v.isEmpty()) { QMessageBox::warning(this, "Select version", "Select version."); return; }
        data["version"] = v;
        data["executablePath"] = QJsonValue();
    } else {
        if (m_exePathEdit->text().isEmpty()) { QMessageBox::warning(this, "Select executable", "Select executable."); return; }
        data["executablePath"] = m_exePathEdit->text();
        data["version"] = QJsonValue();
    }
    if (m_editMode) data["creationDate"] = m_existing.value("creationDate");

    m_result = data;
    accept();
}

QJsonObject InstanceDialog::data() const { return m_result; }
