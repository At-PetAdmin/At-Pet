#include "theme.h"

#include <QGuiApplication>
#include <QList>
#include <QPair>
#include <QPalette>
#include <QSettings>

namespace uitheme {
namespace {

QString hex(const QColor &color) {
    return color.name(QColor::HexRgb);
}

QString alpha(const QColor &color, int alphaValue) {
    QColor blended = color;
    blended.setAlpha(alphaValue);
    return blended.name(QColor::HexArgb);
}

QString expandTokens(QString text, const Palette &p) {
    const QList<QPair<QLatin1String, QString>> tokens = {
        {QLatin1String("@window@"), hex(p.window)},
        {QLatin1String("@surface@"), hex(p.surface)},
        {QLatin1String("@surfaceAlt@"), hex(p.surfaceAlt)},
        {QLatin1String("@border@"), hex(p.border)},
        {QLatin1String("@borderStrong@"), hex(p.borderStrong)},
        {QLatin1String("@hover@"), hex(p.hover)},
        {QLatin1String("@pressed@"), hex(p.pressed)},
        {QLatin1String("@text@"), hex(p.text)},
        {QLatin1String("@textSecondary@"), hex(p.textSecondary)},
        {QLatin1String("@textFaint@"), hex(p.textFaint)},
        {QLatin1String("@onAccent@"), hex(p.onAccent)},
        {QLatin1String("@accent@"), hex(p.accent)},
        {QLatin1String("@accentHover@"), hex(p.accentHover)},
        {QLatin1String("@accentPressed@"), hex(p.accentPressed)},
        {QLatin1String("@selectionBg@"), hex(p.selectionBg)},
        {QLatin1String("@selectionText@"), hex(p.selectionText)},
        {QLatin1String("@tooltipBg@"), hex(p.tooltipBg)},
        {QLatin1String("@tooltipText@"), hex(p.tooltipText)},
        {QLatin1String("@tooltipBorder@"), hex(p.tooltipBorder)},
        {QLatin1String("@dangerText@"), hex(p.dangerText)},
        {QLatin1String("@dangerBorder@"), alpha(p.dangerText, p.dark ? 96 : 80)},
        {QLatin1String("@dangerBg@"), alpha(p.dangerText, p.dark ? 28 : 16)},
        {QLatin1String("@scrollHandle@"), hex(p.scrollHandle)},
        {QLatin1String("@scrollHandleHover@"), hex(p.scrollHandleHover)},
    };
    for (const auto &token : tokens)
        text.replace(token.first, token.second);
    return text;
}

Palette lightPalette() {
    Palette p;
    p.dark = false;
    p.window = QColor(0xfa, 0xfa, 0xfa);
    p.surface = QColor(0xff, 0xff, 0xff);
    p.surfaceAlt = QColor(0xf5, 0xf5, 0xf5);
    p.border = QColor(0xe4, 0xe4, 0xe4);
    p.borderStrong = QColor(0xc9, 0xc9, 0xc9);
    p.hover = QColor(0xec, 0xec, 0xec);
    p.pressed = QColor(0xe0, 0xe0, 0xe0);
    p.text = QColor(0x17, 0x17, 0x17);
    p.textSecondary = QColor(0x6f, 0x6f, 0x6f);
    p.textFaint = QColor(0x8f, 0x8f, 0x8f);
    p.onAccent = QColor(0xff, 0xff, 0xff);
    p.accent = QColor(0x17, 0x17, 0x17);
    p.accentHover = QColor(0x30, 0x30, 0x30);
    p.accentPressed = QColor(0x0a, 0x0a, 0x0a);
    p.selectionBg = QColor(0xe4, 0xe4, 0xe4);
    p.selectionText = QColor(0x17, 0x17, 0x17);
    p.tooltipBg = QColor(0x17, 0x17, 0x17);
    p.tooltipText = QColor(0xfa, 0xfa, 0xfa);
    p.tooltipBorder = QColor(0x17, 0x17, 0x17);
    p.statusOk = QColor(0x16, 0xa3, 0x4a);
    p.statusWarn = QColor(0xd9, 0x77, 0x06);
    p.statusError = QColor(0xdc, 0x26, 0x26);
    p.dangerText = QColor(0xdc, 0x26, 0x26);
    p.scrollHandle = QColor(0xd6, 0xd6, 0xd6);
    p.scrollHandleHover = QColor(0xbd, 0xbd, 0xbd);
    p.previewCard = QColor(0xff, 0xff, 0xff);
    p.previewCardBorder = QColor(0xe4, 0xe4, 0xe4);
    p.checkerA = QColor(0xf0, 0xf0, 0xf0);
    p.checkerB = QColor(0xe7, 0xe7, 0xe7);
    p.allowGreen = QColor(0x16, 0xa3, 0x4a);
    p.allowGreenHover = QColor(0x15, 0x80, 0x3d);
    return p;
}

Palette darkPalette() {
    Palette p;
    p.dark = true;
    p.window = QColor(0x0d, 0x0d, 0x0d);
    p.surface = QColor(0x16, 0x16, 0x16);
    p.surfaceAlt = QColor(0x1d, 0x1d, 0x1d);
    p.border = QColor(0x2a, 0x2a, 0x2a);
    p.borderStrong = QColor(0x3f, 0x3f, 0x3f);
    p.hover = QColor(0x22, 0x22, 0x22);
    p.pressed = QColor(0x19, 0x19, 0x19);
    p.text = QColor(0xed, 0xed, 0xed);
    p.textSecondary = QColor(0x9b, 0x9b, 0x9b);
    p.textFaint = QColor(0x7a, 0x7a, 0x7a);
    p.onAccent = QColor(0x11, 0x11, 0x11);
    p.accent = QColor(0xed, 0xed, 0xed);
    p.accentHover = QColor(0xff, 0xff, 0xff);
    p.accentPressed = QColor(0xcf, 0xcf, 0xcf);
    p.selectionBg = QColor(0x2f, 0x2f, 0x2f);
    p.selectionText = QColor(0xed, 0xed, 0xed);
    p.tooltipBg = QColor(0x26, 0x26, 0x26);
    p.tooltipText = QColor(0xed, 0xed, 0xed);
    p.tooltipBorder = QColor(0x3a, 0x3a, 0x3a);
    p.statusOk = QColor(0x3f, 0xb9, 0x50);
    p.statusWarn = QColor(0xd2, 0x99, 0x22);
    p.statusError = QColor(0xf8, 0x51, 0x49);
    p.dangerText = QColor(0xf8, 0x51, 0x49);
    p.scrollHandle = QColor(0x33, 0x33, 0x33);
    p.scrollHandleHover = QColor(0x45, 0x45, 0x45);
    p.previewCard = QColor(0x16, 0x16, 0x16);
    p.previewCardBorder = QColor(0x2a, 0x2a, 0x2a);
    p.checkerA = QColor(0x1c, 0x1c, 0x1c);
    p.checkerB = QColor(0x14, 0x14, 0x14);
    p.allowGreen = QColor(0x23, 0x86, 0x36);
    p.allowGreenHover = QColor(0x2e, 0xa0, 0x43);
    return p;
}

} // namespace

Mode modeFromSettings() {
    const QString value = QSettings().value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString();
    if (value == QLatin1String("light"))
        return Mode::Light;
    if (value == QLatin1String("dark"))
        return Mode::Dark;
    return Mode::System;
}

QString modeSettingValue(Mode mode) {
    switch (mode) {
    case Mode::Light:
        return QStringLiteral("light");
    case Mode::Dark:
        return QStringLiteral("dark");
    case Mode::System:
        break;
    }
    return QStringLiteral("system");
}

Palette paletteFor(bool dark) {
    return dark ? darkPalette() : lightPalette();
}

Palette resolvePalette(Mode mode) {
    if (mode == Mode::System)
        return paletteFor(QGuiApplication::palette().window().color().lightness() < 128);
    return paletteFor(mode == Mode::Dark);
}

QString settingsDialogStyle(const Palette &p) {
    return expandTokens(QStringLiteral(R"qss(
QDialog { background-color: @window@; }
QWidget { background: transparent; color: @text@; font-family: 'Segoe UI', 'Microsoft YaHei', sans-serif; font-size: 13px; }
QToolTip { background-color: @tooltipBg@; color: @tooltipText@; border: 1px solid @tooltipBorder@; padding: 4px 6px; }
QTabWidget::pane { border: 1px solid @border@; background: @surface@; border-radius: 10px; top: -1px; }
QTabBar { background: transparent; }
QTabBar::tab { background: transparent; color: @textSecondary@; padding: 10px 22px; margin-right: 2px; border: none; border-bottom: 2px solid transparent; font-weight: 600; }
QTabBar::tab:selected { color: @text@; border-bottom: 2px solid @accent@; }
QTabBar::tab:hover:!selected { color: @text@; border-bottom: 2px solid @borderStrong@; }
QGroupBox { font-weight: 600; border: 1px solid @border@; border-radius: 10px; margin-top: 14px; padding: 20px 14px 14px 14px; color: @text@; background: @surface@; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 14px; padding: 0 6px; }
QListWidget { background: @surfaceAlt@; border: 1px solid @border@; border-radius: 8px; color: @text@; padding: 4px; outline: none; }
QListWidget::item { padding: 9px 12px; border-radius: 6px; margin-bottom: 2px; }
QListWidget::item:hover { background: @hover@; }
QListWidget::item:selected { background: @selectionBg@; color: @selectionText@; }
QPushButton { background: @surface@; border: 1px solid @border@; border-radius: 7px; color: @text@; padding: 8px 18px; font-weight: 500; }
QPushButton:hover { background: @hover@; border-color: @borderStrong@; }
QPushButton:pressed { background: @pressed@; }
QPushButton:focus { border-color: @accent@; }
QPushButton:disabled { background: transparent; color: @textFaint@; border-color: @border@; }
QPushButton#primaryBtn { background: @accent@; border: 1px solid @accent@; font-weight: 600; color: @onAccent@; }
QPushButton#primaryBtn:hover { background: @accentHover@; border-color: @accentHover@; }
QPushButton#primaryBtn:pressed { background: @accentPressed@; }
QPushButton#dangerBtn { background: transparent; border: 1px solid @dangerBorder@; font-weight: 500; color: @dangerText@; }
QPushButton#dangerBtn:hover { background: @dangerBg@; border-color: @dangerText@; }
QPushButton#dangerBtn:pressed { background: @dangerBg@; color: @dangerText@; }
QComboBox, QSpinBox { background: @surfaceAlt@; border: 1px solid @border@; border-radius: 6px; color: @text@; padding: 6px 10px; min-height: 20px; selection-background-color: @selectionBg@; selection-color: @selectionText@; }
QComboBox:hover, QSpinBox:hover { border-color: @borderStrong@; }
QComboBox:focus, QSpinBox:focus { border-color: @accent@; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 24px; border: none; }
QComboBox QAbstractItemView { background: @surface@; border: 1px solid @border@; border-radius: 6px; color: @text@; selection-background-color: @selectionBg@; selection-color: @selectionText@; padding: 4px; outline: none; }
QCheckBox { color: @text@; spacing: 10px; }
QCheckBox::indicator { width: 18px; height: 18px; border-radius: 5px; border: 1px solid @borderStrong@; background: @surfaceAlt@; }
QCheckBox::indicator:hover { border-color: @accent@; }
QCheckBox::indicator:checked { background: @accent@; border-color: @accent@; }
QCheckBox::indicator:disabled { border-color: @border@; background: transparent; }
QLabel { color: @textSecondary@; }
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @scrollHandle@; border-radius: 4px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: @scrollHandleHover@; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    )qss"), p);
}

QString permissionDialogStyle(const Palette &p) {
    const QString allow = hex(p.allowGreen);
    return expandTokens(QStringLiteral(R"qss(
QDialog { background: @surface@; color: @text@; }
QLabel { color: @text@; }
QPushButton { min-width: 100px; padding: 7px 12px; border-radius: 6px; background: @surfaceAlt@; border: 1px solid @border@; color: @text@; }
QPushButton:hover { background: @hover@; border-color: @borderStrong@; }
QPushButton#allowOnceBtn { background: %1; color: #ffffff; border: 1px solid %1; font-weight: 600; }
QPushButton#allowOnceBtn:hover { background: %2; border-color: %2; }
QPushButton#denyBtn { background: @surfaceAlt@; border: 1px solid @border@; color: @text@; }
    )qss").arg(allow, hex(p.allowGreenHover)), p);
}

} // namespace uitheme
