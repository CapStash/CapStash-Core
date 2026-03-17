// Portions Copyright (c) 2009-2021 The Bitcoin Core developers
// Portions Copyright (c) 2026 The CapStash Core & BitcoinII Core developers
//
// This file contains modifications and additions specific to CapStash.
// Original upstream portions remain subject to their original license terms.
// CapStash-specific original additions are not licensed for reuse,
// modification, or redistribution absent prior written permission
// from the copyright holder.
// Contact: satooshi@bitcoin-ii.org

#pragma once

#include <QApplication>
#include <QColor>
#include <QWidget>

namespace PipBoyTheme {

enum class Mode {
    Classic = 0,
    Green,
    Amber
};

struct Palette {
    QColor bg;
    QColor panel;
    QColor text;
    QColor accent;
    QColor selection_bg;
    QColor selection_text;
    QColor border;
    QColor alt_panel;
    QColor pressed;
    QColor link;
    QColor modal_bg;
    QColor modal_border;
};

Palette GetPalette(Mode mode);
Mode CurrentMode();
void SetMode(Mode mode);

void Apply(Mode mode, QWidget* root = nullptr);
void Apply(QWidget* root = nullptr);

} // namespace PipBoyTheme
