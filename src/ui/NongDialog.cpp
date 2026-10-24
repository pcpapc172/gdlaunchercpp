#include "NongDialog.h"
#include "Theme.h"
#include "../Settings.h"
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace { constexpr int pageSize = 30; }

NongDialog::NongDialog(QWidget *parent, NongManager *manager) : QDialog(parent), m_manager(manager) {
    setWindowTitle("NONG Downloader");
    setWindowIcon(QIcon(":/icon.png"));
    resize(950, 720);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin);
    root->setSpacing(UiMetrics::kSpacing);
    auto *heading = new QLabel("NONG Downloader", this);
    heading->setObjectName("heading");
    root->addWidget(heading);
    auto *about = new QLabel("Songs from <a href='https://songfilehub.com/songs'>SongFileHub</a>. Existing files are renamed to .original before replacement.", this);
    about->setWordWrap(true);
    about->setOpenExternalLinks(true);
    root->addWidget(about);
    root->addWidget(new QLabel("Download folder (supports %LOCALAPPDATA%):", this));
    auto *folderRow = new QHBoxLayout;
    m_folder = new QLineEdit(Settings::load().nongDownloadFolder, this);
    m_folder->setObjectName("nongFolder");
    m_folder->setPlaceholderText("%LOCALAPPDATA%/GeometryDash or %LOCALAPPDATA%/custom");
    auto *browse = new QPushButton("Browse...", this);
    auto *apply = new QPushButton("Apply folder", this);
    auto *reset = new QPushButton("Default", this);
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(browse);
    folderRow->addWidget(apply);
    folderRow->addWidget(reset);
    root->addLayout(folderRow);
    m_directory = NongManager::resolveDirectory(m_folder->text());
    if (m_directory.isEmpty()) m_directory = NongManager::defaultDirectory();
    m_folder->setToolTip(m_directory);
    m_folderPath = new QLabel("Destination: " + m_directory, this);
    m_folderPath->setWordWrap(true);
    m_folderPath->setTextFormat(Qt::PlainText);
    m_folderPath->setObjectName("subtext");
    root->addWidget(m_folderPath);
    connect(apply, &QPushButton::clicked, this, &NongDialog::applyDirectory);
    connect(m_folder, &QLineEdit::returnPressed, this, &NongDialog::applyDirectory);
    connect(reset, &QPushButton::clicked, this, [this]() {
        m_folder->setText("%LOCALAPPDATA%/GeometryDash"); applyDirectory();
    });
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString directory = QFileDialog::getExistingDirectory(this, "Choose song folder", m_directory);
        if (!directory.isEmpty()) { m_folder->setText(directory); applyDirectory(); }
    });
    auto *searchRow = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setObjectName("nongSearch");
    m_search->setPlaceholderText("Search song, level, song ID, level ID, or creator...");
    m_refresh = new QPushButton("Refresh", this);
    m_refresh->setObjectName("nongRetry");
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(m_refresh);
    root->addLayout(searchRow);
    auto *debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(180);
    connect(m_search, &QLineEdit::textChanged, this, [debounce]() { debounce->start(); });
    connect(debounce, &QTimer::timeout, this, &NongDialog::filterSongs);
    connect(m_refresh, &QPushButton::clicked, this, [this]() {
        m_status->setText("Fetching songs from SongFileHub...");
        m_refresh->setEnabled(false);
        m_manager->fetchCatalog();
    });
    m_status = new QLabel(this);
    m_status->setObjectName("nongStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    root->addWidget(m_status);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *container = new QWidget(scroll);
    m_list = new QVBoxLayout(container);
    m_list->setAlignment(Qt::AlignTop);
    scroll->setWidget(container);
    root->addWidget(scroll, 1);
    auto *footer = new QHBoxLayout;
    m_previous = new QPushButton("Previous", this);
    m_next = new QPushButton("Next", this);
    m_pageLabel = new QLabel(this);
    auto *close = new QPushButton("Close", this);
    footer->addWidget(m_previous);
    footer->addWidget(m_pageLabel);
    footer->addWidget(m_next);
    footer->addStretch();
    footer->addWidget(close);
    root->addLayout(footer);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_previous, &QPushButton::clicked, this, [this]() { --m_page; renderPage(); });
    connect(m_next, &QPushButton::clicked, this, [this]() { ++m_page; renderPage(); });
    connect(m_manager, &NongManager::catalogReady, this, [this]() {
        m_refresh->setEnabled(true);
        m_refresh->setText("Refresh");
        m_status->setText(QString("%1 songs available. Downloads continue when this window is closed.").arg(m_manager->songs().size()));
        filterSongs();
    });
    connect(m_manager, &NongManager::catalogError, this, [this](const QString &error) {
        m_refresh->setEnabled(true);
        m_refresh->setText("Retry");
        m_status->setText("Could not load SongFileHub: " + error + " Click Retry to try again.");
    });
    connect(m_manager, &NongManager::downloadStarted, this, [this](const QString &) { renderPage(); });
    connect(m_manager, &NongManager::downloadProgress, this, &NongDialog::updateProgress);
    connect(m_manager, &NongManager::downloadFinished, this, [this](const QString &key, bool success, const QString &message) {
        if (NongManager::operationKey(m_directory, QFileInfo(key).fileName()) != key) return;
        m_status->setText(success ? "Song downloaded. You can restore its original file if a backup was made." : message);
        renderPage();
    });
    filterSongs();
    if (m_manager->songs().isEmpty()) {
        m_status->setText("Fetching songs from SongFileHub...");
        m_refresh->setEnabled(false);
        m_manager->fetchCatalog();
    } else m_status->setText(QString("%1 songs available. Downloads continue when this window is closed.").arg(m_manager->songs().size()));
}
void NongDialog::applyDirectory() {
    const QString directory = NongManager::resolveDirectory(m_folder->text());
    if (directory.isEmpty()) {
        QMessageBox::warning(this, "Invalid Folder", "Enter an absolute folder path or a path such as %LOCALAPPDATA%/custom.");
        return;
    }
    m_directory = directory;
    m_folder->setToolTip(directory);
    m_folderPath->setText("Destination: " + directory);
    auto settings = Settings::load();
    settings.nongDownloadFolder = m_folder->text().trimmed();
    if (!Settings::save(settings)) m_status->setText("Folder selected, but the preference could not be saved.");
    else m_status->setText("Download folder: " + directory);
    renderPage();
}
void NongDialog::filterSongs() {
    m_filtered.clear();
    const auto terms = m_search->text().toLower().simplified().split(' ', Qt::SkipEmptyParts);
    const auto &songs = m_manager->songs();
    for (int i = 0; i < songs.size(); ++i) {
        bool matches = true;
        for (const auto &term : terms) if (!songs[i].searchText.contains(term)) { matches = false; break; }
        if (matches) m_filtered.push_back(i);
    }
    m_page = 0;
    renderPage();
}
void NongDialog::renderPage() {
    m_cards.clear();
    while (auto *item = m_list->takeAt(0)) { delete item->widget(); delete item; }
    const int pages = qMax(1, static_cast<int>((m_filtered.size() + pageSize - 1) / pageSize));
    m_page = qBound(0, m_page, pages - 1);
    const int end = qMin(static_cast<int>(m_filtered.size()), (m_page + 1) * pageSize);
    for (int i = m_page * pageSize; i < end; ++i) m_list->addWidget(buildCard(m_manager->songs()[m_filtered[i]]));
    if (m_filtered.isEmpty()) m_list->addWidget(new QLabel(m_manager->isFetching() ? "Loading catalogue..." : "No matching songs.", this));
    m_previous->setEnabled(m_page > 0);
    m_next->setEnabled(m_page + 1 < pages);
    m_pageLabel->setText(QString("Page %1 / %2 · %3 results").arg(m_page + 1).arg(pages).arg(m_filtered.size()));
}
QWidget *NongDialog::buildCard(const NongSong &song) {
    const QString directory = m_directory;
    const QString key = NongManager::operationKey(directory, song.songId);
    auto *card = new QFrame(this);
    card->setObjectName("card");
    card->setProperty("recordId", song.recordId);
    auto *layout = new QVBoxLayout(card);
    auto *title = new QLabel(song.description.isEmpty() ? song.name : song.description, card);
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    title->setStyleSheet("font-weight:600;");
    layout->addWidget(title);
    QString description = QString("%1 · Song ID: %2 · %3").arg(song.name, song.songId, song.format.toUpper());
    if (!song.levels.isEmpty()) description += " · Levels: " + song.levels;
    if (!song.credit.isEmpty()) description += " · By " + song.credit;
    auto *detail = new QLabel(description, card);
    detail->setTextFormat(Qt::PlainText);
    detail->setWordWrap(true);
    detail->setObjectName("subtext");
    layout->addWidget(detail);
    auto *bar = new QProgressBar(card);
    bar->setObjectName("songProgress");
    const bool busy = m_manager->isDownloading(key);
    const bool active = busy && m_manager->operation(key).recordId == song.recordId;
    bar->setVisible(active);
    layout->addWidget(bar);
    auto *actions = new QHBoxLayout;
    auto *download = new QPushButton(active ? "Downloading..." : busy ? "Same ID downloading..." : NongManager::hasSong(directory, song.songId) ? "Replace" : "Download", card);
    download->setObjectName("primary");
    download->setEnabled(!busy);
    if (busy && !active) download->setToolTip("Another song is downloading to this same song ID. Wait for it to finish before replacing it.");
    auto *restore = new QPushButton("Restore original", card);
    restore->setEnabled(!busy && NongManager::hasOriginal(directory, song.songId));
    auto *cancel = new QPushButton("Cancel", card);
    cancel->setVisible(active);
    actions->addWidget(download);
    actions->addWidget(restore);
    actions->addStretch();
    actions->addWidget(cancel);
    layout->addLayout(actions);
    connect(download, &QPushButton::clicked, this, [this, song, directory]() {
        if (NongManager::hasSong(directory, song.songId) && QMessageBox::question(this, "Replace song",
            "Replace the current song? Its original file will be kept for Restore original.") != QMessageBox::Yes) return;
        m_manager->downloadSong(song, directory);
    });
    connect(cancel, &QPushButton::clicked, this, [this, key]() { m_manager->cancelDownload(key); });
    connect(restore, &QPushButton::clicked, this, [this, song, directory]() {
        QString error;
        if (!m_manager->restoreOriginal(directory, song.songId, &error)) m_status->setText(error);
        else m_status->setText("Original song restored.");
        renderPage();
    });
    m_cards[key].push_back(card);
    if (active) {
        const auto op = m_manager->operation(key);
        updateProgress(key, op.received, op.total);
    }
    return card;
}
void NongDialog::updateProgress(const QString &key, qint64 received, qint64 total) {
    const QString recordId = m_manager->operation(key).recordId;
    for (auto *card : m_cards.value(key)) {
        if (card->property("recordId").toString() != recordId) continue;
        auto *bar = card->findChild<QProgressBar *>("songProgress");
        bar->setVisible(true);
        bar->setRange(0, total > 0 ? 100 : 0);
        if (total > 0) bar->setValue(static_cast<int>(received * 100 / total));
    }
}
