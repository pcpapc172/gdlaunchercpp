#pragma once
#include <QDialog>

class QStackedWidget;
class QLabel;
class QPushButton;

// A short first-run introduction to the launcher's main features. This is a
// lighter-weight equivalent of the Electron build's overlay walkthrough
// (which highlighted live UI elements); here it's a self-contained multi-step
// dialog instead, since QtWidgets has no built-in element-highlighting overlay.
class WelcomeDialog : public QDialog {
    Q_OBJECT
public:
    explicit WelcomeDialog(QWidget *parent = nullptr);

signals:
    // Emitted when the user reaches the "import existing save data" step;
    // MainWindow runs the actual import and calls continueAfterImport().
    void importRequested();

public slots:
    void continueAfterImport(bool foundFiles);

private:
    QStackedWidget *m_stack;
    QLabel *m_counterLabel;
    QPushButton *m_backBtn;
    QPushButton *m_nextBtn;
    int m_importStepIndex = -1;

    void goTo(int index);
    void updateNavigation();
};
