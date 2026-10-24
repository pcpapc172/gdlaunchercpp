#include "Animations.h"

AnimatedProgressBar::AnimatedProgressBar(QWidget *parent) : QProgressBar(parent) {}

void AnimatedProgressBar::setAnimatedValue(int value) {
    setValue(value);
}
