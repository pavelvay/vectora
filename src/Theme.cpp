#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

namespace {

QString uiFontFamily()
{
    const QStringList wanted = {
        QStringLiteral("Inter"),
        QStringLiteral("Adwaita Sans"),
        QStringLiteral("Noto Sans"),
        QStringLiteral("DejaVu Sans"),
    };
    const QStringList families = QFontDatabase::families();
    for (const QString &name : wanted) {
        if (families.contains(name))
            return name;
    }
    return QApplication::font().family();
}

const char *kStyleSheet = R"(
QMainWindow, QDialog {
    background: #1b1d21;
}
QWidget {
    color: #d7dbe2;
    font-size: 13px;
}
QMenuBar {
    background: #202226;
    border-bottom: 1px solid #2c2f35;
    padding: 2px 4px;
}
QMenuBar::item {
    background: transparent;
    padding: 5px 10px;
    border-radius: 5px;
}
QMenuBar::item:selected { background: #2e323a; }
QMenuBar::item:pressed { background: #35507d; }

QMenu {
    background: #24262b;
    border: 1px solid #34383f;
    padding: 6px;
}
QMenu::item {
    padding: 6px 26px 6px 26px;
    border-radius: 5px;
}
QMenu::item:selected { background: #35507d; }
QMenu::item:disabled { color: #6a6f78; }
QMenu::separator {
    height: 1px;
    background: #34383f;
    margin: 5px 8px;
}
QMenu::indicator { width: 14px; height: 14px; }

QToolBar {
    background: #202226;
    border: none;
    spacing: 2px;
    padding: 3px;
}
QToolBar#MainToolBar { border-bottom: 1px solid #2c2f35; }
QToolBar#PropertyBar {
    background: #24262b;
    border-bottom: 1px solid #2c2f35;
    padding: 4px 8px;
    spacing: 8px;
}
QToolBar::separator {
    background: #34383f;
    width: 1px;
    margin: 5px 6px;
}

QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 4px;
}
QToolButton:hover { background: #2f333a; }
QToolButton:pressed { background: #3a3f48; }
QToolButton:checked {
    background: #2d4a78;
    border: 1px solid #4f7cc0;
}
QToolButton:disabled { color: #5d626b; }

QDockWidget {
    color: #aeb4bf;
    titlebar-close-icon: none;
    titlebar-normal-icon: none;
}
QDockWidget::title {
    background: #232529;
    padding: 7px 10px;
    border-bottom: 1px solid #2c2f35;
}
QMainWindow::separator {
    background: #2c2f35;
    width: 4px;
    height: 4px;
}
QMainWindow::separator:hover { background: #4f7cc0; }

QStatusBar {
    background: #202226;
    border-top: 1px solid #2c2f35;
    min-height: 28px;
}
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #a9b0bb; }

QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
    background: #17191c;
    border: 1px solid #34383f;
    border-radius: 5px;
    padding: 3px 6px;
    min-height: 20px;
    selection-background-color: #35507d;
}
QComboBox:hover, QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover {
    border-color: #454a53;
}
QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border-color: #4f7cc0;
}
QComboBox::drop-down { border: none; width: 18px; }
QComboBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #9aa1ac;
    margin-right: 6px;
}
QComboBox QAbstractItemView {
    background: #24262b;
    border: 1px solid #34383f;
    padding: 4px;
    selection-background-color: #35507d;
    outline: none;
}

QPushButton {
    background: #2b2e34;
    border: 1px solid #3a3f47;
    border-radius: 6px;
    padding: 5px 14px;
    min-height: 20px;
}
QPushButton:hover { background: #33373f; }
QPushButton:pressed { background: #3b414b; }
QPushButton:disabled { color: #6a6f78; }
QPushButton#accent {
    background: #2f5da8;
    border: 1px solid #5286d6;
}

QCheckBox { spacing: 6px; }
QSlider::groove:horizontal {
    height: 4px;
    background: #34383f;
    border-radius: 2px;
}
QSlider::sub-page:horizontal { background: #4f7cc0; border-radius: 2px; }
QSlider::handle:horizontal {
    background: #cfd5de;
    width: 12px;
    height: 12px;
    margin: -5px 0;
    border-radius: 6px;
}

QTreeWidget, QListWidget {
    background: #1b1d21;
    border: none;
    outline: none;
}
QTreeWidget::item { padding: 4px 2px; border-radius: 4px; }
QTreeWidget::item:selected { background: #35507d; }
QTreeWidget::item:hover { background: #262a30; }

QTabWidget::pane { border: none; }
QTabBar::tab {
    background: transparent;
    color: #9aa1ac;
    padding: 6px 14px;
    border-bottom: 2px solid transparent;
}
QTabBar::tab:selected { color: #d7dbe2; border-bottom: 2px solid #4f7cc0; }

QScrollBar:vertical {
    background: #17181b;
    width: 12px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #3a3e45;
    border-radius: 6px;
    min-height: 30px;
    margin: 2px;
}
QScrollBar::handle:vertical:hover { background: #4a4f58; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

QScrollBar:horizontal {
    background: #17181b;
    height: 12px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: #3a3e45;
    border-radius: 6px;
    min-width: 30px;
    margin: 2px;
}
QScrollBar::handle:horizontal:hover { background: #4a4f58; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

QSplitter::handle { background: #2c2f35; }
QToolTip {
    background: #2b2e34;
    color: #e3e7ee;
    border: 1px solid #3b3f47;
    border-radius: 4px;
    padding: 5px 7px;
}
)";

} // namespace

QColor Theme::accent()
{
    return QColor(0x4f, 0x7c, 0xc0);
}

QColor Theme::panel()
{
    return QColor(0x20, 0x22, 0x26);
}

QColor Theme::canvas()
{
    return QColor(0x17, 0x18, 0x1b);
}

void Theme::apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QFont font(uiFontFamily(), 10);
    font.setHintingPreference(QFont::PreferFullHinting);
    app.setFont(font);

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x1b, 0x1d, 0x21));
    palette.setColor(QPalette::WindowText, QColor(0xd7, 0xdb, 0xe2));
    palette.setColor(QPalette::Base, QColor(0x17, 0x19, 0x1c));
    palette.setColor(QPalette::AlternateBase, QColor(0x20, 0x22, 0x26));
    palette.setColor(QPalette::Text, QColor(0xd7, 0xdb, 0xe2));
    palette.setColor(QPalette::Button, QColor(0x2b, 0x2e, 0x34));
    palette.setColor(QPalette::ButtonText, QColor(0xd7, 0xdb, 0xe2));
    palette.setColor(QPalette::Highlight, QColor(0x35, 0x50, 0x7d));
    palette.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::ToolTipBase, QColor(0x2b, 0x2e, 0x34));
    palette.setColor(QPalette::ToolTipText, QColor(0xe3, 0xe7, 0xee));
    palette.setColor(QPalette::PlaceholderText, QColor(0x6a, 0x6f, 0x78));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x6a, 0x6f, 0x78));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6a, 0x6f, 0x78));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6a, 0x6f, 0x78));
    app.setPalette(palette);

    app.setStyleSheet(QString::fromUtf8(kStyleSheet));
}
