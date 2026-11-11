#pragma once
#include <QComboBox>
#include "../ProtonManager.h"

inline QComboBox *makeProtonVersionCombo(QWidget *parent, const QString &choice, bool inherit = false) {
    auto *combo = new QComboBox(parent);
    combo->setObjectName("protonVersion");
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    if (inherit) combo->addItem("Use Settings default", "default");
    combo->addItem("Automatic (Proton 9 for standalone MegaHack)", "auto");
    combo->addItem("Latest downloaded release", "latest");
    combo->addItem("GE-Proton9-27", "GE-Proton9-27");
    const int index = combo->findData(choice);
    if (index >= 0) combo->setCurrentIndex(index);
    else combo->setEditText(choice);
    combo->setToolTip("Select a version or type an official release tag, e.g. GE-Proton9-27. Missing versions download on launch. The latest release is downloaded once and reused.");
    return combo;
}

inline QString protonVersionChoice(const QComboBox *combo) {
    return combo->currentIndex() >= 0 && combo->currentText() == combo->itemText(combo->currentIndex())
        ? combo->currentData().toString() : combo->currentText().trimmed();
}
