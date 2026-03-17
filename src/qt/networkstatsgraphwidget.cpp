// Portions Copyright (c) 2009-2021 The Bitcoin Core developers
// Portions Copyright (c) 2026 The CapStash Core & BitcoinII Core developers
//
// This file contains modifications and additions specific to CapStash.
// Original upstream portions remain subject to their original license terms.
// CapStash-specific original additions are not licensed for reuse,
// modification, or redistribution absent prior written permission
// from the copyright holder.
// Contact: satooshi@bitcoin-ii.org

#include <qt/networkstatsgraphwidget.h>
#include <qt/pipboytheme.h>

#include <QColor>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QVector>

#include <algorithm>
#include <cmath>

#define XMARGIN 8
#define YMARGIN 6
#define LABEL_WIDTH 210

namespace {

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

int PlotRight(int plot_left, int plot_width)
{
    return plot_left + plot_width;
}

int XForTimestampMs(qint64 t_ms, qint64 now_ms, qint64 range_ms, int plot_left, int plot_width)
{
    if (plot_width <= 0) return plot_left;
    if (range_ms <= 0) return PlotRight(plot_left, plot_width);

    const qint64 age_ms = now_ms - t_ms;
    const double frac = std::clamp(double(age_ms) / double(range_ms), 0.0, 1.0);
    const int x = PlotRight(plot_left, plot_width) - int(std::llround(double(plot_width) * frac));
    return std::clamp(x, plot_left, PlotRight(plot_left, plot_width));
}

float SmoothedSample(const QQueue<float>& samples, int i)
{
    if (samples.isEmpty()) return 0.0f;
    const int n = samples.size();
    const float a = samples.at(std::clamp(i - 1, 0, n - 1));
    const float b = samples.at(std::clamp(i,     0, n - 1));
    const float c = samples.at(std::clamp(i + 1, 0, n - 1));
    return (a + 2.0f * b + c) * 0.25f;
}

} // namespace

static constexpr int MARKER_SIZE = 10;
static constexpr int MARKER_HALF = MARKER_SIZE / 2;
static constexpr int MAX_BLOCK_MARKERS = 512;

NetworkStatsGraphWidget::NetworkStatsGraphWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(100);
}

QSize NetworkStatsGraphWidget::minimumSizeHint() const
{
    return QSize(200, 175);
}

std::chrono::minutes NetworkStatsGraphWidget::getGraphRange() const
{
    return m_range;
}

std::chrono::milliseconds NetworkStatsGraphWidget::sampleInterval() const
{
    auto interval = std::chrono::duration_cast<std::chrono::milliseconds>(m_range) / DESIRED_SAMPLES;
    if (interval < std::chrono::milliseconds{1000}) {
        interval = std::chrono::milliseconds{1000};
    }
    return interval;
}

void NetworkStatsGraphWidget::setGraphRange(std::chrono::minutes new_range)
{
    if (new_range <= std::chrono::minutes{0}) {
        new_range = std::chrono::minutes{1};
    }

    m_range = new_range;
    clear();
}

void NetworkStatsGraphWidget::addBlockMarker(qint64 block_time_sec)
{
    const qint64 t_ms = block_time_sec * 1000;

    if (!m_blockMarkers.isEmpty() && m_blockMarkers.front() == t_ms) return;

    m_blockMarkers.push_front(t_ms);
    while (m_blockMarkers.size() > 2048) m_blockMarkers.pop_back();

    update();
}

void NetworkStatsGraphWidget::clearBlockMarkers()
{
    m_blockMarkers.clear();
    update();
}

void NetworkStatsGraphWidget::clear()
{
    m_hashrateSamples.clear();
    m_sampleTimesMs.clear();
    m_blockMarkers.clear();
    m_difficultyHistory.clear();

    m_peakHashrate = 0.0f;
    m_displayHashrateMax = 0.0f;

    m_lastHashrate = 0.0f;
    m_lastDifficulty = 0.0f;

    m_hasLastSampleTime = false;
    update();
}

float NetworkStatsGraphWidget::computeDisplayMax(float peak) const
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

