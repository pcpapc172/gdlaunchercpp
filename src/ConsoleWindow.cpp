#include "ConsoleWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>

ConsoleWindow::ConsoleWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle("Game Log Console");
    resize(860, 520);
    setStyleSheet("background:#0d0d0d; color:#00e676;");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *toolbar = new QWidget(this);
    toolbar->setStyleSheet("background:#1a1a1a; border-bottom:1px solid #333;");
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(12, 8, 12, 8);

    m_statusDot = new QLabel(this);
    m_statusDot->setFixedSize(8, 8);
    m_statusDot->setStyleSheet("background:#555; border-radius:4px;");
    m_statusText = new QLabel("Idle", this);
    m_statusText->setStyleSheet("color:#ccc; font-size:11px;");

    toolbarLayout->addWidget(m_statusDot);
    toolbarLayout->addWidget(m_statusText);
    toolbarLayout->addStretch();

    auto *clearBtn = new QPushButton("Clear", this);
    auto *copyBtn = new QPushButton("Copy All", this);
    for (QPushButton *b : {clearBtn, copyBtn})
        b->setStyleSheet("padding:4px 12px; font-size:11px; background:#2a2a2a; color:#ccc; border:1px solid #444; border-radius:4px;");
    toolbarLayout->addWidget(clearBtn);
    toolbarLayout->addWidget(copyBtn);

    root->addWidget(toolbar);

    m_terminal = new QPlainTextEdit(this);
    m_terminal->setReadOnly(true);
    m_terminal->setStyleSheet("background:#0d0d0d; color:#00e676; font-family:'Courier New',monospace; font-size:12px; border:none;");
    m_terminal->setPlainText("Waiting for game launch...");
    root->addWidget(m_terminal, 1);

    connect(clearBtn, &QPushButton::clicked, this, [this]() { m_terminal->setPlainText("Log cleared."); });
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_terminal->toPlainText());
    });
}

void ConsoleWindow::addLine(const QString &line) {
    if (m_terminal->toPlainText() == "Waiting for game launch...") m_terminal->clear();
    m_terminal->appendPlainText(line);
}

void ConsoleWindow::initLines(const QStringList &lines) {
    if (lines.isEmpty()) return;
    m_terminal->clear();
    for (const QString &l : lines) m_terminal->appendPlainText(l);
}

void ConsoleWindow::setRunning(bool running) {
    if (running) {
        m_statusDot->setStyleSheet("background:#00e676; border-radius:4px;");
        m_statusText->setText("Live");
    } else {
        m_statusDot->setStyleSheet("background:#555; border-radius:4px;");
        m_statusText->setText("Idle");
    }
}
