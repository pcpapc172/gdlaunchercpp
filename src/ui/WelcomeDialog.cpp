#include "WelcomeDialog.h"
#include "Theme.h"
#include "Animations.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QApplication>
#include <QIcon>

namespace {
QWidget *makeStep(QWidget *parent, QStyle::StandardPixmap icon, const QString &title, const QString &desc) {
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin);
    layout->setSpacing(UiMetrics::kSpacing);
    layout->addStretch();

    auto *iconLabel = new QLabel(page);
    iconLabel->setPixmap(QApplication::style()->standardIcon(icon).pixmap(48, 48));
    iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(iconLabel);

    auto *titleLabel = new QLabel(title, page);
    titleLabel->setObjectName("heading");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    auto *descLabel = new QLabel(desc, page);
    descLabel->setObjectName("subtext");
    descLabel->setWordWrap(true);
    descLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(descLabel);

    layout->addStretch();
    return page;
}
}

WelcomeDialog::WelcomeDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Welcome");
    setWindowIcon(QIcon(":/icon.png"));
    setMinimumSize(460, 380);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    m_stack->addWidget(makeStep(this, QStyle::SP_DesktopIcon, "Welcome to GDLauncher",
        "Let's walk through the essentials before you get started."));
    m_stack->addWidget(makeStep(this, QStyle::SP_FileDialogNewFolder, "Create Instances",
        "Instances are separated profiles for different mods, texture packs, or Geometry Dash versions. "
        "Use Create on the main window to set one up."));
    m_stack->addWidget(makeStep(this, QStyle::SP_MediaPlay, "Launch and Sync",
        "Launching swaps in the instance's save files and starts the game. When you close it, the launcher "
        "syncs your progress back into that instance automatically."));
    m_stack->addWidget(makeStep(this, QStyle::SP_FileDialogDetailedView, "Save Editor",
        "Need to rename a level, change its song, or fix save data without opening the game? The Save Editor "
        "handles all of that directly."));
    m_stack->addWidget(makeStep(this, QStyle::SP_DriveHDIcon, "Existing Save Data",
        "Checking for Geometry Dash save data already on this computer, so it can be imported as its own instance instead of being left behind..."));

    m_importStepIndex = m_stack->count() - 1;

    auto *footer = new QWidget(this);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(UiMetrics::kMargin, UiMetrics::kSpacing, UiMetrics::kMargin, UiMetrics::kMargin);

    m_counterLabel = new QLabel(this);
    m_counterLabel->setObjectName("subtext");
    footerLayout->addWidget(m_counterLabel);
    footerLayout->addStretch();

    m_backBtn = new QPushButton("Back", this);
    m_nextBtn = new QPushButton("Next", this);
    m_nextBtn->setObjectName("primary");
    footerLayout->addWidget(m_backBtn);
    footerLayout->addWidget(m_nextBtn);
    root->addWidget(footer);

    connect(m_backBtn, &QPushButton::clicked, this, [this]() { goTo(m_stack->currentIndex() - 1); });
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        const int next = m_stack->currentIndex() + 1;
        if (next >= m_stack->count()) { accept(); return; }
        goTo(next);
        if (next == m_importStepIndex) emit importRequested();
    });

    goTo(0);
    Animations::fadeIn(this);
}

void WelcomeDialog::goTo(int index) {
    if (index < 0 || index >= m_stack->count()) return;
    m_stack->setCurrentIndex(index);
    updateNavigation();
}

void WelcomeDialog::updateNavigation() {
    const int index = m_stack->currentIndex();
    m_backBtn->setEnabled(index > 0);
    m_nextBtn->setEnabled(index != m_importStepIndex); // wait for continueAfterImport() on that step
    m_nextBtn->setText(index == m_stack->count() - 1 ? "Finish" : "Next");
    m_counterLabel->setText(QString("%1 / %2").arg(index + 1).arg(m_stack->count()));
}

void WelcomeDialog::continueAfterImport(bool /*foundFiles*/) {
    m_nextBtn->setEnabled(true);
    m_nextBtn->setText("Finish");
}
