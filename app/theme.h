#pragma once

#include <QColor>
#include <QString>

// "Paper & Ink" UI theme tokens (direction A of the dashboard redesign):
// pure neutral black/white/gray surfaces, a single ink accent, and semantic
// status colors used only as small dots and labels. No blue-tinted darks.
namespace uitheme {

enum class Mode {
    System,
    Light,
    Dark
};

Mode modeFromSettings();
QString modeSettingValue(Mode mode);

struct Palette {
    bool dark = false;
    QColor window;        // page background
    QColor surface;       // cards, panes, popups
    QColor surfaceAlt;    // inset inputs, lists
    QColor border;        // hairline borders
    QColor borderStrong;  // hovered/focused borders
    QColor hover;         // hover fill on rows and buttons
    QColor pressed;       // pressed fill
    QColor text;          // primary text
    QColor textSecondary; // secondary text
    QColor textFaint;     // hints and notes
    QColor onAccent;      // text on accent fill
    QColor accent;        // single ink accent
    QColor accentHover;
    QColor accentPressed;
    QColor selectionBg;
    QColor selectionText;
    QColor tooltipBg;
    QColor tooltipText;
    QColor tooltipBorder;
    QColor statusOk;
    QColor statusWarn;
    QColor statusError;
    QColor dangerText;
    QColor dangerBorder;
    QColor dangerBg;
    QColor scrollHandle;
    QColor scrollHandleHover;
    QColor previewCard;         // pet preview canvas
    QColor previewCardBorder;
    QColor checkerA;            // transparency checkerboard
    QColor checkerB;
    QColor allowGreen;          // "Allow once" semantic action
    QColor allowGreenHover;
};

Palette paletteFor(bool dark);
Palette resolvePalette(Mode mode);

QString settingsDialogStyle(const Palette &palette);
QString permissionDialogStyle(const Palette &palette);

} // namespace uitheme
