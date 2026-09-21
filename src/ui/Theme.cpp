#include "Theme.h"
#include <QApplication>

Theme::Kind Theme::fromSettingsString(const QString &theme) {
    return theme == "Dark" ? Kind::Dark : Kind::Light;
}

QString Theme::toSettingsString(Kind kind) {
    return kind == Kind::Dark ? "Dark" : "Light";
}

QString Theme::styleSheet(Kind kind) {
    struct Palette {
        QString bg, panel, text, textMuted, border, hover, selected, selectedText, input, accent, accentAlt;
    };

    Palette p;
    if (kind == Kind::Dark) {
        p = {"#232629", "#2c2f33", "#e8e8e8", "#9aa0a6", "#3c4045", "#34383c",
             "#3d7ab8", "#ffffff", "#1c1e21", "#4a90d9", "#8e6fce"};
    } else {
        p = {"#f4f5f7", "#ffffff", "#1c1e21", "#6b7280", "#dde1e6", "#eef1f5",
             "#2f7de1", "#ffffff", "#ffffff", "#2f7de1", "#7c5cd4"};
    }

    return QString(R"(
        QWidget {
            color: %3;
            font-family: 'Segoe UI', 'Cantarell', sans-serif;
            font-size: 13px;
        }
        /* Only top-level windows and explicitly named containers paint a
           background; everything else (labels, checkboxes, panels' own
           children, ...) stays transparent so it shows its parent's
           background instead of a mismatched opaque rectangle. */
        QMainWindow, QDialog { background-color: %1; }
        QLabel, QCheckBox, QRadioButton, QGroupBox, QScrollArea, QScrollArea > QWidget > QWidget {
            background-color: transparent;
        }
        #panel, QTableWidget, QListWidget, QPlainTextEdit, QFrame#card {
            background-color: %2;
            border: 1px solid %5;
            border-radius: 8px;
        }
        QTableWidget {
            gridline-color: %5;
            selection-background-color: %7;
            selection-color: %8;
        }
        QHeaderView::section {
            background-color: %2;
            color: %4;
            border: none;
            border-bottom: 2px solid %5;
            padding: 8px;
            font-weight: 600;
        }
        QTableWidget::item, QListWidget::item { padding: 6px; }
        QTableWidget::item:hover, QListWidget::item:hover { background-color: %6; }

        QPushButton {
            background-color: %2;
            color: %3;
            border: 1px solid %5;
            border-radius: 6px;
            padding: 8px 16px;
        }
        QPushButton:hover { background-color: %6; border-color: %9; }
        QPushButton:pressed { background-color: %5; }
        QPushButton:disabled { color: %4; }

        QPushButton#primary {
            background-color: %9;
            border-color: %9;
            color: #ffffff;
            font-weight: 600;
        }
        QPushButton#primary:hover { background-color: %10; border-color: %10; }
        QPushButton#primary:disabled { background-color: %5; border-color: %5; color: %4; }

        QPushButton#danger { border-color: #c0392b; color: #e05c4b; }
        QPushButton#danger:hover { background-color: #c0392b; color: #ffffff; }

        QLineEdit, QComboBox, QSpinBox, QPlainTextEdit, QTextEdit {
            background-color: %11;
            border: 1px solid %5;
            border-radius: 6px;
            padding: 7px 10px;
            selection-background-color: %9;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus {
            border: 1px solid %9;
        }
        QComboBox::drop-down { border: none; width: 22px; }
        QComboBox QAbstractItemView {
            background-color: %2;
            border: 1px solid %5;
            selection-background-color: %7;
            selection-color: %8;
        }

        QCheckBox, QRadioButton { spacing: 8px; padding: 3px 0; }
        /* Once you set ANY property on ::indicator, Qt stops drawing the native box and
           expects the stylesheet to supply the whole look -- without an explicit
           border/background here the indicator renders as a blank, invisible square. */
        QCheckBox::indicator, QRadioButton::indicator {
            width: 15px;
            height: 15px;
            border: 1px solid %5;
            border-radius: 3px;
            background-color: %11;
        }
        QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: %9; }
        QCheckBox::indicator:checked, QRadioButton::indicator:checked {
            background-color: %9;
            border-color: %9;
        }
        QRadioButton::indicator {
            border-radius: 8px;
        }
        QRadioButton::indicator:checked {
            border: 4px solid %9;
            background-color: %11;
        }

        QGroupBox {
            border: 1px solid %5;
            border-radius: 8px;
            margin-top: 14px;
            padding-top: 12px;
            font-weight: 600;
        }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }

        QProgressBar {
            background-color: %1;
            border: 1px solid %5;
            border-radius: 6px;
            text-align: center;
            height: 10px;
        }
        QProgressBar::chunk {
            border-radius: 5px;
            background-color: %9;
        }

        QScrollBar:vertical { background: %1; width: 10px; margin: 0; }
        QScrollBar::handle:vertical { background: %5; border-radius: 5px; min-height: 24px; }
        QScrollBar::handle:vertical:hover { background: %6; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }

        QLabel#heading { font-size: 20px; font-weight: 700; }
        QLabel#subtext { color: %4; }
        QLabel#statusLabel { color: %4; }
    )")
        .arg(p.bg, p.panel, p.text, p.textMuted, p.border, p.hover, p.selected, p.selectedText, p.accent)
        .arg(p.accentAlt, p.input);
}

void Theme::apply(Kind kind) {
    if (auto *app = qobject_cast<QApplication *>(QApplication::instance()))
        app->setStyleSheet(styleSheet(kind));
}
