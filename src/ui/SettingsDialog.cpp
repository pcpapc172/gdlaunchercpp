#include "SettingsDialog.h"
#include "Theme.h"
#include "Animations.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QIcon>
#include <QStyle>
#include <QApplication>

SettingsDialog::SettingsDialog(QWidget *parent, const AppSettings &current, const QString &appVersion)
    : QDialog(parent) {
    setWindowTitle("Settings");
    setWindowIcon(QIcon(":/icon.png"));
    setMinimumWidth(420);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin);
    root->setSpacing(UiMetrics::kSectionSpacing);

    auto *heading = new QLabel("Settings", this);
    heading->setObjectName("heading");
    root->addWidget(heading);

    auto *behaviorBox = new QGroupBox("Launcher Behavior", this);
    auto *behaviorLayout = new QVBoxLayout(behaviorBox);
    behaviorLayout->setSpacing(UiMetrics::kTightSpacing);
    m_stayOpenRadio = new QRadioButton("Stay Open After Game Close", this);
    m_closeAfterRadio = new QRadioButton("Close After Game Ends (Unsafe)", this);
    auto *closeGroup = new QButtonGroup(this);
    closeGroup->addButton(m_stayOpenRadio);
    closeGroup->addButton(m_closeAfterRadio);
    behaviorLayout->addWidget(m_stayOpenRadio);
    behaviorLayout->addWidget(m_closeAfterRadio);
    root->addWidget(behaviorBox);

    auto *appearanceBox = new QGroupBox("Appearance", this);
    auto *appearanceLayout = new QVBoxLayout(appearanceBox);
    appearanceLayout->setSpacing(UiMetrics::kTightSpacing);
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
    advancedLayout->setSpacing(UiMetrics::kTightSpacing);
    advancedLayout->addWidget(new QLabel("Post-Game Sync Delay (seconds):", this));
    m_syncDelaySpin = new QSpinBox(this);
    m_syncDelaySpin->setRange(0, 30);
    advancedLayout->addWidget(m_syncDelaySpin);
    auto *help = new QLabel("Set to 0 to disable restart detection (GEODE MIGHT CRASH).", this);
    help->setObjectName("subtext");
    help->setStyleSheet("font-size:12px;");
    advancedLayout->addWidget(help);
    m_logOutputCheck = new QCheckBox("Enable Log Output (shows a live terminal when launching)", this);
    advancedLayout->addWidget(m_logOutputCheck);

    advancedLayout->addWidget(new QLabel("How GDLauncher was installed (used for updates):", this));
    m_updatePackageTypeCombo = new QComboBox(this);
#if defined(Q_OS_WIN)
    m_updatePackageTypeCombo->addItem("Portable (.zip)", "portable");
    m_updatePackageTypeCombo->addItem("Installer (.exe)", "nsis");
    m_updatePackageTypeCombo->addItem("Windows Installer (.msi)", "msi");
#else
    m_updatePackageTypeCombo->addItem("Portable (.tar.gz)", "portable");
    m_updatePackageTypeCombo->addItem("Debian/Ubuntu Package (.deb)", "deb");
    m_updatePackageTypeCombo->addItem("Fedora/RHEL Package (.rpm)", "rpm");
#endif
    advancedLayout->addWidget(m_updatePackageTypeCombo);
    auto *updateHelp = new QLabel(
        "A portable copy downloads and extracts an update for you to move into place; an "
        "installer/package downloads it, closes GDLauncher, and runs it for you.", this);
    updateHelp->setObjectName("subtext");
    updateHelp->setStyleSheet("font-size:12px;");
    updateHelp->setWordWrap(true);
    advancedLayout->addWidget(updateHelp);

    root->addWidget(advancedBox);

    auto *footer = new QLabel(QString("Launcher created by pcpapc172 — v%1").arg(appVersion), this);
    footer->setObjectName("subtext");
    footer->setAlignment(Qt::AlignCenter);
    root->addWidget(footer);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(UiMetrics::kTightSpacing);
    QStyle *style = QApplication::style();
    auto *checkUpdatesBtn = new QPushButton(style->standardIcon(QStyle::SP_BrowserReload), "Check for Updates", this);
    auto *openDataBtn = new QPushButton(style->standardIcon(QStyle::SP_DirOpenIcon), "Open Data Folder", this);
    auto *saveBtn = new QPushButton("Save and Close", this);
    saveBtn->setObjectName("primary");
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
    {
        const int idx = m_updatePackageTypeCombo->findData(current.updatePackageType);
        m_updatePackageTypeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    connect(checkUpdatesBtn, &QPushButton::clicked, this, &SettingsDialog::checkUpdatesRequested);
    connect(openDataBtn, &QPushButton::clicked, this, &SettingsDialog::openDataFolderRequested);
    connect(saveBtn, &QPushButton::clicked, this, [this, appVersion]() {
        m_result.theme = m_darkRadio->isChecked() ? "Dark" : "Light";
        m_result.closeBehavior = m_closeAfterRadio->isChecked() ? "Close After Game Ends" : "Stay Open";
        m_result.syncDelay = m_syncDelaySpin->value();
        m_result.enableLogOutput = m_logOutputCheck->isChecked();
        m_result.updatePackageType = m_updatePackageTypeCombo->currentData().toString();
        m_result.lastRunVersion = appVersion;
        accept();
    });

    Animations::fadeIn(this);
}
