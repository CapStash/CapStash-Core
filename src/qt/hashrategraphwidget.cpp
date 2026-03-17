// Portions Copyright (c) 2009-2021 The Bitcoin Core developers
// Portions Copyright (c) 2026 The CapStash Core & BitcoinII Core developers
//
// This file contains modifications and additions specific to CapStash.
// Original upstream portions remain subject to their original license terms.
// CapStash-specific original additions are not licensed for reuse,
// modification, or redistribution absent prior written permission
// from the copyright holder.
// Contact: satooshi@bitcoin-ii.org

#include <qt/hashrategraphwidget.h>
#include <qt/pipboytheme.h>

#include <QColor>
#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr int LEFT_MARGIN   = 10;
constexpr int RIGHT_MARGIN  = 10;
constexpr int TOP_MARGIN    = 64; // extra room for diff-hit stats + 25x25 markers
constexpr int BOTTOM_MARGIN = 18;
constexpr int MARKER_SIZE   = 25;

const PipBoyTheme::Palette& CurrentThemePalette()
{
    static thread_local PipBoyTheme::Palette cached;
    cached = PipBoyTheme::GetPalette(PipBoyTheme::CurrentMode());
    return cached;
}

QColor WithAlpha(const QColor& c, int alpha)
{
    QColor out(c);
    out.setAlpha(alpha);
    return out;
}

static QString FormatDiffHit(double diff)
{
    if (!std::isfinite(diff) || diff < 0.0) {
        diff = 0.0;
    }

    int decimals = 8;
    if (diff >= 1000000.0) {
        decimals = 2;
    } else if (diff >= 10000.0) {
        decimals = 4;
    } else if (diff >= 100.0) {
        decimals = 6;
    }

    return QString::number(diff, 'f', decimals);
}
} // namespace

HashrateGraphWidget::HashrateGraphWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
}

std::chrono::minutes HashrateGraphWidget::getGraphRange() const
{
    return m_range;
}

std::chrono::milliseconds HashrateGraphWidget::sampleInterval() const
{
    return std::chrono::milliseconds{SAMPLE_INTERVAL_MS};
}

qint64 HashrateGraphWidget::currentTimeMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

qint64 HashrateGraphWidget::visibleRangeMs() const
{
    qint64 range_ms = std::chrono::duration_cast<std::chrono::milliseconds>(m_range).count();
    if (range_ms <= 0) range_ms = 60ll * 1000ll;
    if (range_ms > HISTORY_RETENTION_MS) range_ms = HISTORY_RETENTION_MS;
    return range_ms;
}

qint64 HashrateGraphWidget::visibleEndMs() const
{
    if (m_liveMode || m_viewEndTimeMs <= 0) {
        return currentTimeMs();
    }
    return m_viewEndTimeMs;
}

qint64 HashrateGraphWidget::visibleStartMs() const
{
    return visibleEndMs() - visibleRangeMs();
}

QRect HashrateGraphWidget::plotRect() const
{
    const int w = std::max(1, width() - LEFT_MARGIN - RIGHT_MARGIN);
    const int h = std::max(1, height() - TOP_MARGIN - BOTTOM_MARGIN);
    return QRect(LEFT_MARGIN, TOP_MARGIN, w, h);
}

void HashrateGraphWidget::setGraphRange(std::chrono::minutes new_range)
{
    if (new_range <= std::chrono::minutes{0}) {
        new_range = std::chrono::minutes{1};
    }

    const auto max_range = std::chrono::duration_cast<std::chrono::minutes>(
        std::chrono::milliseconds{HISTORY_RETENTION_MS});

    if (new_range > max_range) {
        new_range = max_range;
    }

    m_range = new_range;

    if (m_liveMode) {
        m_viewEndTimeMs = currentTimeMs();
    }
    clampViewport();
    update();
}

void HashrateGraphWidget::clear()
{
    m_samples.clear();
    m_blockMarkers.clear();
    m_peakSample = 0.0f;
    m_displayMax = 0.0f;
    m_viewEndTimeMs = currentTimeMs();
    m_liveMode = true;
    m_isPanning = false;
    m_bestDiffHit = 0.0;
    update();
}

