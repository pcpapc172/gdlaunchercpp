#include "CloseGuard.h"
#include <QMessageBox>
#include <QPushButton>

bool CloseGuard::confirmForceClose(QWidget *parent, const QString &reason, bool allowForce, bool confirmTwice) {
    QMessageBox blocked(QMessageBox::Critical, "Cannot Close GDLauncher", reason,
                        QMessageBox::NoButton, parent);
    blocked.setInformativeText("Keep GDLauncher open until this finishes. Force closing can interrupt file transfers or prevent game saves from syncing.");
    auto *stay = blocked.addButton("Stay open", QMessageBox::RejectRole);
    stay->setObjectName("closeGuardStay");
    QPushButton *force = nullptr;
    if (allowForce) {
        force = blocked.addButton("Force close...", QMessageBox::DestructiveRole);
        force->setObjectName("closeGuardForce");
    }
    blocked.setDefaultButton(stay);
    blocked.setEscapeButton(stay);
    blocked.exec();
    if (!force || blocked.clickedButton() != force) return false;
    if (!confirmTwice) return true;

    QMessageBox confirm(QMessageBox::Warning, "Confirm Force Close",
        "Force close GDLauncher now?", QMessageBox::NoButton, parent);
    confirm.setInformativeText("This immediately exits the launcher. Files may be incomplete and unsynced game progress may be lost. No final save sync will run.");
    auto *cancel = confirm.addButton("Stay open", QMessageBox::RejectRole);
    cancel->setObjectName("closeGuardCancel");
    auto *exit = confirm.addButton("Force close now", QMessageBox::DestructiveRole);
    exit->setObjectName("closeGuardConfirm");
    confirm.setDefaultButton(cancel);
    confirm.setEscapeButton(cancel);
    confirm.exec();
    return confirm.clickedButton() == exit;
}
