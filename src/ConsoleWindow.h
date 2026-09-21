#pragma once
#include <QWidget>

class QPlainTextEdit;
class QLabel;

// Port of console.html - a standalone, persistent log window.
class ConsoleWindow : public QWidget {
    Q_OBJECT
public:
    explicit ConsoleWindow(QWidget *parent = nullptr);

public slots:
    void addLine(const QString &line);
    void setRunning(bool running);
    void initLines(const QStringList &lines);

private:
    QPlainTextEdit *m_terminal;
    QLabel *m_statusDot;
    QLabel *m_statusText;
};
