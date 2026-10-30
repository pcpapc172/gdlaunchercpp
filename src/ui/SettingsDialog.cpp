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
#include <QDesktopServices>
#include <QUrl>
#include <QScrollArea>
#include <QLineEdit>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include "BackupsDialog.h"

SettingsDialog::SettingsDialog(QWidget *parent, const AppSettings &current, const QString &appVersion)
    : QDialog(parent), m_result(current) {
    setWindowTitle("Settings");
    setWindowIcon(QIcon(":/icon.png"));
    setMinimumSize(560, 500);
    resize(650, 780);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin);
    root->setSpacing(UiMetrics::kSectionSpacing);

    auto *heading = new QLabel("Settings", this);
    heading->setObjectName("heading");
    root->addWidget(heading);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget(scroll);
    auto *content = new QVBoxLayout(body);
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(UiMetrics::kSectionSpacing);
    scroll->setWidget(body);
    root->addWidget(scroll, 1);

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
    content->addWidget(behaviorBox);

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
    content->addWidget(appearanceBox);

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
    m_logOutputCheck = new QCheckBox("Enable Log Output (opens the log console with GDLauncher)", this);
    advancedLayout->addWidget(m_logOutputCheck);
#ifdef Q_OS_LINUX
    m_protonGECheck = new QCheckBox("Download GE-Proton and launch with UMULauncher", this);
    m_protonGECheck->setChecked(current.useProtonGE);
    advancedLayout->addWidget(m_protonGECheck);
    auto *protonHelp = new QLabel("Downloads GE-Proton automatically on first launch. May improve performance and compatibility.", this);
    protonHelp->setObjectName("subtext");
    protonHelp->setWordWrap(true);
    advancedLayout->addWidget(protonHelp);
