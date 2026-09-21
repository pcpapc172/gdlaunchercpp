#include "ChangelogDialog.h"
#include "Theme.h"
#include "Animations.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QIcon>

ChangelogDialog::ChangelogDialog(QWidget *parent, const QString &appVersion, const QStringList &entries)
    : QDialog(parent) {
    setWindowTitle("What's New");
    setWindowIcon(QIcon(":/icon.png"));
    setMinimumWidth(420);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin, UiMetrics::kMargin);
    root->setSpacing(UiMetrics::kSpacing);

    auto *headerRow = new QHBoxLayout();
    auto *heading = new QLabel("What's New", this);
    heading->setObjectName("heading");
    auto *badge = new QLabel(QString("v%1").arg(appVersion), this);
    badge->setStyleSheet("background:#2f7de1; color:white; border-radius:10px; padding:2px 10px; font-size:12px; font-weight:600;");
    headerRow->addWidget(heading);
    headerRow->addWidget(badge);
    headerRow->addStretch();
    root->addLayout(headerRow);

    QString listHtml = "<ul style='margin:0; padding-left:18px;'>";
    for (const QString &entry : entries) listHtml += QString("<li style='margin-bottom:6px;'>%1</li>").arg(entry);
    listHtml += "</ul>";
    auto *body = new QLabel(listHtml, this);
    body->setWordWrap(true);
    root->addWidget(body);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto *okBtn = new QPushButton("Got it", this);
    okBtn->setObjectName("primary");
    btnRow->addWidget(okBtn);
    root->addLayout(btnRow);

    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    Animations::fadeIn(this);
}