void HashrateGraphWidget::setBestDiffHit(double best_diff_hit)
{
    if (!std::isfinite(best_diff_hit) || best_diff_hit < 0.0) {
        best_diff_hit = 0.0;
    }

    m_bestDiffHit = best_diff_hit;
    update();
}

void HashrateGraphWidget::pruneOldHistory(qint64 now_ms)
{
    const qint64 cutoff = now_ms - HISTORY_RETENTION_MS;

    while (!m_samples.isEmpty() && m_samples.front().timestamp_ms < cutoff) {
        m_samples.removeFirst();
    }

    while (!m_blockMarkers.isEmpty() && m_blockMarkers.front().timestamp_ms < cutoff) {
        m_blockMarkers.removeFirst();
    }
}

void HashrateGraphWidget::setHashrate(double hashps)
{
    if (!std::isfinite(hashps) || hashps < 0.0) {
        hashps = 0.0;
    }

    const qint64 now_ms = currentTimeMs();
    const float rate = static_cast<float>(hashps);

    if (m_samples.isEmpty()) {
        m_samples.push_back({now_ms, rate});
    } else {
        HashrateSample& last = m_samples.last();
        if ((now_ms - last.timestamp_ms) >= SAMPLE_INTERVAL_MS) {
            m_samples.push_back({now_ms, rate});
        } else {
            last.timestamp_ms = now_ms;
            last.hashps = rate;
        }
    }

    pruneOldHistory(now_ms);

    if (m_liveMode || m_viewEndTimeMs <= 0) {
        m_viewEndTimeMs = now_ms;
    }

    clampViewport();
    update();
}

void HashrateGraphWidget::addBlockMarker(qint64 timestamp_ms, int height, const QString& time_text, const QString& hash_hex)
{
    if (timestamp_ms <= 0 || height <= 0) {
        return;
    }

    if (!m_blockMarkers.isEmpty()) {
        const BlockMarker& last = m_blockMarkers.constLast();
        if (last.timestamp_ms == timestamp_ms && last.height == height && last.hash_hex == hash_hex) {
            return;
        }
    }

    BlockMarker marker;
    marker.timestamp_ms = timestamp_ms;
    marker.height = height;
    marker.time_text = time_text;
    marker.hash_hex = hash_hex;
    m_blockMarkers.push_back(std::move(marker));

    pruneOldHistory(currentTimeMs());
    update();
}

void HashrateGraphWidget::clearBlockMarkers()
{
    m_blockMarkers.clear();
    update();
}

void HashrateGraphWidget::jumpToLive()
{
    m_liveMode = true;
    m_viewEndTimeMs = currentTimeMs();
    clampViewport();
    update();
}

float HashrateGraphWidget::computeDisplayMax(float peak) const
{
    if (peak <= 0.0f) {
        return 0.0f;
    }

    const float padded = peak * 1.15f;
    const float exponent = std::floor(std::log10(padded));
    const float magnitude = std::pow(10.0f, exponent);
    const float normalized = padded / magnitude;

    float rounded = 1.0f;
    if (normalized <= 1.0f) {
        rounded = 1.0f;
    } else if (normalized <= 2.0f) {
        rounded = 2.0f;
    } else if (normalized <= 5.0f) {
        rounded = 5.0f;
    } else {
        rounded = 10.0f;
    }

    return rounded * magnitude;
}

QString HashrateGraphWidget::formatHashrateLabel(float hashps) const
{
    if (!std::isfinite(hashps) || hashps < 0.0f) {
        hashps = 0.0f;
    }

    static const char* units[] = {"H/s", "kH/s", "MH/s", "GH/s", "TH/s", "PH/s"};
    int unit_index = 0;
    double value = static_cast<double>(hashps);

    while (value >= 1000.0 && unit_index < 5) {
        value /= 1000.0;
        ++unit_index;
    }

    int decimals = 2;
    if (value >= 100.0) {
        decimals = 0;
    } else if (value >= 10.0) {
        decimals = 1;
    }

    return QString("%1 %2")
        .arg(QString::number(value, 'f', decimals))
        .arg(units[unit_index]);
}

int HashrateGraphWidget::mapX(qint64 timestamp_ms, const QRect& plot_rect, qint64 view_start_ms, qint64 view_end_ms) const
{
    if (view_end_ms <= view_start_ms) {
        return plot_rect.left();
    }

    const double frac = double(timestamp_ms - view_start_ms) / double(view_end_ms - view_start_ms);
    const double clamped = std::clamp(frac, 0.0, 1.0);
    return plot_rect.left() + static_cast<int>(clamped * plot_rect.width());
}

