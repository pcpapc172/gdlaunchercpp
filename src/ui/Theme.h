#pragma once
#include <QString>

// Central place for the app's visual language: color tokens, the generated
// QSS stylesheet for each theme, and shared layout metrics so every dialog
// uses the same spacing instead of ad-hoc numbers.
namespace Theme {

enum class Kind { Light, Dark };

Kind fromSettingsString(const QString &theme);
QString toSettingsString(Kind kind);

// Applies the theme application-wide (qApp->setStyleSheet).
void apply(Kind kind);

QString styleSheet(Kind kind);

}

namespace UiMetrics {
constexpr int kMargin = 16;
constexpr int kSpacing = 10;
constexpr int kTightSpacing = 6;
constexpr int kSectionSpacing = 18;
}
