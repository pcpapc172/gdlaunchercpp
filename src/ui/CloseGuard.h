#pragma once
#include <QString>
class QWidget;

namespace CloseGuard {
// True only when Force close is selected and then confirmed a second time.
bool confirmForceClose(QWidget *parent, const QString &reason, bool allowForce = true, bool confirmTwice = true);
}
