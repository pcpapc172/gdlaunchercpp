#include "DownloadsDialog.h"
#include "../Settings.h"
#include "Theme.h"
#include "Animations.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QFrame>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QStyle>
#include <QApplication>
#include <QIcon>

namespace {
constexpr const char *kExtractingChunkStyle = "QProgressBar::chunk { background-color: #2ecc71; border-radius: 5px; }";
}

DownloadsDialog::DownloadsDialog(QWidget *parent, VersionManager *versionManager)
    : QDialog(parent), m_versionManager(versionManager) {
    setWindowTitle("Downloads");
    resize(1000, 700);
    setWindowIcon(QIcon(":/icon.png"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin);
    root->setSpacing(UiMetrics::kSpacing);

    auto *heading = new QLabel("Downloads", this);
    heading->setObjectName("heading");
    root->addWidget(heading);

    m_statsWidget = new QWidget(this);
    auto *statsLayout = new QHBoxLayout(m_statsWidget);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(UiMetrics::kSpacing);
    root->addWidget(m_statsWidget);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search versions...");
    root->addWidget(m_searchEdit);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_listWidget = new QWidget(scroll);
    m_listLayout = new QVBoxLayout(m_listWidget);
    m_listLayout->setSpacing(UiMetrics::kSpacing);
    m_listLayout->addStretch();
    scroll->setWidget(m_listWidget);
    root->addWidget(scroll, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(UiMetrics::kSpacing);
    auto *closeBtn = new QPushButton("Close", this);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) { renderList(text); });

    connect(m_versionManager, &VersionManager::remoteVersionsReady, this, [this](const QJsonArray &versions) {
        m_versions = versions;
        renderStats();
        renderList(m_searchEdit->text());
        m_versionManager->resolveSizes(m_versions);
    });
    connect(m_versionManager, &VersionManager::sizeResolved, this, [this](const QString &id, const QString &size) {
        for (int i = 0; i < m_versions.size(); ++i) {
            QJsonObject v = m_versions[i].toObject();
            if (v.value("id").toString() == id) { v["size"] = size; m_versions[i] = v; break; }
        }
        QWidget *card = m_cardWidgets.value(id);
        if (!card) return;
        if (auto *label = card->findChild<QLabel *>("sizeLabel")) label->setText(size);
    });
    connect(m_versionManager, &VersionManager::downloadProgress, this, [this](const QString &id, qint64 recv, qint64 total) {
        QWidget *card = m_cardWidgets.value(id);
        if (!card) return;
        if (auto *bar = card->findChild<QProgressBar *>()) {
            bar->setVisible(true);
            if (total > 0) bar->setValue(static_cast<int>(recv * 100 / total));
        }
        if (auto *label = card->findChild<QLabel *>("progressLabel")) {
            const QString pct = total > 0 ? QString::number(recv * 100 / total) : "0";
            label->setText(QString("Downloading... %1%").arg(pct));
        }
    });
    connect(m_versionManager, &VersionManager::extractionStarted, this, [this](const QString &id) {
        QWidget *card = m_cardWidgets.value(id);
        if (!card) return;
        if (auto *bar = card->findChild<QProgressBar *>()) {
            bar->setVisible(true);
            bar->setValue(0);
            bar->setStyleSheet(kExtractingChunkStyle);
        }
        if (auto *label = card->findChild<QLabel *>("progressLabel")) label->setText("Extracting... 0%");
    });
    connect(m_versionManager, &VersionManager::extractionProgress, this, [this](const QString &id, qint64 cur, qint64 total) {
        QWidget *card = m_cardWidgets.value(id);
        if (!card) return;
        const int pct = total > 0 ? static_cast<int>(cur * 100 / total) : 0;
        if (auto *bar = card->findChild<QProgressBar *>()) bar->setValue(pct);
        if (auto *label = card->findChild<QLabel *>("progressLabel")) label->setText(QString("Extracting... %1%").arg(pct));
    });
    connect(m_versionManager, &VersionManager::downloadFinished, this, [this](const QString &id, bool success, const QString &message) {
        Q_UNUSED(id);
        if (!success && message != "Cancelled") QMessageBox::warning(this, "Download failed", message.isEmpty() ? "Download failed" : message);
        refresh();
    });

    refresh();
    Animations::fadeIn(this);
}

