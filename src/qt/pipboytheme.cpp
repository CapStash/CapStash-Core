// Portions Copyright (c) 2009-2021 The Bitcoin Core developers
// Portions Copyright (c) 2026 The CapStash Core & BitcoinII Core developers
//
// This file contains modifications and additions specific to CapStash.
// Original upstream portions remain subject to their original license terms.
// CapStash-specific original additions are not licensed for reuse,
// modification, or redistribution absent prior written permission
// from the copyright holder.
// Contact: satooshi@bitcoin-ii.org


#include "pipboytheme.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QIODevice>
#include <QPalette>
#include <QString>
#include <QStyle>
#include <QApplication>
#include <QPainter>
#include <QPolygon>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>

namespace PipBoyTheme {
namespace {

class PipBoyStyle : public QProxyStyle
{
public:
    explicit PipBoyStyle(QStyle* base = nullptr)
        : QProxyStyle(base)
    {
    }

    void drawPrimitive(PrimitiveElement elem,
                       const QStyleOption* option,
                       QPainter* painter,
                       const QWidget* widget = nullptr) const override
    {
        switch (elem) {
        case PE_IndicatorSpinUp:
        case PE_IndicatorSpinDown: {
            if (!option || !painter) return;

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(option->palette.buttonText());

            const QRect r = option->rect.adjusted(8, 8, -8, -8);
            QPolygon poly;

            if (elem == PE_IndicatorSpinUp) {
                poly << QPoint(r.left(),  r.top())
                     << QPoint(r.right(), r.top())
                     << QPoint(r.center().x(), r.bottom());
            } else {
                poly << QPoint(r.center().x(), r.top())
                     << QPoint(r.left(),  r.bottom())
                     << QPoint(r.right(), r.bottom());
            }

            painter->drawPolygon(poly);
            painter->restore();
            return;
        }

        default:
            break;
        }

        QProxyStyle::drawPrimitive(elem, option, painter, widget);
    }
};

Mode g_current_mode = Mode::Green;

QString ToHex(const QColor& color)
{
    return color.name(QColor::HexRgb).toUpper();
}

QString ToCssColor(const QColor& color)
{
    if (color.alpha() == 255) {
        return ToHex(color);
    }

    return QString("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 2));
}

QString BuildStyleSheet(const Palette& p)
{
    QFile f(":/css/pipboy.qss");
    if (!f.exists()) {
        qWarning() << "[PipBoyTheme] pipboy.qss missing from resources";
        return QString();
    }

    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[PipBoyTheme] Failed to open pipboy.qss";
        return QString();
    }

    QString qss = QString::fromUtf8(f.readAll());
    f.close();

    qss.replace("%BG%", ToCssColor(p.bg));
    qss.replace("%PANEL%", ToCssColor(p.panel));
    qss.replace("%TEXT%", ToCssColor(p.text));
    qss.replace("%ACCENT%", ToCssColor(p.accent));
    qss.replace("%SELECTION_BG%", ToCssColor(p.selection_bg));
    qss.replace("%SELECTION_TEXT%", ToCssColor(p.selection_text));
    qss.replace("%BORDER%", ToCssColor(p.border));
    qss.replace("%ALT_PANEL%", ToCssColor(p.alt_panel));
    qss.replace("%PRESSED%", ToCssColor(p.pressed));
    qss.replace("%LINK%", ToCssColor(p.link));
    qss.replace("%MODAL_BG%", ToCssColor(p.modal_bg));
    qss.replace("%MODAL_BORDER%", ToCssColor(p.modal_border));

    return qss;
}

void ApplyPaletteToApp(const Palette& p)
{
    QPalette pal;
    pal.setColor(QPalette::Window,          p.bg);
    pal.setColor(QPalette::Base,            p.panel);
    pal.setColor(QPalette::AlternateBase,   p.alt_panel);
    pal.setColor(QPalette::WindowText,      p.text);
    pal.setColor(QPalette::Text,            p.text);
    pal.setColor(QPalette::Button,          p.panel.lighter(108));
    pal.setColor(QPalette::ButtonText,      p.text);
    pal.setColor(QPalette::Highlight,       p.selection_bg.lighter(118));
    pal.setColor(QPalette::HighlightedText, p.selection_text);
    pal.setColor(QPalette::Link,            QColor(0xFF, 0x3B, 0x3B));
    pal.setColor(QPalette::LinkVisited,     QColor(0xFF, 0x3B, 0x3B));

    // Stronger Fusion phosphor "glow" / bevel energy.
    pal.setColor(QPalette::Light,           p.text.lighter(160));
    pal.setColor(QPalette::Midlight,        p.accent.lighter(135));
    pal.setColor(QPalette::Mid,             p.border.lighter(118));
    pal.setColor(QPalette::Dark,            p.bg.darker(135));
    pal.setColor(QPalette::Shadow,          p.bg.darker(185));

    qApp->setPalette(pal);
}

void ApplyPaletteToRoot(const Palette& p, QWidget* root)
{
    if (!root) return;

    QPalette pal = root->palette();
    pal.setColor(QPalette::Window,          p.bg);
    pal.setColor(QPalette::Base,            p.panel);
    pal.setColor(QPalette::AlternateBase,   p.alt_panel);
    pal.setColor(QPalette::WindowText,      p.text);
    pal.setColor(QPalette::Text,            p.text);
    pal.setColor(QPalette::Button,          p.panel.lighter(108));
    pal.setColor(QPalette::ButtonText,      p.text);
    pal.setColor(QPalette::Highlight,       p.selection_bg.lighter(118));
    pal.setColor(QPalette::HighlightedText, p.selection_text);
    pal.setColor(QPalette::Link,            QColor(0xFF, 0x3B, 0x3B));
    pal.setColor(QPalette::LinkVisited,     QColor(0xFF, 0x3B, 0x3B));

    // Stronger Fusion phosphor "glow" / bevel energy.
    pal.setColor(QPalette::Light,           p.text.lighter(160));
    pal.setColor(QPalette::Midlight,        p.accent.lighter(135));
    pal.setColor(QPalette::Mid,             p.border.lighter(118));
    pal.setColor(QPalette::Dark,            p.bg.darker(135));
    pal.setColor(QPalette::Shadow,          p.bg.darker(185));

    root->setPalette(pal);
}

void ResetToClassic(QWidget* root)
{
    qApp->setStyleSheet(QString());

    if (QStyle* style = QApplication::style()) {
        qApp->setPalette(style->standardPalette());
        if (root) {
            root->setPalette(style->standardPalette());
        }
    } else {
        qApp->setPalette(QPalette());
        if (root) {
            root->setPalette(QPalette());
        }
    }
}

} // namespace

Palette GetPalette(Mode mode)
{
    switch (mode) {
    case Mode::Amber:
        return Palette{
            QColor(0x08, 0x07, 0x03),      // bg
            QColor(0x10, 0x0C, 0x05),      // panel
            QColor(0xF0, 0xB0, 0x4A),      // text
            QColor(0xE2, 0x98, 0x2E),      // accent
            QColor(0xA8, 0x62, 0x18),      // selection_bg
            QColor(0x08, 0x07, 0x03),      // selection_text
            QColor(0x4A, 0x31, 0x0D),      // border
            QColor(0x16, 0x11, 0x07),      // alt_panel
            QColor(0x22, 0x16, 0x08),      // pressed
            QColor(0xF2, 0xA0, 0x38),      // link
            QColor(8, 7, 3, 245),          // modal_bg
            QColor(0xA8, 0x62, 0x18)       // modal_border
        };

    case Mode::Green:
        return Palette{
            QColor(0x06, 0x0D, 0x06),      // bg
            QColor(0x0A, 0x12, 0x0A),      // panel
            QColor(0x98, 0xD8, 0x98),      // text
            QColor(0x7F, 0xC0, 0x7F),      // accent
            QColor(0x46, 0x82, 0x46),      // selection_bg
            QColor(0x06, 0x0D, 0x06),      // selection_text
            QColor(0x22, 0x38, 0x22),      // border
            QColor(0x0D, 0x18, 0x0D),      // alt_panel
            QColor(0x15, 0x24, 0x15),      // pressed
            QColor(0x8C, 0xE0, 0x8C),      // link
            QColor(6, 13, 6, 245),         // modal_bg
            QColor(0x46, 0x82, 0x46)       // modal_border
        };

    case Mode::Classic:
    default:
        return Palette{
            QColor(), QColor(), QColor(), QColor(),
            QColor(), QColor(), QColor(), QColor(),
            QColor(), QColor(), QColor(), QColor()
        };
    }
}

Mode CurrentMode()
{
    return g_current_mode;
}

void SetMode(Mode mode)
{
    g_current_mode = mode;
}

void Apply(Mode mode, QWidget* root)
{
    if (!QCoreApplication::instance()) {
        qWarning() << "[PipBoyTheme] QApplication not yet constructed";
        return;
    }

    g_current_mode = mode;

    if (mode == Mode::Classic) {
        ResetToClassic(root);
        return;
    }

    if (dynamic_cast<PipBoyStyle*>(qApp->style()) == nullptr) {
    if (QStyle* base = QStyleFactory::create("fusion")) {
        qApp->setStyle(new PipBoyStyle(base));
    } else {
        QApplication::setStyle("fusion");
    }
    }

    const Palette p = GetPalette(mode);

    ApplyPaletteToApp(p);
    ApplyPaletteToRoot(p, root);

    const QString qss = BuildStyleSheet(p);
    if (!qss.isEmpty()) {
        qApp->setStyleSheet(qss);
    } else {
        qApp->setStyleSheet(QString());
    }
}

void Apply(QWidget* root)
{
    Apply(g_current_mode, root);
}

} // namespace PipBoyTheme
