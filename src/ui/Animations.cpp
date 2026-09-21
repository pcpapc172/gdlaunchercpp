#include "Animations.h"
#include <QPropertyAnimation>
#include <QEasingCurve>

void Animations::fadeIn(QWidget *topLevelWindow, int durationMs) {
    if (!topLevelWindow) return;
    topLevelWindow->setWindowOpacity(0.0);
    auto *anim = new QPropertyAnimation(topLevelWindow, "windowOpacity", topLevelWindow);
    anim->setDuration(durationMs);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

AnimatedProgressBar::AnimatedProgressBar(QWidget *parent) : QProgressBar(parent) {}

void AnimatedProgressBar::setAnimatedValue(int value) {
    auto *anim = new QPropertyAnimation(this, "value", this);
    anim->setDuration(250);
    anim->setStartValue(this->value());
    anim->setEndValue(value);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}