void DownloadsDialog::refresh() {
    m_versionManager->fetchRemoteVersions();
}

void DownloadsDialog::renderStats() {
    QLayoutItem *item;
    while ((item = m_statsWidget->layout()->takeAt(0)) != nullptr) { delete item->widget(); delete item; }

    int installed = 0;
    for (const QJsonValue &v : m_versions) if (v.toObject().value("isInstalled").toBool()) installed++;
    const int available = m_versions.size() - installed;

    auto addStat = [&](const QString &label, const QString &value) {
        auto *card = new QFrame(m_statsWidget);
        card->setObjectName("card");
        auto *l = new QVBoxLayout(card);
        l->setContentsMargins(UiMetrics::kMargin, UiMetrics::kTightSpacing, UiMetrics::kMargin, UiMetrics::kTightSpacing);
        l->setSpacing(4);
        auto *lab = new QLabel(label, card);
        lab->setObjectName("subtext");
        auto *val = new QLabel(value, card);
        val->setStyleSheet("font-size:22px; font-weight:700;");
        l->addWidget(lab);
        l->addWidget(val);
        m_statsWidget->layout()->addWidget(card);
    };
    addStat("Installed", QString::number(installed));
    addStat("Available", QString::number(available));
}

void DownloadsDialog::renderList(const QString &filter) {
    QLayoutItem *item;
    while ((item = m_listLayout->takeAt(0)) != nullptr) { if (item->widget()) delete item->widget(); delete item; }
    m_cardWidgets.clear();

    const QString needle = filter.toLower();
    for (const QJsonValue &vv : m_versions) {
        const QJsonObject v = vv.toObject();
        const QString name = v.value("name").toString();
        const QString path = v.value("path").toString();
        if (!needle.isEmpty() && !name.toLower().contains(needle) && !path.toLower().contains(needle)) continue;
        QWidget *card = buildCard(v);
        m_listLayout->insertWidget(m_listLayout->count() - 1, card);
        m_cardWidgets[v.value("id").toString()] = card;
        applyOperationState(card, v.value("id").toString());
    }
}

QWidget *DownloadsDialog::buildCard(const QJsonObject &v) {
    const QString id = v.value("id").toString();
    const QString path = v.value("path").toString();
    const bool installed = v.value("isInstalled").toBool();
    QStyle *style = QApplication::style();

    auto *card = new QFrame(m_listWidget);
    card->setObjectName("card");
    card->setProperty("versionId", id);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(UiMetrics::kMargin, UiMetrics::kSpacing, UiMetrics::kMargin, UiMetrics::kSpacing);
    layout->setSpacing(UiMetrics::kTightSpacing);

    auto *headerRow = new QHBoxLayout();
    headerRow->setSpacing(UiMetrics::kSpacing);
    auto *nameLabel = new QLabel(QString("<b>%1</b><br><span style='opacity:0.6'>%2</span>")
                                      .arg(v.value("name").toString(), path), card);
    headerRow->addWidget(nameLabel);
    headerRow->addStretch();
    auto *badge = new QLabel(installed ? "Installed" : "Available", card);
    badge->setStyleSheet(installed ? "color:#2ecc71; font-weight:600;" : "color:#3498db; font-weight:600;");
    headerRow->addWidget(badge);
    layout->addLayout(headerRow);

    auto *sizeLabel = new QLabel(v.value("size").toString("Calculating..."), card);
    sizeLabel->setObjectName("sizeLabel");
    sizeLabel->setStyleSheet("opacity:0.7; font-size:12px;");
    layout->addWidget(sizeLabel);

    auto *progressRow = new QHBoxLayout();
    progressRow->setSpacing(UiMetrics::kTightSpacing);
    auto *progressLabel = new QLabel(card);
    progressLabel->setObjectName("progressLabel");
    progressLabel->setStyleSheet("font-size:12px;");
    progressLabel->setVisible(false);
    auto *cancelBtn = new QPushButton("Cancel", card);
    cancelBtn->setObjectName("cancelBtn");
    cancelBtn->setVisible(false);
    cancelBtn->setMaximumWidth(80);
    progressRow->addWidget(progressLabel, 1);
    progressRow->addWidget(cancelBtn);
    layout->addLayout(progressRow);
    connect(cancelBtn, &QPushButton::clicked, this, [this, id]() { m_versionManager->cancelOperation(id); });

    auto *progressBar = new AnimatedProgressBar(card);
    progressBar->setRange(0, 100);
    progressBar->setVisible(false);
    layout->addWidget(progressBar);

    auto *actions = new QHBoxLayout();
    actions->setSpacing(UiMetrics::kTightSpacing);
    if (installed) {
        auto *repairBtn = new QPushButton(style->standardIcon(QStyle::SP_BrowserReload), "Repair", card);
        auto *deleteBtn = new QPushButton(style->standardIcon(QStyle::SP_TrashIcon), "Delete", card);
        deleteBtn->setObjectName("danger");
        auto *folderBtn = new QPushButton(style->standardIcon(QStyle::SP_DirOpenIcon), QString(), card);
        folderBtn->setToolTip("Open Folder");
        actions->addWidget(repairBtn);
        actions->addWidget(deleteBtn);
        actions->addWidget(folderBtn);
        connect(repairBtn, &QPushButton::clicked, this, [this, id, repairBtn]() {
            if (QMessageBox::question(this, "Repair", "Repair this version?\n\nThis will re-download and overwrite existing files.") != QMessageBox::Yes) return;
            repairBtn->setEnabled(false);
            startDownload(id, true);
        });
        connect(deleteBtn, &QPushButton::clicked, this, [this, path, id]() { deleteVersion(path, id); });
        connect(folderBtn, &QPushButton::clicked, this, [this, path]() { openVersionFolder(path); });
    } else {
        auto *downloadBtn = new QPushButton(style->standardIcon(QStyle::SP_ArrowDown), "Download", card);
        downloadBtn->setObjectName("primary");
        actions->addWidget(downloadBtn);
        connect(downloadBtn, &QPushButton::clicked, this, [this, id, downloadBtn]() {
            downloadBtn->setEnabled(false);
            downloadBtn->setText("Starting...");
            startDownload(id, false);
        });
    }
    layout->addLayout(actions);

    return card;
}

