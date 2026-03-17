// Portions Copyright (c) 2009-2021 The Bitcoin Core developers
// Portions Copyright (c) 2026 The CapStash Core & BitcoinII Core developers
//
// This file contains modifications and additions specific to CapStash.
// Original upstream portions remain subject to their original license terms.
// CapStash-specific original additions are not licensed for reuse,
// modification, or redistribution absent prior written permission
// from the copyright holder.
// Contact: satooshi@bitcoin-ii.org


#ifndef CAPSTASH_QT_MINERPAGE_H
#define CAPSTASH_QT_MINERPAGE_H

#include <QWidget>
#include <QElapsedTimer>
#include <QRegularExpressionValidator>

class ClientModel;
class WalletModel;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class HashrateGraphWidget;

class MinerPage : public QWidget
{
public:
    explicit MinerPage(QWidget* parent = nullptr);

    void setClientModel(ClientModel* model);
    void setWalletModel(WalletModel* model);
    void refreshTheme();
private:
    void onStartMiningClicked();
    void onStopMiningClicked();
    void onTimeRangeChanged(int);
    void updateMiningStats();

    void buildUi();
    void setMiningUiState(bool mining);
    bool callSetGenerate(bool mine, int threads, const QString& address, const QString& coinbaseTag);

    double getLocalHashrateKhs() const;
    int getLocalBlocksMined() const;
    int m_lastLocalBlocksFound{0};
    int m_lastLocalBlockHeight{0};
    QString m_lastLocalBlockHash;

private:
    ClientModel* m_client_model{nullptr};
    WalletModel* m_wallet_model{nullptr};

    QLineEdit* m_addressEdit{nullptr};
    QSpinBox* m_threadsSpin{nullptr};
    QPushButton* m_startButton{nullptr};
    QPushButton* m_stopButton{nullptr};

    QLabel* m_statusLabel{nullptr};
    QLabel* m_hashrateLabel{nullptr};
    QLabel* m_blocksMinedLabel{nullptr};
    QComboBox* m_estimateWindowCombo{nullptr};
    QLabel* m_etaLabel{nullptr};
    QLabel* m_expectedBlocksLabel{nullptr};
    QLabel* m_probabilityLabel{nullptr};
    QComboBox* m_rangeCombo{nullptr};
    int m_lastObservedNetworkHeight{-1};
    qint64 m_lastObservedNetworkBlockTime{0};
    QLabel* m_lotteryBlockLabel = nullptr;
    bool m_lotteryBlockIncomingVisible = false;
    bool m_lotteryBlinkOn{false};
    QElapsedTimer m_miningElapsed;
    bool m_miningElapsedStarted{false};
    HashrateGraphWidget* m_hashrateGraph{nullptr};
    

    QTimer* m_updateTimer{nullptr};

    bool m_isMining{false};
};

#endif // CAPSTASH_QT_MINERPAGE_H
