#include "SettingsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>

SettingsDialog::SettingsDialog(QWidget *parent, const AppSettings &current, const QString &appVersion)
    : QDialog(parent) {
    setWindowTitle("Settings");
    setMinimumWidth(420);
    auto *root = new QVBoxLayout(this);

    auto *behaviorBox = new QGroupBox("Launcher Behavior", this);
    auto *behaviorLayout = new QVBoxLayout(behaviorBox);
    m_stayOpenRadio = new QRadioButton("Stay Open After Game Close", this);
    m_closeAfterRadio = new QRadioButton("⚠️ Close After Game Ends (Unsafe)", this);
    auto *closeGroup = new QButtonGroup(this);
    closeGroup->addButton(m_stayOpenRadio);
    closeGroup->addButton(m_closeAfterRadio);
    behaviorLayout->addWidget(m_stayOpenRadio);
    behaviorLayout->addWidget(m_closeAfterRadio);
    root->addWidget(behaviorBox);

    auto *appearanceBox = new QGroupBox("Appearance", this);
    auto *appearanceLayout = new QVBoxLayout(appearanceBox);
    m_lightRadio = new QRadioButton("Light Theme", this);
    m_darkRadio = new QRadioButton("Dark Theme", this);
    auto *themeGroup = new QButtonGroup(this);
    themeGroup->addButton(m_lightRadio);
    themeGroup->addButton(m_darkRadio);
    appearanceLayout->addWidget(m_lightRadio);
    appearanceLayout->addWidget(m_darkRadio);
    root->addWidget(appearanceBox);

    auto *advancedBox = new QGroupBox("Advanced", this);
    auto *advancedLayout = new QVBoxLayout(advancedBox);
    advancedLayout->addWidget(new QLabel("Post-Game Sync Delay (seconds):", this));
    m_syncDelaySpin = new QSpinBox(this);
    m_syncDelaySpin->setRange(0, 30);
    advancedLayout->addWidget(m_syncDelaySpin);
    auto *help = new QLabel("Set to 0 to disable restart detection (GEODE MIGHT CRASH).", this);
    help->setStyleSheet("opacity:0.7; font-size:12px;");
    advancedLayout->addWidget(help);
    m_logOutputCheck = new QCheckBox("Enable Log Output (shows a live terminal when launching)", this);
    advancedLayout->addWidget(m_logOutputCheck);
    root->addWidget(advancedBox);

    auto *footer = new QLabel(QString("Launcher created by pcpapc172 — v%1").arg(appVersion), this);
    footer->setAlignment(Qt::AlignCenter);
    root->addWidget(footer);

    auto *btnRow = new QHBoxLayout();
    auto *checkUpdatesBtn = new QPushButton("Check for Updates", this);
    auto *openDataBtn = new QPushButton("Open Data Folder", this);
    auto *saveBtn = new QPushButton("Save and Close", this);
    btnRow->addWidget(checkUpdatesBtn);
    btnRow->addWidget(openDataBtn);
    btnRow->addStretch();
    btnRow->addWidget(saveBtn);
    root->addLayout(btnRow);

    m_stayOpenRadio->setChecked(current.closeBehavior != "Close After Game Ends");
    m_closeAfterRadio->setChecked(current.closeBehavior == "Close After Game Ends");
    m_lightRadio->setChecked(current.theme != "Dark");
    m_darkRadio->setChecked(current.theme == "Dark");
    m_syncDelaySpin->setValue(current.syncDelay);
    m_logOutputCheck->setChecked(current.enableLogOutput);

    connect(checkUpdatesBtn, &QPushButton::clicked, this, &SettingsDialog::checkUpdatesRequested);
    connect(openDataBtn, &QPushButton::clicked, this, &SettingsDialog::openDataFolderRequested);
    connect(saveBtn, &QPushButton::clicked, this, [this, appVersion]() {
        m_result.theme = m_darkRadio->isChecked() ? "Dark" : "Light";
        m_result.closeBehavior = m_closeAfterRadio->isChecked() ? "Close After Game Ends" : "Stay Open";
        m_result.syncDelay = m_syncDelaySpin->value();
        m_result.enableLogOutput = m_logOutputCheck->isChecked();
        m_result.lastRunVersion = appVersion;
        accept();
    });
}
