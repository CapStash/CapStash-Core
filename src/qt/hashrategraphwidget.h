// Portions Copyright (c) 2009-2021 The Bitcoin Core developers
// Portions Copyright (c) 2026 The CapStash Core & BitcoinII Core developers
//
// This file contains modifications and additions specific to CapStash.
// Original upstream portions remain subject to their original license terms.
// CapStash-specific original additions are not licensed for reuse,
// modification, or redistribution absent prior written permission
// from the copyright holder.
// Contact: satooshi@bitcoin-ii.org

#ifndef CAPSTASH_QT_HASHRATEGRAPHWIDGET_H
#define CAPSTASH_QT_HASHRATEGRAPHWIDGET_H

#include <QQueue>
#include <QRect>
#include <QWidget>

#include <chrono>

class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

class HashrateGraphWidget : public QWidget
{

public:
    explicit HashrateGraphWidget(QWidget* parent = nullptr);

    std::chrono::minutes getGraphRange() const;
    void setGraphRange(std::chrono::minutes new_range);

    void clear();
    void setHashrate(double hashps);
    void setBestDiffHit(double best_diff_hit);

    void addBlockMarker(qint64 timestamp_ms, int height, const QString& time_text, const QString& hash_hex);
    void clearBlockMarkers();
    void jumpToLive();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct HashrateSample {
        qint64 timestamp_ms{0};
        float hashps{0.0f};
    };

    struct BlockMarker {
        qint64 timestamp_ms{0};
        int height{0};
        QString time_text;
        QString hash_hex;
        QRect hit_rect;
    };

    static constexpr int SAMPLE_INTERVAL_MS = 1000;
    static constexpr qint64 HISTORY_RETENTION_MS = 24ll * 60ll * 60ll * 1000ll;

    std::chrono::milliseconds sampleInterval() const;
    qint64 currentTimeMs() const;
    qint64 visibleRangeMs() const;
    qint64 visibleEndMs() const;
    qint64 visibleStartMs() const;
    QRect plotRect() const;

    void pruneOldHistory(qint64 now_ms);
    float computeDisplayMax(float peak) const;
    QString formatHashrateLabel(float hashps) const;
    int mapX(qint64 timestamp_ms, const QRect& plot_rect, qint64 view_start_ms, qint64 view_end_ms) const;
    int mapY(float hashps, const QRect& plot_rect, float display_max) const;
    void clearMarkerHitRects();
    void clampViewport();
    void showMarkerTooltip(const QPoint& local_pos);

private:
    std::chrono::minutes m_range{std::chrono::minutes{30}};

    QQueue<HashrateSample> m_samples;
    QQueue<BlockMarker> m_blockMarkers;

    float m_peakSample{0.0f};
    float m_displayMax{0.0f};

    qint64 m_viewEndTimeMs{0};
    bool m_liveMode{true};

    bool m_isPanning{false};
    int m_lastPanX{0};

    double m_bestDiffHit{0.0};
};

#endif // CAPSTASH_QT_HASHRATEGRAPHWIDGET_H
