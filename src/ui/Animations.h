#pragma once
#include <QWidget>
#include <QProgressBar>

// Small, dependency-free animation helpers shared across the app.
namespace Animations {
// Fades a top-level window (QDialog/QMainWindow) in from transparent.
void fadeIn(QWidget *topLevelWindow, int durationMs = 200);
}

// A QProgressBar that eases toward its target value instead of jumping,
// used everywhere the app reports download/launch progress.
class AnimatedProgressBar : public QProgressBar {
    Q_OBJECT
public:
    explicit AnimatedProgressBar(QWidget *parent = nullptr);
    void setAnimatedValue(int value);
};