int HashrateGraphWidget::mapY(float hashps, const QRect& plot_rect, float display_max) const
{
    if (display_max <= 0.0f) {
        return plot_rect.bottom();
    }

    const float clamped = std::clamp(hashps, 0.0f, display_max);
    return plot_rect.bottom() - static_cast<int>((clamped / display_max) * plot_rect.height());
}

void HashrateGraphWidget::clearMarkerHitRects()
{
    for (BlockMarker& marker : m_blockMarkers) {
        marker.hit_rect = QRect();
    }
}

void HashrateGraphWidget::clampViewport()
{
    const qint64 now_ms = currentTimeMs();
    const qint64 range_ms = visibleRangeMs();

    const qint64 oldest_ms = m_samples.isEmpty() ? (now_ms - HISTORY_RETENTION_MS) : m_samples.front().timestamp_ms;
    const qint64 min_end = oldest_ms + range_ms;
    const qint64 max_end = now_ms;

    if (m_liveMode) {
        m_viewEndTimeMs = max_end;
        return;
    }

    if (m_viewEndTimeMs < min_end) {
        m_viewEndTimeMs = min_end;
    }
    if (m_viewEndTimeMs >= max_end) {
        m_viewEndTimeMs = max_end;
        m_liveMode = true;
    }
}

void HashrateGraphWidget::paintEvent(QPaintEvent*)
{
    const auto& pal = CurrentThemePalette();

    QPainter painter(this);
    painter.fillRect(rect(), pal.bg);

    const QRect plot = plotRect();
    const qint64 view_end_ms = visibleEndMs();
    const qint64 view_start_ms = view_end_ms - visibleRangeMs();

    QColor axisCol = pal.border;
    QColor textCol = pal.text;
    QColor lineCol = pal.accent;
    QColor fillCol = WithAlpha(pal.accent, 80);
    QColor markerCol(255, 180, 0);
    QColor markerTextCol = pal.selection_text;
    QColor guideCol = WithAlpha(pal.accent, 90);

    // Top-left mining hit stats
    painter.setPen(pal.accent);
    painter.drawText(
        LEFT_MARGIN,
        22,
        QString("Best Diff Hit This Session: %1").arg(FormatDiffHit(m_bestDiffHit))
    );

    painter.setPen(axisCol);
    painter.drawRect(plot);

    // Determine visible peak only from samples in current viewport.
    float visible_peak = 0.0f;
    int visible_count = 0;
    for (const HashrateSample& s : m_samples) {
        if (s.timestamp_ms >= view_start_ms && s.timestamp_ms <= view_end_ms) {
            visible_peak = std::max(visible_peak, s.hashps);
            ++visible_count;
        }
    }

    m_peakSample = visible_peak;
    m_displayMax = computeDisplayMax(visible_peak);

    // Horizontal grid / Y labels.
    if (m_displayMax > 0.0f) {
        const int divisions = 5;
        for (int i = 1; i <= divisions; ++i) {
            const float yValue = (m_displayMax / divisions) * i;
            const int yy = mapY(yValue, plot, m_displayMax);

            painter.setPen(axisCol);
            painter.drawLine(plot.left(), yy, plot.right(), yy);

            painter.setPen(textCol);
            painter.drawText(plot.left() + 4, yy - 2, formatHashrateLabel(yValue));
        }
    }

    clearMarkerHitRects();

    // Draw hashrate trace.
    if (visible_count > 0 && m_displayMax > 0.0f) {
        QPainterPath fillPath;
        QPainterPath linePath;
        bool started = false;
        int last_x = plot.left();

        for (const HashrateSample& s : m_samples) {
            if (s.timestamp_ms < view_start_ms || s.timestamp_ms > view_end_ms) {
                continue;
            }

            const int x = mapX(s.timestamp_ms, plot, view_start_ms, view_end_ms);
            const int y = mapY(s.hashps, plot, m_displayMax);

            if (!started) {
                fillPath.moveTo(x, plot.bottom());
                fillPath.lineTo(x, y);
                linePath.moveTo(x, y);
                started = true;
            } else {
                fillPath.lineTo(x, y);
                linePath.lineTo(x, y);
            }

            last_x = x;
        }

        if (started) {
            fillPath.lineTo(last_x, plot.bottom());
            fillPath.closeSubpath();

            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.fillPath(fillPath, fillCol);

            QPen pen(lineCol);
            pen.setWidth(2);
            painter.setPen(pen);
            painter.drawPath(linePath);
        }
    }

    // Draw block-hit markers in a top overlay lane with guide lines.
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont markerFont = painter.font();
    markerFont.setPointSize(std::max(7, markerFont.pointSize() - 1));
    painter.setFont(markerFont);

    for (BlockMarker& marker : m_blockMarkers) {
        if (marker.timestamp_ms < view_start_ms || marker.timestamp_ms > view_end_ms) {
            continue;
        }

        const int x = mapX(marker.timestamp_ms, plot, view_start_ms, view_end_ms);

        QRect markerRect(
            x - (MARKER_SIZE / 2),
            plot.top() - MARKER_SIZE - 6,
            MARKER_SIZE,
            MARKER_SIZE
        );

        if (markerRect.left() < plot.left()) {
            markerRect.moveLeft(plot.left());
        }
        if (markerRect.right() > plot.right()) {
            markerRect.moveRight(plot.right());
        }

        marker.hit_rect = markerRect;

        painter.setPen(QPen(guideCol, 1, Qt::DashLine));
        painter.drawLine(x, markerRect.bottom(), x, plot.bottom());

        QPen markerBorder(pal.border);
        markerBorder.setWidth(1);
        painter.setPen(markerBorder);
        painter.setBrush(markerCol);
        painter.drawRoundedRect(markerRect, 3, 3);

        // painter.setPen(markerTextCol);
        // const QString text = QString::number(marker.height);
        // painter.drawText(markerRect, Qt::AlignCenter, text.length() > 3 ? text.right(3) : text);
        Q_UNUSED(markerTextCol);
    }

    // Bottom status label: live/history mode.
    painter.setPen(textCol);
    const QString modeLabel = m_liveMode ? tr("LIVE") : tr("HISTORY");
    painter.drawText(plot.left() + 4, height() - 4, modeLabel);
}

