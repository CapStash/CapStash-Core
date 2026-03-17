// Portions Copyright (c) 2009-2021 The Bitcoin Core developers
// Portions Copyright (c) 2026 The CapStash Core & BitcoinII Core developers
//
// This file contains modifications and additions specific to CapStash.
// Original upstream portions remain subject to their original license terms.
// CapStash-specific original additions are not licensed for reuse,
// modification, or redistribution absent prior written permission
// from the copyright holder.
// Contact: satooshi@bitcoin-ii.org


#ifndef CAPSTASH_QT_EXPLORERPAGE_H
#define CAPSTASH_QT_EXPLORERPAGE_H

#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>

class ClientModel;
class QLineEdit;
class QPushButton;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QTimer;
class QCheckBox;
class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;

class UniValue;
class NetworkStatsGraphWidget;

class ExplorerPage : public QWidget
{
public:
    explicit ExplorerPage(QWidget* parent = nullptr);

    void reapplyTheme();
    void setClientModel(ClientModel* model);

private:
    void buildUi();

    void refreshNetworkStats();
    void refreshMempool();
    void refreshTip();
    void refreshCurrentView();
    void loadInitialBlocks();
    void appendOlderBlocks(int count);
    void prependNewBlocks(int new_tip_height);
    void rebuildDifficultyHistoryGraph();

    void loadBlockRow(int height);
    bool showBlockByHeight(int height);
    bool showBlockByHash(const QString& block_hash);
    bool showTransaction(const QString& txid);
    void showAddress(const QString& address);

    void onSearchClicked();
    void onSearchReturnPressed();

    void addMempoolRow(const QString& txid, double amount, double fee);
    void onMempoolItemClicked(QTreeWidgetItem* item, int column);
    void onBlockItemClicked(QTreeWidgetItem* item, int column);
    void onTxItemClicked(QListWidgetItem* item);
    void onBlockListScrollChanged(int value);
    void onDetailedToggled(bool checked);
    void onDetailLinkClicked(const QString& link);

    double blockTotalMoved(const UniValue& block) const;
    void fillBlockRow(QTreeWidgetItem* item, int height, const UniValue& block) const;

    double estimateObservedNetworkHashrate(int window) const;

    QString rpcGetBlockHash(int height) const;
    int rpcGetBlockCount() const;
    QStringList rpcDecodeScriptAddresses(const QString& script_hex) const;

    QStringList addressesFromScriptPubKey(const UniValue& spk) const;
    double valueAsDouble(const UniValue& value) const;

    QString makeTxListLabel(const UniValue& tx) const;

    QString formatBlockForDisplay(const UniValue& block) const;
    QString formatTxForDisplay(const UniValue& tx) const;
    QString formatAddressForDisplay(const QString& address) const;
    QSet<QString> collectInputAddressesForTx(const UniValue& tx) const;

private:
    ClientModel* m_client_model{nullptr};
    NetworkStatsGraphWidget* m_networkStatsGraph{nullptr};

    QLineEdit* m_searchEdit{nullptr};
    QPushButton* m_searchButton{nullptr};
    QCheckBox* m_detailedToggle{nullptr};
    QLabel* m_statusLabel{nullptr};

    QTreeWidget* m_mempoolList{nullptr};
    QTreeWidget* m_blockList{nullptr};

    QListWidget* m_txList{nullptr};
    QLabel* m_detailView{nullptr};
    QScrollArea* m_detailScroll{nullptr};

    QTimer* m_updateTimer{nullptr};

    int m_loadedOldestHeight{-1};
    int m_knownTipHeight{-1};
    bool m_loadingMore{false};

    bool m_haveSmoothedHashrate{false};
    double m_smoothedHashrate{0.0};

    QString m_currentBlockHash;
    QString m_currentBlockJson;

    QMap<QString, QString> m_cachedTxJson;
    QMap<QString, QString> m_cachedMempoolTxJson;

    QString m_lastViewKind;
    QString m_lastViewValue;
};

#endif // CAPSTASH_QT_EXPLORERPAGE_H
