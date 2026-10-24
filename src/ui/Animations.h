#pragma once
#include <QWidget>
#include <QProgressBar>

// Shared progress bar API. Values update immediately without UI animations.
class AnimatedProgressBar : public QProgressBar {
    Q_OBJECT
public:
    explicit AnimatedProgressBar(QWidget *parent = nullptr);
    void setAnimatedValue(int value);
};
