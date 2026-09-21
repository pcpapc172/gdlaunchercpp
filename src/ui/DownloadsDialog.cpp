#include "DownloadsDialog.h"
#include "../Settings.h"
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

DownloadsDialog::DownloadsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Downloads");
    resize(1000, 700);

    m_versionManager = new VersionManager(this);

    auto *root = new QVBoxLayout(this);

    m_statsWidget = new QWidget(this);
    auto *statsLayout = new QHBoxLayout(m_statsWidget);
    root->addWidget(m_statsWidget);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("🔍 Search versions...");
    root->addWidget(m_searchEdit);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    m_listWidget = new QWidget(scroll);
    m_listLayout = new QVBoxLayout(m_listWidget);
    m_listLayout->addStretch();
    scroll->setWidget(m_listWidget);
    root->addWidget(scroll, 1);

    auto *btnRow = new QHBoxLayout();
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
    });
    connect(m_versionManager, &VersionManager::downloadProgress, this, [this](const QString &id, qint64 recv, qint64 total) {
        QWidget *card = m_cardWidgets.value(id);
        if (!card) return;
        if (auto *bar = card->findChild<QProgressBar *>()) {
            bar->setVisible(true);
            if (total > 0) bar->setValue(static_cast<int>(recv * 100 / total));
        }
    });
    connect(m_versionManager, &VersionManager::downloadFinished, this, [this](const QString &id, bool success, const QString &message) {
        if (success) {
            QMessageBox::information(this, "Download complete", "Download complete!");
            refresh();
        } else {
            QMessageBox::warning(this, "Download failed", message.isEmpty() ? "Download failed" : message);
            refresh();
        }
    });

    refresh();
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
        card->setFrameShape(QFrame::StyledPanel);
        auto *l = new QVBoxLayout(card);
        auto *lab = new QLabel(label, card);
        lab->setStyleSheet("opacity:0.7; font-size:12px;");
        auto *val = new QLabel(value, card);
        val->setStyleSheet("font-size:22px; font-weight:bold;");
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
    }
}

QWidget *DownloadsDialog::buildCard(const QJsonObject &v) {
    const QString id = v.value("id").toString();
    const QString path = v.value("path").toString();
    const bool installed = v.value("isInstalled").toBool();

    auto *card = new QFrame(m_listWidget);
    card->setFrameShape(QFrame::StyledPanel);
    card->setProperty("versionId", id);
    auto *layout = new QVBoxLayout(card);

    auto *headerRow = new QHBoxLayout();
    auto *nameLabel = new QLabel(QString("<b>%1</b><br><span style='opacity:0.6'>%2</span>")
                                      .arg(v.value("name").toString(), path), card);
    headerRow->addWidget(nameLabel);
    headerRow->addStretch();
    auto *badge = new QLabel(installed ? "✓ Installed" : "Available", card);
    badge->setStyleSheet(installed ? "color:#2ecc71; font-weight:bold;" : "color:#3498db; font-weight:bold;");
    headerRow->addWidget(badge);
    layout->addLayout(headerRow);

    auto *sizeLabel = new QLabel(QString("💾 %1").arg(v.value("size").toString("Size unknown")), card);
    sizeLabel->setStyleSheet("opacity:0.7; font-size:12px;");
    layout->addWidget(sizeLabel);

    auto *progressBar = new QProgressBar(card);
    progressBar->setRange(0, 100);
    progressBar->setVisible(false);
    layout->addWidget(progressBar);

    auto *actions = new QHBoxLayout();
    if (installed) {
        auto *repairBtn = new QPushButton("🔧 Repair", card);
        auto *deleteBtn = new QPushButton("🗑️ Delete", card);
        auto *folderBtn = new QPushButton("📁", card);
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
        auto *downloadBtn = new QPushButton("⬇️ Download", card);
        actions->addWidget(downloadBtn);
        connect(downloadBtn, &QPushButton::clicked, this, [this, id, downloadBtn]() {
            downloadBtn->setEnabled(false);
            downloadBtn->setText("⏳ Downloading...");
            startDownload(id, false);
        });
    }
    layout->addLayout(actions);

    return card;
}

void DownloadsDialog::startDownload(const QString &id, bool /*repair*/) {
    for (const QJsonValue &vv : m_versions) {
        QJsonObject v = vv.toObject();
        if (v.value("id").toString() == id) {
            m_versionManager->downloadVersion(v);
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