#endif

    auto *installation = new QLabel("Installation detected automatically: " + Settings::installationType(), this);
    advancedLayout->addWidget(installation);
    auto *updateHelp = new QLabel(
        "A portable copy downloads and extracts an update for you to move into place; an "
        "installer/package downloads it, closes GDLauncher, and runs it for you.", this);
    updateHelp->setObjectName("subtext");
    updateHelp->setStyleSheet("font-size:12px;");
    updateHelp->setWordWrap(true);
    advancedLayout->addWidget(updateHelp);

    content->addWidget(advancedBox);

    auto *protection = new QGroupBox("Close Protection", this);
    auto *protectionLayout = new QVBoxLayout(protection);
    m_closeGuardCheck = new QCheckBox("Block closing while a game or file operation is active", this);
    m_closeGuardCheck->setObjectName("closeGuardEnabled");
    m_allowForceCheck = new QCheckBox("Offer Force close", this);
    m_allowForceCheck->setObjectName("allowForceClose");
    m_doubleConfirmCheck = new QCheckBox("Require a second confirmation before force closing", this);
    m_doubleConfirmCheck->setObjectName("confirmForceCloseTwice");
    m_closeGuardCheck->setChecked(current.closeGuardEnabled);
    m_allowForceCheck->setChecked(current.allowForceClose);
    m_doubleConfirmCheck->setChecked(current.confirmForceCloseTwice);
    for (auto *check : {m_closeGuardCheck, m_allowForceCheck, m_doubleConfirmCheck}) protectionLayout->addWidget(check);
    auto updateProtection = [this]() {
        m_allowForceCheck->setEnabled(m_closeGuardCheck->isChecked());
        m_doubleConfirmCheck->setEnabled(m_closeGuardCheck->isChecked() && m_allowForceCheck->isChecked());
    };
    connect(m_closeGuardCheck, &QCheckBox::toggled, this, updateProtection);
    connect(m_allowForceCheck, &QCheckBox::toggled, this, updateProtection);
    updateProtection();
    content->addWidget(protection);

    auto *backups = new QGroupBox("Instance Backups", this);
    auto *backupLayout = new QVBoxLayout(backups);
    m_backupsCheck = new QCheckBox("Back up an instance before launching it", this);
    m_backupsCheck->setObjectName("backupsEnabled");
    m_backupsCheck->setChecked(current.backupsEnabled);
    backupLayout->addWidget(m_backupsCheck);
    auto *backupOptions = new QWidget(backups);
    auto *backupForm = new QFormLayout(backupOptions);
    m_backupFrequency = new QComboBox(this);
    m_backupFrequency->setObjectName("backupFrequency");
    m_backupFrequency->addItem("Once per day (first launch)", 1);
    m_backupFrequency->addItem("Every launch", 0);
    m_backupFrequency->addItem("Once every 7 days", 7);
    m_backupFrequency->addItem("Custom interval", -1);
    int frequency = m_backupFrequency->findData(current.backupIntervalDays);
    m_backupFrequency->setCurrentIndex(frequency >= 0 ? frequency : 3);
    backupForm->addRow("Frequency:", m_backupFrequency);
    m_backupDays = new QSpinBox(this);
    m_backupDays->setObjectName("backupIntervalDays");
    m_backupDays->setRange(1, 3650);
    m_backupDays->setSuffix(" days");
    m_backupDays->setValue(qMax(1, current.backupIntervalDays));
    backupForm->addRow("Custom interval:", m_backupDays);
    connect(m_backupFrequency, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        m_backupDays->setEnabled(m_backupFrequency->currentData().toInt() == -1);
    });
    m_backupDays->setEnabled(frequency < 0);
    m_backupScope = new QComboBox(this);
    m_backupScope->setObjectName("backupScope");
    m_backupScope->addItem("Entire instance", "instance");
    m_backupScope->addItem("Managed saves and instance settings", "managed");
    m_backupScope->setCurrentIndex(current.backupScope == "managed" ? 1 : 0);
    backupForm->addRow("Include:", m_backupScope);
    m_backupKeep = new QSpinBox(this);
    m_backupKeep->setObjectName("backupKeepCount");
    m_backupKeep->setRange(0, 10000);
    m_backupKeep->setSpecialValueText("Keep all backups");
    m_backupKeep->setValue(current.backupKeepCount);
    backupForm->addRow("Keep latest per instance:", m_backupKeep);
    auto *folderRow = new QHBoxLayout;
    m_backupFolder = new QLineEdit(current.backupDirectory, this);
    m_backupFolder->setObjectName("backupDirectory");
    m_backupFolder->setPlaceholderText(Settings::baseDir() + "/Backups");
    auto *browseBackups = new QPushButton("Browse...", this);
    auto *defaultBackups = new QPushButton("Default", this);
    folderRow->addWidget(m_backupFolder, 1);
    folderRow->addWidget(browseBackups);
    folderRow->addWidget(defaultBackups);
    backupForm->addRow("Backup folder:", folderRow);
    connect(browseBackups, &QPushButton::clicked, this, [this]() {
        const auto directory = QFileDialog::getExistingDirectory(this, "Choose backup folder", Settings::backupsDir());
        if (!directory.isEmpty()) m_backupFolder->setText(directory);
    });
    connect(defaultBackups, &QPushButton::clicked, this, [this]() { m_backupFolder->clear(); });
    backupOptions->setEnabled(current.backupsEnabled);
    connect(m_backupsCheck, &QCheckBox::toggled, backupOptions, &QWidget::setEnabled);
    backupLayout->addWidget(backupOptions);
    auto *backupHelp = new QLabel("Each instance has its own schedule. Backups are saved as name-timestamp folders. "
        "If a limit is set, older completed backups are deleted only after a new backup succeeds.", this);
    backupHelp->setWordWrap(true);
    backupHelp->setObjectName("subtext");
    backupLayout->addWidget(backupHelp);
    auto *manageBackups = new QPushButton("Backups...", this);
    manageBackups->setObjectName("manageBackups");
    manageBackups->setToolTip("Browse, restore, fork, or delete backups in the saved backup folder.");
    connect(manageBackups, &QPushButton::clicked, this, [this]() {
        BackupsDialog dialog(this);
        dialog.exec();
    });
    content->addWidget(backups);
    content->addStretch();

    auto *footer = new QLabel(QString("Launcher created by pcpapc172 — v%1").arg(appVersion), this);
    footer->setObjectName("subtext");
    footer->setAlignment(Qt::AlignCenter);
    root->addWidget(footer);

    auto *sourceBtn = new QPushButton(QIcon(current.theme == "Dark" ? ":/github-light.svg" : ":/github-dark.svg"), "Source code", this);
    sourceBtn->setObjectName("sourceCodeButton");
    sourceBtn->setIconSize(QSize(20, 20));
    sourceBtn->setToolTip("View GDLauncher on GitHub");
    auto *links = new QHBoxLayout;
    links->addWidget(sourceBtn, 1);
    links->addWidget(manageBackups);
    root->addLayout(links);
    connect(sourceBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/pcpapc172/gdlaunchercpp"));
    });

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


    connect(checkUpdatesBtn, &QPushButton::clicked, this, &SettingsDialog::checkUpdatesRequested);
    connect(openDataBtn, &QPushButton::clicked, this, &SettingsDialog::openDataFolderRequested);
    connect(saveBtn, &QPushButton::clicked, this, [this, appVersion, current]() {
        const auto folder = QDir::fromNativeSeparators(m_backupFolder->text().trimmed());
        if (!folder.isEmpty() && !QDir::isAbsolutePath(folder)) {
            QMessageBox::warning(this, "Invalid Backup Folder", "Choose an absolute backup folder, or leave the field empty for the default.");
            return;
        }
        m_result.theme = m_darkRadio->isChecked() ? "Dark" : "Light";
        m_result.closeBehavior = m_closeAfterRadio->isChecked() ? "Close After Game Ends" : "Stay Open";
        m_result.syncDelay = m_syncDelaySpin->value();
        m_result.enableLogOutput = m_logOutputCheck->isChecked();
        m_result.useProtonGE = m_protonGECheck ? m_protonGECheck->isChecked() : current.useProtonGE;
        m_result.closeGuardEnabled = m_closeGuardCheck->isChecked();
        m_result.allowForceClose = m_allowForceCheck->isChecked();
        m_result.confirmForceCloseTwice = m_doubleConfirmCheck->isChecked();
        m_result.backupsEnabled = m_backupsCheck->isChecked();
        m_result.backupIntervalDays = m_backupFrequency->currentData().toInt() < 0
            ? m_backupDays->value() : m_backupFrequency->currentData().toInt();
        m_result.backupKeepCount = m_backupKeep->value();
        m_result.backupDirectory = folder;
        m_result.backupScope = m_backupScope->currentData().toString();
        m_result.lastRunVersion = appVersion;
        accept();
    });

}