void DownloadsDialog::applyOperationState(QWidget *card, const QString &id) {
    if (!m_versionManager->hasActiveOperation(id)) return;
    const VersionOperation op = m_versionManager->activeOperation(id);

    auto *bar = card->findChild<QProgressBar *>();
    auto *progressLabel = card->findChild<QLabel *>("progressLabel");
    auto *cancelBtn = card->findChild<QPushButton *>("cancelBtn");
    const QList<QPushButton *> actionsWidgets = card->findChildren<QPushButton *>();

    const int pct = op.total > 0 ? static_cast<int>(op.current * 100 / op.total) : 0;
    const bool extracting = op.phase == VersionOperation::Phase::Extracting;

    if (bar) {
        bar->setVisible(true);
        bar->setValue(pct);
        if (extracting) bar->setStyleSheet(kExtractingChunkStyle);
    }
    if (progressLabel) {
        progressLabel->setVisible(true);
        progressLabel->setText(QString("%1... %2%").arg(extracting ? "Extracting" : "Downloading").arg(pct));
    }
    if (cancelBtn) cancelBtn->setVisible(true);

    for (QPushButton *btn : actionsWidgets) {
        if (btn->objectName() == "cancelBtn") continue;
        btn->setEnabled(false);
        if (btn->objectName() == "primary") btn->setText(extracting ? "Extracting..." : "Downloading...");
    }
}

void DownloadsDialog::startDownload(const QString &id, bool /*repair*/) {
    for (const QJsonValue &vv : m_versions) {
        QJsonObject v = vv.toObject();
        if (v.value("id").toString() == id) {
            m_versionManager->downloadVersion(v);
            QWidget *card = m_cardWidgets.value(id);
            if (card) applyOperationState(card, id);
            return;
        }
    }
}

void DownloadsDialog::deleteVersion(const QString &path, const QString &id) {
    if (QMessageBox::question(this, "Delete version",
            QString("Delete this version?\n\nPath: %1\n\nThis will move it to trash.").arg(path)) != QMessageBox::Yes)
        return;
    QString error;
    if (VersionManager::deleteVersion(path, &error)) {
        refresh();
    } else {
        QMessageBox::warning(this, "Failed to delete", error);
    }
}

void DownloadsDialog::openVersionFolder(const QString &path) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::versionsDir() + "/" + path));
}