void HashrateGraphWidget::showMarkerTooltip(const QPoint& local_pos)
{
    for (const BlockMarker& marker : m_blockMarkers) {
        if (marker.hit_rect.isValid() && marker.hit_rect.contains(local_pos)) {
            QString tip = tr("Block %1\n%2")
                              .arg(marker.height)
                              .arg(marker.time_text);

            if (!marker.hash_hex.isEmpty()) {
                tip += tr("\n%1").arg(marker.hash_hex);
            }

            QToolTip::showText(QCursor::pos(), tip, this, marker.hit_rect);
            return;
        }
    }

    QToolTip::hideText();
}

void HashrateGraphWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isPanning = true;
        m_lastPanX = event->pos().x();
        m_liveMode = false;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void HashrateGraphWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPanning) {
        const QRect plot = plotRect();
        if (plot.width() > 0) {
            const int dx = event->pos().x() - m_lastPanX;
            m_lastPanX = event->pos().x();

            const double ms_per_px = double(visibleRangeMs()) / double(plot.width());
            const qint64 delta_ms = static_cast<qint64>(dx * ms_per_px);

            m_viewEndTimeMs -= delta_ms;
            clampViewport();
            update();
        }

        event->accept();
        return;
    }

    showMarkerTooltip(event->pos());
    QWidget::mouseMoveEvent(event);
}

void HashrateGraphWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isPanning) {
        m_isPanning = false;
        unsetCursor();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void HashrateGraphWidget::wheelEvent(QWheelEvent* event)
{
    if (m_samples.isEmpty()) {
        event->accept();
        return;
    }

    const qint64 step_ms = std::max<qint64>(1000, visibleRangeMs() / 10);
    m_liveMode = false;

    if (event->angleDelta().y() > 0) {
        m_viewEndTimeMs -= step_ms;
    } else {
        m_viewEndTimeMs += step_ms;
    }

    clampViewport();
    update();
    event->accept();
}

void HashrateGraphWidget::leaveEvent(QEvent* event)
{
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}