QString NetworkStatsGraphWidget::formatHashrateLabel(float hashps) const
{
    if (!std::isfinite(hashps) || hashps < 0.0f) {
        hashps = 0.0f;
    }

    static const char* units[] = {"H/s", "kH/s", "MH/s", "GH/s", "TH/s", "PH/s", "EH/s"};
    int unit_index = 0;
    double value = static_cast<double>(hashps);

    while (value >= 1000.0 && unit_index < 6) {
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

QString NetworkStatsGraphWidget::formatDifficultyLabel(float difficulty) const
{
    if (!std::isfinite(difficulty) || difficulty < 0.0f) {
        difficulty = 0.0f;
    }

    int decimals = 8;
    if (difficulty >= 1000.0f) {
        decimals = 2;
    } else if (difficulty >= 100.0f) {
        decimals = 4;
    } else if (difficulty >= 10.0f) {
        decimals = 6;
    }

    return QString::number(difficulty, 'f', decimals);
}

void NetworkStatsGraphWidget::setStats(double network_hashps, double difficulty)
{
    if (!std::isfinite(network_hashps) || network_hashps < 0.0) {
        network_hashps = 0.0;
    }
    if (!std::isfinite(difficulty) || difficulty < 0.0) {
        difficulty = 0.0;
    }

    m_lastHashrate = static_cast<float>(network_hashps);
    m_lastDifficulty = static_cast<float>(difficulty);

    const auto now = std::chrono::steady_clock::now();
    const auto interval = sampleInterval();
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();

    if (!m_hasLastSampleTime) {
        m_lastSampleTime = now;
        m_hasLastSampleTime = true;
    }

    if (now - m_lastSampleTime < interval) {
        if (!m_hashrateSamples.isEmpty()) {
            m_hashrateSamples[0] = static_cast<float>(network_hashps);
            m_sampleTimesMs[0] = now_ms;
        } else {
            m_hashrateSamples.push_front(static_cast<float>(network_hashps));
            m_sampleTimesMs.push_front(now_ms);
        }
    } else {
        m_lastSampleTime = now;

        m_hashrateSamples.push_front(static_cast<float>(network_hashps));
        m_sampleTimesMs.push_front(now_ms);

        while (m_hashrateSamples.size() > DESIRED_SAMPLES) m_hashrateSamples.pop_back();
        while (m_sampleTimesMs.size() > DESIRED_SAMPLES) m_sampleTimesMs.pop_back();
    }

    float peak_hash = 0.0f;
    for (const float v : m_hashrateSamples) {
        if (v > peak_hash) peak_hash = v;
    }
    m_peakHashrate = peak_hash;
    m_displayHashrateMax = computeDisplayMax(peak_hash);

    // Stable difficulty scale:
    // - true floor stays at difficulty 1
    // - top grows quickly, shrinks slowly
    float peak_diff = std::max(1.0f, m_lastDifficulty);
    for (const DifficultyPoint& p : m_difficultyHistory) {
        if (std::isfinite(p.difficulty) && p.difficulty > peak_diff) {
            peak_diff = p.difficulty;
        }
    }

    const float target_diff_max = std::max(2.0f, computeDisplayMax(peak_diff));

    if (m_displayDifficultyMax <= 1.0f || target_diff_max > m_displayDifficultyMax) {
        m_displayDifficultyMax = target_diff_max;
    } else {
        m_displayDifficultyMax =
            0.92f * m_displayDifficultyMax +
            0.08f * target_diff_max;

        if (m_displayDifficultyMax < 2.0f) {
            m_displayDifficultyMax = 2.0f;
        }
    }

    const qint64 window_ms =
        static_cast<qint64>(std::chrono::duration_cast<std::chrono::milliseconds>(m_range).count());
    const qint64 cutoff_ms = now_ms - window_ms - 30000;

    while (!m_sampleTimesMs.isEmpty() && m_sampleTimesMs.back() < cutoff_ms) {
        m_sampleTimesMs.pop_back();
        if (!m_hashrateSamples.isEmpty()) m_hashrateSamples.pop_back();
    }

    // IMPORTANT:
    // Do NOT destructively prune m_blockMarkers here.
    // Do NOT destructively prune m_difficultyHistory here.
    // Explorer owns historical chain data; paintEvent() decides visibility.

    update();
}

void NetworkStatsGraphWidget::setDifficultyHistory(const QVector<DifficultyPoint>& points)
{
    m_difficultyHistory = points;

    std::sort(m_difficultyHistory.begin(), m_difficultyHistory.end(),
        [](const DifficultyPoint& a, const DifficultyPoint& b) {
            if (a.time_ms != b.time_ms) return a.time_ms < b.time_ms;
            return a.difficulty < b.difficulty;
        });

    QVector<DifficultyPoint> deduped;
    deduped.reserve(m_difficultyHistory.size());
    for (const DifficultyPoint& p : m_difficultyHistory) {
        if (p.time_ms <= 0 || !std::isfinite(p.difficulty) || p.difficulty <= 0.0f) {
            continue;
        }
        if (!deduped.isEmpty() && deduped.back().time_ms == p.time_ms) {
            deduped.back() = p;
        } else {
            deduped.push_back(p);
        }
    }
    m_difficultyHistory.swap(deduped);

    // Leave m_lastDifficulty alone; setStats() owns the live readout value.
    update();
}

void NetworkStatsGraphWidget::paintFilledPath(QPainterPath&, const QQueue<float>&,
                                              float, int, int, int, int)
{
}

void NetworkStatsGraphWidget::paintLinePath(QPainterPath&, const QQueue<float>&,
                                            float, int, int, int, int)
{
}

void NetworkStatsGraphWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    const PipBoyTheme::Palette& pal = CurrentThemePalette();
    const QColor hashTextCol = pal.text;
    const QColor hashFillCol = WithAlpha(pal.text, 80);
    const QColor hashLineCol = pal.text;
    const QColor diffTextCol(255, 80, 80);
    const QColor diffLineCol(255, 60, 60);
    const QColor markerCol(255, 180, 0);

    const int plot_left = XMARGIN + LABEL_WIDTH;
    const int plot_top = YMARGIN;
    const int plot_width = std::max(0, width() - plot_left - XMARGIN);
    const int plot_height = std::max(0, height() - YMARGIN * 2);

    QColor axis_col(90, 90, 90);
    painter.setPen(axis_col);
    painter.drawLine(plot_left, plot_top + plot_height, plot_left + plot_width, plot_top + plot_height);

    const int divisions = 4;
    for (int i = 1; i <= divisions; ++i) {
        const int yy = plot_top + plot_height - (plot_height * i / divisions);
        painter.drawLine(plot_left, yy, plot_left + plot_width, yy);
    }

    painter.setPen(hashTextCol);
    painter.drawText(XMARGIN, plot_top + 16,
        QString("Net Hashrate: %1").arg(formatHashrateLabel(m_lastHashrate)));

    painter.setPen(diffTextCol);
    painter.drawText(XMARGIN, plot_top + 34,
        QString("Difficulty: %1").arg(formatDifficultyLabel(m_lastDifficulty)));

    if (plot_width <= 0 || plot_height <= 0) {
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing);

    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    const qint64 range_ms = std::max<qint64>(
        1, static_cast<qint64>(std::chrono::duration_cast<std::chrono::milliseconds>(m_range).count()));

    auto padRange = [](float& lo, float& hi) {
        if (hi < lo) std::swap(lo, hi);

        const float span = hi - lo;
        if (span <= 0.0f) {
            const float center = hi;
            const float pad = std::max(1.0f, std::abs(center) * 0.10f);
            lo = center - pad;
            hi = center + pad;
        } else {
            const float pad = span * 0.15f;
            lo -= pad;
            hi += pad;
        }

        if (!std::isfinite(lo)) lo = 0.0f;
        if (!std::isfinite(hi) || hi <= lo) hi = lo + 1.0f;
    };

    auto mapYRange = [&](float value, float lo, float hi) -> int {
        if (plot_height <= 0 || !(hi > lo)) {
            return plot_top + plot_height;
        }

        const float clamped = std::clamp(value, lo, hi);
        const double frac = double(clamped - lo) / double(hi - lo);
        const int y = plot_top + plot_height - int(std::llround(double(plot_height) * frac));
        return std::clamp(y, plot_top, plot_top + plot_height);
    };

    float hash_min = 0.0f;
    float hash_max = 0.0f;
    if (!m_hashrateSamples.isEmpty()) {
        hash_min = m_hashrateSamples.front();
        hash_max = m_hashrateSamples.front();
        for (const float v : m_hashrateSamples) {
            hash_min = std::min(hash_min, v);
            hash_max = std::max(hash_max, v);
        }
        if (!std::isfinite(hash_min)) hash_min = 0.0f;
        if (!std::isfinite(hash_max)) hash_max = 0.0f;
        padRange(hash_min, hash_max);
    }

    auto interpolatedHashByTime = [&](qint64 t_ms) -> float {
        if (m_hashrateSamples.isEmpty() || m_sampleTimesMs.isEmpty()) return 0.0f;
        if (m_hashrateSamples.size() == 1 || m_sampleTimesMs.size() == 1) {
            return SmoothedSample(m_hashrateSamples, 0);
        }

        for (int i = 0; i + 1 < m_sampleTimesMs.size(); ++i) {
            const qint64 t0 = m_sampleTimesMs.at(i);
            const qint64 t1 = m_sampleTimesMs.at(i + 1);

            if (t_ms <= t0 && t_ms >= t1) {
                const double denom = double(t0 - t1);
                const double blend = (denom > 0.0) ? double(t_ms - t1) / denom : 0.0;
                const float v0 = SmoothedSample(m_hashrateSamples, i);
                const float v1 = SmoothedSample(m_hashrateSamples, i + 1);
                return float((1.0 - blend) * double(v1) + blend * double(v0));
            }
        }

        if (t_ms >= m_sampleTimesMs.front()) return SmoothedSample(m_hashrateSamples, 0);
        return SmoothedSample(m_hashrateSamples, m_hashrateSamples.size() - 1);
    };

    if (!m_hashrateSamples.isEmpty() &&
        !m_sampleTimesMs.isEmpty() &&
        m_hashrateSamples.size() == m_sampleTimesMs.size()) {
        QVector<QPointF> pts;
        pts.reserve(m_hashrateSamples.size());

        for (int i = 0; i < m_hashrateSamples.size(); ++i) {
            const int x = XForTimestampMs(m_sampleTimesMs.at(i), now_ms, range_ms, plot_left, plot_width);
            const int y = mapYRange(SmoothedSample(m_hashrateSamples, i), hash_min, hash_max);
            pts.push_back(QPointF(x, y));
        }

        if (!pts.isEmpty()) {
            QPainterPath line_path;
            line_path.moveTo(pts[0]);

            if (pts.size() == 1) {
                line_path.lineTo(pts[0]);
            } else {
                for (int i = 1; i < pts.size(); ++i) {
                    const QPointF mid = (pts[i - 1] + pts[i]) * 0.5;
                    line_path.quadTo(pts[i - 1], mid);
                }
                line_path.lineTo(pts.back());
            }

            QPainterPath fill_path = line_path;
            fill_path.lineTo(pts.back().x(), plot_top + plot_height);
            fill_path.lineTo(pts.front().x(), plot_top + plot_height);
            fill_path.closeSubpath();

            painter.fillPath(fill_path, hashFillCol);
            painter.setPen(QPen(hashLineCol, 1.5));
            painter.drawPath(line_path);
        }
    }

    if (!m_difficultyHistory.isEmpty()) {
        const float diff_lo = 1.0f;
        const float diff_hi = std::max(2.0f, m_displayDifficultyMax);

        QPainterPath diff_path;
        bool started = false;
        int prev_y = 0;
        int last_hist_x = plot_left;

        for (const DifficultyPoint& p : m_difficultyHistory) {
            if (p.time_ms < now_ms - range_ms || p.time_ms > now_ms) {
                continue;
            }

            const int x = XForTimestampMs(p.time_ms, now_ms, range_ms, plot_left, plot_width);
            const int y = mapYRange(p.difficulty, diff_lo, diff_hi);

            if (!started) {
                diff_path.moveTo(x, y);
                prev_y = y;
                last_hist_x = x;
                started = true;
            } else {
                diff_path.lineTo(x, prev_y);
                diff_path.lineTo(x, y);
                prev_y = y;
                last_hist_x = x;
            }
        }

        if (started) {
            int live_y = prev_y;
            if (std::isfinite(m_lastDifficulty) && m_lastDifficulty > 0.0f) {
                live_y = mapYRange(m_lastDifficulty, diff_lo, diff_hi);
            }

            if (live_y != prev_y) {
                diff_path.lineTo(last_hist_x, live_y);
            }

            diff_path.lineTo(plot_left + plot_width, live_y);
        }

        painter.setPen(QPen(diffLineCol, 1.5));
        painter.drawPath(diff_path);
    }

    if (!m_blockMarkers.isEmpty()) {
        QPen markerPen(QColor(0, 0, 0));
        markerPen.setWidth(2);
        painter.setBrush(markerCol);
        painter.setPen(markerPen);

        for (qint64 t_ms : m_blockMarkers) {
            if (t_ms < now_ms - range_ms || t_ms > now_ms) {
                continue;
            }

            const int x = XForTimestampMs(t_ms, now_ms, range_ms, plot_left, plot_width);

            int y = plot_top + plot_height - MARKER_HALF;
            if (!m_hashrateSamples.isEmpty() && !m_sampleTimesMs.isEmpty()) {
                y = mapYRange(interpolatedHashByTime(t_ms), hash_min, hash_max);
            }

            const int cx = std::clamp(x, plot_left + MARKER_HALF, plot_left + plot_width - MARKER_HALF);
            const int cy = std::clamp(y, plot_top + MARKER_HALF, plot_top + plot_height - MARKER_HALF);

            painter.drawRect(cx - MARKER_HALF, cy - MARKER_HALF, MARKER_SIZE, MARKER_SIZE);
        }
    }
}
