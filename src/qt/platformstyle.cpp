// Copyright (c) 2015-2021 The CapStash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/platformstyle.h>

#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QImage>
#include <QPalette>
#include <QPixmap>
#include <QSize>
#include <QtGlobal>

static const struct {
    const char *platformId;
    /** Show images on push buttons */
    const bool imagesOnButtons;
    /** Colorize single-color icons */
    const bool colorizeIcons;
    /** Extra padding/spacing in transactionview */
    const bool useExtraSpacing;
} platform_styles[] = {
    {"macosx",  false, true,  true},
    {"windows", true,  true,  false},
    /* Other: linux, unix, ... */
    {"other",   true,  true,  false}
};

namespace {
/* Local functions for colorizing single-color images */

void MakeSingleColorImage(QImage& img, const QColor& colorbase)
{
    if (img.isNull()) return;

    img = img.convertToFormat(QImage::Format_ARGB32);
    for (int x = 0; x < img.width(); ++x) {
        for (int y = 0; y < img.height(); ++y) {
            const QRgb rgb = img.pixel(x, y);
            img.setPixel(x, y, qRgba(colorbase.red(), colorbase.green(), colorbase.blue(), qAlpha(rgb)));
        }
    }
}

QPixmap ColorizePixmap(const QPixmap& src, const QColor& colorbase)
{
    if (src.isNull()) return QPixmap();

    QImage img = src.toImage();
    MakeSingleColorImage(img, colorbase);
    return QPixmap::fromImage(img);
}

void AddPixmapForAllActiveStates(QIcon& icon, const QPixmap& px)
{
    if (px.isNull()) return;

    icon.addPixmap(px, QIcon::Normal,   QIcon::Off);
    icon.addPixmap(px, QIcon::Active,   QIcon::Off);
    icon.addPixmap(px, QIcon::Selected, QIcon::Off);
    icon.addPixmap(px, QIcon::Normal,   QIcon::On);
    icon.addPixmap(px, QIcon::Active,   QIcon::On);
    icon.addPixmap(px, QIcon::Selected, QIcon::On);

    // Let disabled be a slightly dimmed custom version instead of platform auto-grey.
    QImage disabled_img = px.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int x = 0; x < disabled_img.width(); ++x) {
        for (int y = 0; y < disabled_img.height(); ++y) {
            const QColor c = QColor::fromRgba(disabled_img.pixel(x, y));
            disabled_img.setPixel(
                x, y,
                qRgba(c.red(), c.green(), c.blue(), qBound(0, c.alpha() * 140 / 255, 255))
            );
        }
    }
    const QPixmap disabled_px = QPixmap::fromImage(disabled_img);
    icon.addPixmap(disabled_px, QIcon::Disabled, QIcon::Off);
    icon.addPixmap(disabled_px, QIcon::Disabled, QIcon::On);
}

QIcon ColorizeIcon(const QIcon& ico, const QColor& colorbase)
{
    QIcon new_ico;

    QList<QSize> sizes = ico.availableSizes();
    if (sizes.isEmpty()) {
        sizes << QSize(16, 16)
              << QSize(22, 22)
              << QSize(24, 24)
              << QSize(32, 32)
              << QSize(48, 48)
              << QSize(64, 64);
    }

    for (const QSize& sz : sizes) {
        const QPixmap base_px = ico.pixmap(sz, QIcon::Normal, QIcon::Off);
        if (base_px.isNull()) continue;

        const QPixmap colored_px = ColorizePixmap(base_px, colorbase);
        AddPixmapForAllActiveStates(new_ico, colored_px);
    }

    return new_ico;
}

QImage ColorizeImage(const QString& filename, const QColor& colorbase)
{
    QImage img(filename);
    MakeSingleColorImage(img, colorbase);
    return img;
}

QIcon ColorizeIcon(const QString& filename, const QColor& colorbase)
{
    QIcon source_icon(filename);
    if (!source_icon.isNull()) {
        return ColorizeIcon(source_icon, colorbase);
    }

    QImage img(filename);
    if (img.isNull()) {
        return QIcon();
    }

    MakeSingleColorImage(img, colorbase);
    QIcon icon;
    AddPixmapForAllActiveStates(icon, QPixmap::fromImage(img));
    return icon;
}

} // namespace

PlatformStyle::PlatformStyle(const QString &_name, bool _imagesOnButtons, bool _colorizeIcons, bool _useExtraSpacing) :
    name(_name),
    imagesOnButtons(_imagesOnButtons),
    colorizeIcons(_colorizeIcons),
    useExtraSpacing(_useExtraSpacing)
{
}

QColor PlatformStyle::TextColor() const
{
    return QApplication::palette().color(QPalette::WindowText);
}

QColor PlatformStyle::SingleColor() const
{
    if (colorizeIcons) {
        const QColor colorHighlightBg(QApplication::palette().color(QPalette::Highlight));
        const QColor colorHighlightFg(QApplication::palette().color(QPalette::HighlightedText));
        const QColor colorText(QApplication::palette().color(QPalette::WindowText));
        const int colorTextLightness = colorText.lightness();

        if (qAbs(colorHighlightBg.lightness() - colorTextLightness) <
            qAbs(colorHighlightFg.lightness() - colorTextLightness)) {
            return colorHighlightBg;
        }

        return colorHighlightFg;
    }

    return QColor(0, 0, 0);
}

QImage PlatformStyle::SingleColorImage(const QString& filename) const
{
    if (!colorizeIcons) {
        return QImage(filename);
    }
    return ColorizeImage(filename, SingleColor());
}

QIcon PlatformStyle::SingleColorIcon(const QString& filename) const
{
    if (!colorizeIcons) {
        return QIcon(filename);
    }
    return ColorizeIcon(filename, SingleColor());
}

QIcon PlatformStyle::SingleColorIcon(const QIcon& icon) const
{
    if (!colorizeIcons) {
        return icon;
    }
    return ColorizeIcon(icon, SingleColor());
}

QIcon PlatformStyle::TextColorIcon(const QIcon& icon) const
{
    return ColorizeIcon(icon, TextColor());
}

const PlatformStyle* PlatformStyle::instantiate(const QString &platformId)
{
    for (const auto& platform_style : platform_styles) {
        if (platformId == platform_style.platformId) {
            return new PlatformStyle(
                platform_style.platformId,
                platform_style.imagesOnButtons,
                platform_style.colorizeIcons,
                platform_style.useExtraSpacing
            );
        }
    }
    return nullptr;
}
