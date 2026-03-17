// Portions Copyright (c) 2009-2021 The Bitcoin Core developers
// Portions Copyright (c) 2026 The CapStash Core & BitcoinII Core developers
//
// This file contains modifications and additions specific to CapStash.
// Original upstream portions remain subject to their original license terms.
// CapStash-specific original additions are not licensed for reuse,
// modification, or redistribution absent prior written permission
// from the copyright holder.
// Contact: satooshi@bitcoin-ii.org

#ifndef CAPSTASH_QT_NETWORKSTATSGRAPHWIDGET_H
#define CAPSTASH_QT_NETWORKSTATSGRAPHWIDGET_H

#include <QQueue>
#include <QVector>
#include <QWidget>

#include <chrono>

class QPaintEvent;
class QPainterPath;

class NetworkStatsGraphWidget : public QWidget
{
public:
    struct DifficultyPoint {
        qint64 time_ms{0};
        float difficulty{0.0f};
    };

    static constexpr int DESIRED_SAMPLES = 120;

    explicit NetworkStatsGraphWidget(QWidget* parent = nullptr);

    QSize minimumSizeHint() const override;

    std::chrono::minutes getGraphRange() const;
    std::chrono::milliseconds sampleInterval() const;

    void setGraphRange(std::chrono::minutes new_range);
    void setStats(double network_hashps, double difficulty);
    void setDifficultyHistory(const QVector<DifficultyPoint>& points);

    void addBlockMarker(qint64 block_time_sec);
    void clearBlockMarkers();
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float computeDisplayMax(float peak) const;
    QString formatHashrateLabel(float hashps) const;
    QString formatDifficultyLabel(float difficulty) const;

    void paintFilledPath(QPainterPath& path,
                         const QQueue<float>& samples,
                         float display_max,
                         int plot_left, int plot_top,
                         int plot_width, int plot_height);

    void paintLinePath(QPainterPath& path,
                       const QQueue<float>& samples,
                       float display_max,
                       int plot_left, int plot_top,
                       int plot_width, int plot_height);

private:
    std::chrono::minutes m_range{std::chrono::minutes{60}};

    QQueue<float> m_hashrateSamples;
    QQueue<qint64> m_sampleTimesMs;
    QQueue<qint64> m_blockMarkers;
    QVector<DifficultyPoint> m_difficultyHistory;

    float m_peakHashrate{0.0f};
    float m_displayHashrateMax{0.0f};

    float m_lastHashrate{0.0f};
    float m_lastDifficulty{0.0f};
    float m_displayDifficultyMax{2.0f};

    std::chrono::steady_clock::time_point m_lastSampleTime{};
    bool m_hasLastSampleTime{false};
};

#endif // CAPSTASH_QT_NETWORKSTATSGRAPHWIDGET_H
