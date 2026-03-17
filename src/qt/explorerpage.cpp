// Portions Copyright (c) 2009-2021 The Bitcoin Core developers
// Portions Copyright (c) 2026 The CapStash Core & BitcoinII Core developers
//
// This file contains modifications and additions specific to CapStash.
// Original upstream portions remain subject to their original license terms.
// CapStash-specific original additions are not licensed for reuse,
// modification, or redistribution absent prior written permission
// from the copyright holder.
// Contact: satooshi@bitcoin-ii.org


#include <qt/explorerpage.h>

#include <qt/clientmodel.h>

#include <cmath>
#include <interfaces/node.h>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit> // (ok if unused)
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QPushButton>

#include <algorithm>
#include <qt/networkstatsgraphwidget.h>
#include <univalue.h>

namespace {

QString ToQString(const UniValue& value)
{
    if (value.isNull()) return QString();
    if (value.isStr()) return QString::fromStdString(value.get_str());
    return QString::fromStdString(value.write());
}

bool LooksLikeHex64(const QString& s)
{
    if (s.size() != 64) return false;
    for (QChar c : s) {
        const ushort u = c.unicode();
        const bool is_num = (u >= '0' && u <= '9');
        const bool is_lo = (u >= 'a' && u <= 'f');
        const bool is_hi = (u >= 'A' && u <= 'F');
        if (!(is_num || is_lo || is_hi)) return false;
    }
    return true;
}

bool ParseJson(const QString& text, UniValue& out)
{
    if (text.isEmpty()) return false;
    return out.read(text.toStdString());
}

QString FirstAddressFromScriptPubKey(const UniValue& spk)
{
    if (!spk.isObject()) return QString();

    const UniValue& address = spk.find_value("address");
    if (address.isStr()) return QString::fromStdString(address.get_str());

    const UniValue& addresses = spk.find_value("addresses");
    if (addresses.isArray() && addresses.size() > 0 && addresses[0].isStr()) {
        return QString::fromStdString(addresses[0].get_str());
    }

    return QString();
}

QString FormatAmountFromDouble(double amount)
{
    return QString::number(amount, 'f', 8) + QStringLiteral(" CAP");
}

static void PushRecent(QStringList& lines, const QString& line, int max_keep = -1)
{
    lines.push_back(line);

    if (max_keep <= 0) return;

    while (lines.size() > max_keep) {
        lines.removeFirst();
    }
}

QString FormatUnixTime(const UniValue& value)
{
    if (value.isNull()) return QString();

    bool ok = false;
    qint64 ts = ToQString(value).toLongLong(&ok);
    if (!ok || ts <= 0) return QString();

    const QDateTime dt = QDateTime::fromSecsSinceEpoch(ts, Qt::LocalTime);
    return dt.toString("yyyy-MM-dd hh:mm:ss");
}

QString HumanTimeOrRaw(const UniValue& value)
{
    const QString friendly = FormatUnixTime(value);
    if (!friendly.isEmpty()) return friendly;
    return ToQString(value);
}

static QString H(const QString& s) { return s.toHtmlEscaped(); }

// Keep clickable detail links red and stable.
static QString LinkTx(const QString& txid, const QString& label = QString())
{
    const QString text = label.isEmpty() ? txid : label;
    return QString("<a style=\"color:#ff3b3b;\" href=\"cap:tx:%1\">%2</a>").arg(H(txid), H(text));
}

static QString LinkBlockHash(const QString& bh, const QString& label = QString())
{
    const QString text = label.isEmpty() ? bh : label;
    return QString("<a style=\"color:#ff3b3b;\" href=\"cap:block:%1\">%2</a>").arg(H(bh), H(text));
}

static QString LinkBlockHeight(int height, const QString& label = QString())
{
    const QString text = label.isEmpty() ? QString::number(height) : label;
    return QString("<a style=\"color:#ff3b3b;\" href=\"cap:blockheight:%1\">%2</a>").arg(height).arg(H(text));
}

static QString LinkAddr(const QString& a)
{
    return QString("<a style=\"color:#ff3b3b;\" href=\"cap:addr:%1\">%2</a>").arg(H(a), H(a));
}

static QString SmallMono(const QString& s)
{
    return QString("<span style=\"font-family:monospace;\">%1</span>").arg(H(s));
}

static QString ShortHash(const QString& h, int keep = 16)
{
    if (h.size() <= keep) return h;
    return h.left(keep) + QStringLiteral("…");
}

static QString MempoolHeaderStyle()
{
    return QStringLiteral(
        "QLabel {"
        " font-weight:bold;"
        " color:#ff4444;"
        " background-color:#220000;"
        " border:1px solid #ff4444;"
        " border-radius:4px;"
        " padding:6px 8px 6px 8px;"
        "}"
    );
}

int HexNibble(QChar c)
{
    const ushort u = c.unicode();
    if (u >= '0' && u <= '9') return int(u - '0');
    if (u >= 'a' && u <= 'f') return 10 + int(u - 'a');
    if (u >= 'A' && u <= 'F') return 10 + int(u - 'A');
    return -1;
}

QByteArray ParseHexBytes(const QString& hex)
{
    QByteArray out;
    if (hex.size() < 2 || (hex.size() & 1)) return out;

    out.reserve(hex.size() / 2);
    for (int i = 0; i < hex.size(); i += 2) {
        const int hi = HexNibble(hex[i]);
        const int lo = HexNibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return QByteArray();
        out.append(char((hi << 4) | lo));
    }
    return out;
}

bool IsCoinbaseTagChar(unsigned char c)
{
    return
        (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') ||
        c == ' ' ||
        c == '.' ||
        c == ':' ||
        c == '_' ||
        c == '-';
}

QString SanitizeCoinbaseTagCandidate(const QString& s)
{
    QString out;
    out.reserve(std::min(32, s.size()));

    for (QChar qc : s) {
        const ushort c = qc.unicode();
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == ' ' || c == '.' || c == ':' || c == '_' || c == '-') {
            out.append(qc);
            if (out.size() >= 32) break;
        }
    }

    return out.trimmed();
}

QString ExtractCoinbaseTagFromHexScript(const QString& hex_script)
{
    const QByteArray bytes = ParseHexBytes(hex_script);
    if (bytes.isEmpty()) return QString();

    // Prefer a trailing printable run, since the custom tag is expected to be
    // appended after the scriptnum/extranonce pushes.
    int end = bytes.size() - 1;
    while (end >= 0 && !IsCoinbaseTagChar(static_cast<unsigned char>(bytes[end]))) {
        --end;
    }
    if (end < 0) return QString();

    int start = end;
    while (start >= 0 && IsCoinbaseTagChar(static_cast<unsigned char>(bytes[start]))) {
        --start;
    }
    ++start;

    QString trailing = QString::fromLatin1(bytes.constData() + start, end - start + 1);
    trailing = SanitizeCoinbaseTagCandidate(trailing);
    if (trailing.size() >= 3) return trailing;

    // Fallback: search for the longest printable run anywhere in the script.
    QString best;
    int i = 0;
    while (i < bytes.size()) {
        while (i < bytes.size() && !IsCoinbaseTagChar(static_cast<unsigned char>(bytes[i]))) {
            ++i;
        }
        const int run_start = i;
        while (i < bytes.size() && IsCoinbaseTagChar(static_cast<unsigned char>(bytes[i]))) {
            ++i;
        }
        const int run_len = i - run_start;
        if (run_len > 0) {
            QString candidate = QString::fromLatin1(bytes.constData() + run_start, run_len);
            candidate = SanitizeCoinbaseTagCandidate(candidate);
            if (candidate.size() >= 3 && candidate.size() > best.size()) {
                best = candidate;
            }
        }
    }

    return best;
}

QString TruncateAddress(const QString& addr)
{
    if (addr.size() <= 18) return addr;
    return addr.left(10) + QStringLiteral("…") + addr.right(6);
}

QString BlockMinerLabel(const UniValue& block)
{
    if (!block.isObject()) return QString();

    const UniValue& txs = block.find_value("tx");
    if (!txs.isArray() || txs.size() == 0 || !txs[0].isObject()) return QString();

    const UniValue& coinbase_tx = txs[0];

    // 1) Try to extract custom miner tag from coinbase input script.
    const UniValue& vin = coinbase_tx.find_value("vin");
    if (vin.isArray() && vin.size() > 0 && vin[0].isObject()) {
        const UniValue& cb_hex = vin[0].find_value("coinbase");
        if (cb_hex.isStr()) {
            const QString tag = ExtractCoinbaseTagFromHexScript(QString::fromStdString(cb_hex.get_str()));
            if (!tag.isEmpty()) return tag;
        }

        const UniValue& script_sig = vin[0].find_value("scriptSig");
        if (script_sig.isObject()) {
            const UniValue& hex = script_sig.find_value("hex");
            if (hex.isStr()) {
                const QString tag = ExtractCoinbaseTagFromHexScript(QString::fromStdString(hex.get_str()));
                if (!tag.isEmpty()) return tag;
            }
        }
    }

    // 2) Fallback to payout address from coinbase outputs.
    const UniValue& vout = coinbase_tx.find_value("vout");
    if (vout.isArray()) {
        for (unsigned int i = 0; i < vout.size(); ++i) {
            const UniValue& outv = vout[i];
            const UniValue& spk = outv.find_value("scriptPubKey");
            const QString addr = FirstAddressFromScriptPubKey(spk);
            if (!addr.isEmpty()) {
                return TruncateAddress(addr);
            }

            const UniValue& addrs = spk.find_value("addresses");
            if (addrs.isArray() && addrs.size() > 0 && addrs[0].isStr()) {
                return TruncateAddress(QString::fromStdString(addrs[0].get_str()));
            }
        }
    }

    return QObject::tr("Unknown");
}

double DifficultyFromBitsHex(const QString& bits_hex)
{
    QString s = bits_hex.trimmed().toLower();
    if (s.startsWith(QStringLiteral("0x"))) s.remove(0, 2);
    if (s.size() != 8) return 0.0;

    bool ok = false;
    const quint32 bits = s.toUInt(&ok, 16);
    if (!ok || bits == 0) return 0.0;

    int nShift = int((bits >> 24) & 0xff);
    const quint32 nMantissa = bits & 0x00ffffffU;
    if (nMantissa == 0) return 0.0;

    // CapStash diff-1 baseline compact:
    // 0x1d01fffe
    double dDiff = double(0x01fffeU) / double(nMantissa);

    while (nShift < 0x1d) {
        dDiff *= 256.0;
        ++nShift;
    }
    while (nShift > 0x1d) {
        dDiff /= 256.0;
        --nShift;
    }

    return dDiff;
}

} // namespace

double ExplorerPage::estimateObservedNetworkHashrate(int window) const
{
    if (!m_client_model || window < 2) return 0.0;

    const int tip = rpcGetBlockCount();
    if (tip <= 0) return 0.0;

    const int start = std::max(0, tip - window);

    struct Sample {
        qint64 time{0};
        double difficulty{0.0};
    };

    std::vector<Sample> samples;
    samples.reserve(window + 1);

    for (int h = start; h <= tip; ++h) {
        try {
            const QString hash = rpcGetBlockHash(h);
            if (hash.isEmpty()) continue;

            UniValue params(UniValue::VARR);
            params.push_back(hash.toStdString());
            params.push_back(1);

            UniValue block = m_client_model->node().executeRpc("getblock", params, "/");
            if (!block.isObject()) continue;

            bool ok = false;
            qint64 t = ToQString(block.find_value("time")).toLongLong(&ok);
            double diff = valueAsDouble(block.find_value("difficulty"));

            if (!ok || t <= 0 || !std::isfinite(diff)) continue;

            samples.push_back({t, diff});
        }
        catch (...) {}
    }

    if (samples.size() < 2) return 0.0;

    constexpr double POW2_32 = 4294967296.0;
    constexpr qint64 MIN_SPACING = 15;
    constexpr qint64 MAX_SPACING = 600;

    double work_sum = 0.0;
    qint64 time_sum = 0;

    for (size_t i = 1; i < samples.size(); ++i) {
        qint64 spacing = samples[i].time - samples[i - 1].time;
        if (spacing <= 0) continue;

        spacing = std::clamp(spacing, MIN_SPACING, MAX_SPACING);

        work_sum += samples[i].difficulty * POW2_32;
        time_sum += spacing;
    }

    if (time_sum <= 0) return 0.0;

    return work_sum / double(time_sum);
}

ExplorerPage::ExplorerPage(QWidget* parent) : QWidget(parent)
{
    buildUi();

    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(3000);
    connect(m_updateTimer, &QTimer::timeout, this, &ExplorerPage::refreshTip);
    m_updateTimer->start();
}

void ExplorerPage::setClientModel(ClientModel* model)
{
    m_client_model = model;

    refreshNetworkStats();
    refreshMempool();
    loadInitialBlocks();
}

void ExplorerPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    m_networkStatsGraph = new NetworkStatsGraphWidget(this);
    m_networkStatsGraph->setObjectName("explorerNetworkStatsGraph");
    m_networkStatsGraph->setMinimumHeight(100);
    m_networkStatsGraph->setMaximumHeight(175);
    root->addWidget(m_networkStatsGraph);

    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(6);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName("explorerSearchEdit");
    m_searchEdit->setPlaceholderText(tr("Search block #, block hash, transaction ID, or address"));

    m_searchButton = new QPushButton(tr("Search"), this);
    m_searchButton->setObjectName("explorerSearchButton");

    m_detailedToggle = new QCheckBox(tr("Show Details"), this);
    m_detailedToggle->setObjectName("explorerDetailedToggle");

    m_statusLabel = new QLabel(tr("Explorer idle"), this);
    m_statusLabel->setObjectName("explorerStatusLabel");

    topRow->addWidget(m_searchEdit, 1);
    topRow->addWidget(m_searchButton);
    topRow->addWidget(m_detailedToggle);
    topRow->addWidget(m_statusLabel);

    root->addLayout(topRow);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName("explorerSplitter");

    constexpr int COL0_WIDTH = 140;
    constexpr int COL1_WIDTH = 90;
    constexpr int COL2_WIDTH = 170;
    constexpr int COL3_WIDTH = 180;

    auto* blockPane = new QFrame(splitter);
    blockPane->setObjectName("explorerBlockPane");
    blockPane->setFrameShape(QFrame::StyledPanel);

    auto* blockLayout = new QVBoxLayout(blockPane);
    blockLayout->setContentsMargins(6, 6, 6, 6);
    blockLayout->setSpacing(6);

    auto* leftStack = new QSplitter(Qt::Vertical, blockPane);
    leftStack->setObjectName("explorerLeftStack");
    leftStack->setChildrenCollapsible(false);
    leftStack->setHandleWidth(8);

    auto* mempoolPane = new QFrame(leftStack);
    mempoolPane->setObjectName("explorerMempoolPane");
    mempoolPane->setFrameShape(QFrame::StyledPanel);
    auto* mempoolLayout = new QVBoxLayout(mempoolPane);
    mempoolLayout->setContentsMargins(0, 0, 0, 0);
    mempoolLayout->setSpacing(6);

    auto* mempoolTitle = new QLabel(tr("--- MEMPOOL TRANSACTIONS ---"), mempoolPane);
    mempoolTitle->setObjectName("explorerMempoolTitle");
    mempoolTitle->setStyleSheet(MempoolHeaderStyle());
    mempoolLayout->addWidget(mempoolTitle);

    m_mempoolList = new QTreeWidget(mempoolPane);
    m_mempoolList->setObjectName("explorerMempoolList");
    m_mempoolList->setColumnCount(3);
    m_mempoolList->setHeaderLabels({tr("TX ID"), tr("Amount"), tr("Fee")});
    m_mempoolList->setRootIsDecorated(false);
    m_mempoolList->setUniformRowHeights(true);
    m_mempoolList->setAlternatingRowColors(false);
    m_mempoolList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mempoolList->setAllColumnsShowFocus(true);
    m_mempoolList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mempoolList->setSortingEnabled(false);
    m_mempoolList->setMinimumHeight(120);

    QHeaderView* memHeader = m_mempoolList->header();
    memHeader->setStretchLastSection(false);
    memHeader->setSectionResizeMode(0, QHeaderView::Interactive);
    memHeader->setSectionResizeMode(1, QHeaderView::Interactive);
    memHeader->setSectionResizeMode(2, QHeaderView::Interactive);
    memHeader->setDefaultAlignment(Qt::AlignCenter);

    m_mempoolList->setColumnWidth(0, COL0_WIDTH);
    m_mempoolList->setColumnWidth(1, COL1_WIDTH);
    m_mempoolList->setColumnWidth(2, COL2_WIDTH);

    mempoolLayout->addWidget(m_mempoolList, 1);

    auto* blocksPane = new QFrame(leftStack);
    blocksPane->setObjectName("explorerBlocksPane");
    blocksPane->setFrameShape(QFrame::StyledPanel);
    auto* blocksLayout = new QVBoxLayout(blocksPane);
    blocksLayout->setContentsMargins(0, 0, 0, 0);
    blocksLayout->setSpacing(6);

    auto* blockTitle = new QLabel(tr("Blocks"), blocksPane);
    blockTitle->setObjectName("explorerBlockTitle");
    blocksLayout->addWidget(blockTitle);

    m_blockList = new QTreeWidget(blocksPane);
    m_blockList->setObjectName("explorerBlockList");
    m_blockList->setColumnCount(4);
    m_blockList->setHeaderLabels({tr("Block Height"), tr("# of TXs"), tr("Total CAP Moved"), tr("Mined By")});
    m_blockList->setRootIsDecorated(false);
    m_blockList->setUniformRowHeights(true);
    m_blockList->setAlternatingRowColors(false);
    m_blockList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_blockList->setAllColumnsShowFocus(true);
    m_blockList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_blockList->setSortingEnabled(false);
    m_blockList->setMinimumHeight(160);

    QHeaderView* header = m_blockList->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(0, QHeaderView::Interactive);
    header->setSectionResizeMode(1, QHeaderView::Interactive);
    header->setSectionResizeMode(2, QHeaderView::Interactive);
    header->setSectionResizeMode(3, QHeaderView::Interactive);
    header->setDefaultAlignment(Qt::AlignCenter);

    m_blockList->setColumnWidth(0, COL0_WIDTH);
    m_blockList->setColumnWidth(1, COL1_WIDTH);
    m_blockList->setColumnWidth(2, COL2_WIDTH);
    m_blockList->setColumnWidth(3, COL3_WIDTH);

    blocksLayout->addWidget(m_blockList, 1);

    leftStack->addWidget(mempoolPane);
    leftStack->addWidget(blocksPane);
    leftStack->setStretchFactor(0, 0);
    leftStack->setStretchFactor(1, 1);
    leftStack->setSizes({200, 420});

    blockLayout->addWidget(leftStack, 1);

    auto* rightPane = new QWidget(splitter);
    rightPane->setObjectName("explorerRightPane");
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    auto* txPane = new QFrame(rightPane);
    txPane->setObjectName("explorerTxPane");
    txPane->setFrameShape(QFrame::StyledPanel);

    auto* txLayout = new QVBoxLayout(txPane);
    txLayout->setContentsMargins(6, 6, 6, 6);
    txLayout->setSpacing(4);

    auto* txTitle = new QLabel(tr("Transactions"), txPane);
    txTitle->setObjectName("explorerTxTitle");
    txLayout->addWidget(txTitle);

    m_txList = new QListWidget(txPane);
    m_txList->setObjectName("explorerTxList");
    txLayout->addWidget(m_txList, 1);

    rightLayout->addWidget(txPane, 1);

    auto* detailPane = new QFrame(rightPane);
    detailPane->setObjectName("explorerDetailPane");
    detailPane->setFrameShape(QFrame::StyledPanel);

    auto* detailLayout = new QVBoxLayout(detailPane);
    detailLayout->setContentsMargins(6, 6, 6, 6);
    detailLayout->setSpacing(4);

    auto* detailTitle = new QLabel(tr("Details"), detailPane);
    detailTitle->setObjectName("explorerDetailTitle");
    detailLayout->addWidget(detailTitle);

    m_detailView = new QLabel(detailPane);
    m_detailView->setObjectName("explorerDetailView");
    m_detailView->setTextFormat(Qt::RichText);
    m_detailView->setTextInteractionFlags(
        Qt::LinksAccessibleByMouse |
        Qt::LinksAccessibleByKeyboard |
        Qt::TextSelectableByMouse |
        Qt::TextSelectableByKeyboard
    );
    m_detailView->setOpenExternalLinks(false);
    m_detailView->setWordWrap(true);

    connect(m_detailView, &QLabel::linkActivated, this, &ExplorerPage::onDetailLinkClicked);

    m_detailScroll = new QScrollArea(detailPane);
    m_detailScroll->setObjectName("explorerDetailScroll");
    m_detailScroll->setWidgetResizable(true);
    m_detailScroll->setFrameShape(QFrame::NoFrame);
    m_detailScroll->setWidget(m_detailView);
    detailLayout->addWidget(m_detailScroll, 2);

    rightLayout->addWidget(detailPane, 2);

    splitter->addWidget(blockPane);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    root->addWidget(splitter, 1);

    connect(m_searchButton, &QPushButton::clicked, this, &ExplorerPage::onSearchClicked);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &ExplorerPage::onSearchReturnPressed);
    connect(m_mempoolList, &QTreeWidget::itemClicked, this, &ExplorerPage::onMempoolItemClicked);
    connect(m_blockList, &QTreeWidget::itemClicked, this, &ExplorerPage::onBlockItemClicked);
    connect(m_txList, &QListWidget::itemClicked, this, &ExplorerPage::onTxItemClicked);
    connect(m_blockList->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &ExplorerPage::onBlockListScrollChanged);
    connect(m_detailedToggle, &QCheckBox::toggled, this, &ExplorerPage::onDetailedToggled);
}

void ExplorerPage::reapplyTheme()
{
    if (auto* w = findChild<QLabel*>("explorerMempoolTitle")) {
        w->setStyleSheet(MempoolHeaderStyle());
    }
    if (m_detailView) {
        refreshCurrentView();
    }
    update();
}

void ExplorerPage::addMempoolRow(const QString& txid, double amount, double fee)
{
    if (!m_mempoolList) return;

    auto* item = new QTreeWidgetItem(m_mempoolList);
    item->setData(0, Qt::UserRole, txid);

    const QString short_txid = txid.isEmpty() ? tr("tx") : (txid.left(16) + QStringLiteral("…"));

    item->setText(0, short_txid);
    item->setText(1, FormatAmountFromDouble(amount));
    item->setText(2, QString::number(fee, 'f', 8));

    item->setToolTip(0, txid);
    item->setToolTip(1, txid);
    item->setToolTip(2, txid);

    item->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);
    item->setTextAlignment(1, Qt::AlignHCenter | Qt::AlignVCenter);
    item->setTextAlignment(2, Qt::AlignHCenter | Qt::AlignVCenter);
}

void ExplorerPage::refreshMempool()
{
    if (!m_client_model || !m_mempoolList) return;

    m_mempoolList->clear();
    m_cachedMempoolTxJson.clear();

    try {
        UniValue params(UniValue::VARR);
        UniValue result = m_client_model->node().executeRpc("getrawmempool", params, "/");

        if (!result.isArray()) {
            auto* item = new QTreeWidgetItem(m_mempoolList);
            item->setText(0, tr("Mempool unavailable"));
            item->setFirstColumnSpanned(true);
            item->setFlags(Qt::NoItemFlags);
            return;
        }

        QStringList txids;
        for (unsigned int i = 0; i < result.size(); ++i) {
            if (result[i].isStr()) {
                txids.push_back(QString::fromStdString(result[i].get_str()));
            }
        }

        std::reverse(txids.begin(), txids.end());

        const int max_items = std::min<int>(100, txids.size());

        for (int i = 0; i < max_items; ++i) {
            const QString& txid = txids[i];

            try {
                UniValue tx_params(UniValue::VARR);
                tx_params.push_back(txid.toStdString());
                tx_params.push_back(true);

                UniValue tx = m_client_model->node().executeRpc("getrawtransaction", tx_params, "/");
                if (!tx.isObject()) continue;

                m_cachedMempoolTxJson.insert(txid, QString::fromStdString(tx.write()));

                double total_out = 0.0;
                const UniValue& vout = tx.find_value("vout");
                if (vout.isArray()) {
                    for (unsigned int j = 0; j < vout.size(); ++j) {
                        total_out += valueAsDouble(vout[j].find_value("value"));
                    }
                }

                double fee = 0.0;
                const UniValue& fee_val = tx.find_value("fee");
                if (!fee_val.isNull()) {
                    fee = std::abs(valueAsDouble(fee_val));
                }

                addMempoolRow(txid, total_out, fee);
            } catch (...) {
                addMempoolRow(txid, 0.0, 0.0);
            }
        }

        if (m_mempoolList->topLevelItemCount() == 0) {
            auto* item = new QTreeWidgetItem(m_mempoolList);
            item->setText(0, tr("No transactions in mempool"));
            item->setFirstColumnSpanned(true);
            item->setFlags(Qt::NoItemFlags);
        }
    } catch (...) {
        auto* item = new QTreeWidgetItem(m_mempoolList);
        item->setText(0, tr("Mempool unavailable"));
        item->setFirstColumnSpanned(true);
        item->setFlags(Qt::NoItemFlags);
    }
}

void ExplorerPage::onMempoolItemClicked(QTreeWidgetItem* item, int)
{
    if (!item) return;

    const QString txid = item->data(0, Qt::UserRole).toString();
    if (txid.isEmpty()) return;

    showTransaction(txid);
}

void ExplorerPage::refreshNetworkStats()
{
    if (!m_client_model || !m_networkStatsGraph) return;

    double rpc_hashps = 0.0;
    double observed = 0.0;
    double display_difficulty = 0.0;

    try {
        UniValue params(UniValue::VARR);
        UniValue result = m_client_model->node().executeRpc("getmininginfo", params, "/");
        if (result.isObject()) {
            rpc_hashps = valueAsDouble(result.find_value("networkhashps"));
            display_difficulty = valueAsDouble(result.find_value("difficulty")); // fallback only
        }
    } catch (...) {
    }

    try {
        observed = estimateObservedNetworkHashrate(72);
    } catch (...) {
        observed = 0.0;
    }

    if (!std::isfinite(observed) || observed <= 0.0) {
        observed = rpc_hashps;
    }

    try {
        UniValue gbt_params(UniValue::VARR);
        UniValue req(UniValue::VOBJ);

        UniValue rules(UniValue::VARR);
        rules.push_back("segwit");
        req.pushKV("rules", rules);

        gbt_params.push_back(req);

        UniValue gbt = m_client_model->node().executeRpc("getblocktemplate", gbt_params, "/");
        if (gbt.isObject()) {
            const QString bits_hex = ToQString(gbt.find_value("bits"));
            const double gbt_diff = DifficultyFromBitsHex(bits_hex);
            if (std::isfinite(gbt_diff) && gbt_diff > 0.0) {
                display_difficulty = gbt_diff;
            }
        }
    } catch (...) {
        // keep fallback from getmininginfo
    }

    if (!std::isfinite(observed) || observed < 0.0) {
        observed = 0.0;
    }
    if (!std::isfinite(display_difficulty) || display_difficulty < 0.0) {
        display_difficulty = 0.0;
    }

    constexpr double ALPHA = 0.12;

    if (!m_haveSmoothedHashrate) {
        m_smoothedHashrate = observed;
        m_haveSmoothedHashrate = true;
    } else {
        m_smoothedHashrate =
            ALPHA * observed +
            (1.0 - ALPHA) * m_smoothedHashrate;
    }

    m_networkStatsGraph->setStats(m_smoothedHashrate, display_difficulty);
}

int ExplorerPage::rpcGetBlockCount() const
{
    if (!m_client_model) return -1;

    UniValue params(UniValue::VARR);
    UniValue result = m_client_model->node().executeRpc("getblockcount", params, "/");
    return result.getInt<int>();
}

QString ExplorerPage::rpcGetBlockHash(int height) const
{
    if (!m_client_model) return QString();

    UniValue params(UniValue::VARR);
    params.push_back(height);
    UniValue result = m_client_model->node().executeRpc("getblockhash", params, "/");
    return QString::fromStdString(result.get_str());
}

QStringList ExplorerPage::rpcDecodeScriptAddresses(const QString& script_hex) const
{
    QStringList out;
    if (!m_client_model || script_hex.isEmpty()) return out;

    try {
        UniValue params(UniValue::VARR);
        params.push_back(script_hex.toStdString());

        UniValue result = m_client_model->node().executeRpc("decodescript", params, "/");
        if (!result.isObject()) return out;

        const UniValue& address = result.find_value("address");
        if (address.isStr()) out.push_back(QString::fromStdString(address.get_str()));

        const UniValue& addresses = result.find_value("addresses");
        if (addresses.isArray()) {
            for (unsigned int i = 0; i < addresses.size(); ++i) {
                if (!addresses[i].isStr()) continue;
                const QString decoded = QString::fromStdString(addresses[i].get_str());
                if (!out.contains(decoded)) out.push_back(decoded);
            }
        }

        const UniValue& segwit = result.find_value("segwit");
        if (segwit.isObject()) {
            const UniValue& segwit_addr = segwit.find_value("address");
            if (segwit_addr.isStr()) {
                const QString decoded = QString::fromStdString(segwit_addr.get_str());
                if (!out.contains(decoded)) out.push_back(decoded);
            }
        }
    } catch (...) {
    }

    return out;
}

QStringList ExplorerPage::addressesFromScriptPubKey(const UniValue& spk) const
{
    QStringList out;
    if (!spk.isObject()) return out;

    const QString direct = FirstAddressFromScriptPubKey(spk);
    if (!direct.isEmpty()) out.push_back(direct);

    const QString hex = ToQString(spk.find_value("hex"));
    const QStringList decoded = rpcDecodeScriptAddresses(hex);
    for (const QString& a : decoded) {
        if (!out.contains(a)) out.push_back(a);
    }

    return out;
}

double ExplorerPage::valueAsDouble(const UniValue& value) const
{
    if (value.isNum()) return value.get_real();
    bool ok = false;
    const double d = ToQString(value).toDouble(&ok);
    return ok ? d : 0.0;
}

double ExplorerPage::blockTotalMoved(const UniValue& block) const
{
    if (!block.isObject()) return 0.0;

    double total = 0.0;
    const UniValue& txs = block.find_value("tx");
    if (!txs.isArray()) return 0.0;

    for (unsigned int i = 0; i < txs.size(); ++i) {
        const UniValue& tx = txs[i];
        if (!tx.isObject()) continue;

        const UniValue& vout = tx.find_value("vout");
        if (!vout.isArray()) continue;

        for (unsigned int j = 0; j < vout.size(); ++j) {
            total += valueAsDouble(vout[j].find_value("value"));
        }
    }

    return total;
}

void ExplorerPage::fillBlockRow(QTreeWidgetItem* item, int height, const UniValue& block) const
{
    if (!item) return;

    int tx_count = 0;
    double moved = 0.0;
    QString mined_by = tr("Unknown");

    if (block.isObject()) {
        const UniValue& txs = block.find_value("tx");
        tx_count = txs.isArray() ? static_cast<int>(txs.size()) : 0;
        moved = blockTotalMoved(block);
        mined_by = BlockMinerLabel(block);
        if (mined_by.isEmpty()) mined_by = tr("Unknown");

        bool ok_time = false;
        const qint64 ts = ToQString(block.find_value("time")).toLongLong(&ok_time);
        if (ok_time && ts > 0) {
            item->setData(0, Qt::UserRole + 2, ts);
        } else {
            item->setData(0, Qt::UserRole + 2, QVariant());
        }

        const QString bits_hex = ToQString(block.find_value("bits"));
        item->setData(0, Qt::UserRole + 3, bits_hex);
    }

    item->setText(0, tr("Block # %1").arg(height));
    item->setText(1, QString::number(tx_count));
    item->setText(2, FormatAmountFromDouble(moved));
    item->setText(3, mined_by);

    item->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);
    item->setTextAlignment(1, Qt::AlignHCenter | Qt::AlignVCenter);
    item->setTextAlignment(2, Qt::AlignHCenter | Qt::AlignVCenter);
    item->setTextAlignment(3, Qt::AlignLeft | Qt::AlignVCenter);
}

void ExplorerPage::rebuildDifficultyHistoryGraph()
{
    if (!m_networkStatsGraph || !m_blockList) return;

    struct BlockPoint {
        qint64 time_ms{0};
        QString bits_hex;
    };

    QVector<BlockPoint> blocks;
    blocks.reserve(m_blockList->topLevelItemCount());

    for (int i = 0; i < m_blockList->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_blockList->topLevelItem(i);
        if (!item) continue;

        bool ok_time = false;
        const qint64 ts = item->data(0, Qt::UserRole + 2).toLongLong(&ok_time);
        const QString bits_hex = item->data(0, Qt::UserRole + 3).toString().trimmed();

        if (!ok_time || ts <= 0 || bits_hex.isEmpty()) continue;

        BlockPoint p;
        p.time_ms = ts * 1000;
        p.bits_hex = bits_hex;
        blocks.push_back(p);
    }

    std::sort(blocks.begin(), blocks.end(),
        [](const BlockPoint& a, const BlockPoint& b) {
            if (a.time_ms != b.time_ms) return a.time_ms < b.time_ms;
            return a.bits_hex < b.bits_hex;
        });

    QVector<NetworkStatsGraphWidget::DifficultyPoint> points;
    points.reserve(blocks.size());

    // Historical segments:
    // from block[i].time -> next boundary, draw NEXT block's bits/difficulty.
    for (int i = 0; i + 1 < blocks.size(); ++i) {
        const double diff = DifficultyFromBitsHex(blocks[i + 1].bits_hex);
        if (!std::isfinite(diff) || diff <= 0.0) continue;

        NetworkStatsGraphWidget::DifficultyPoint p;
        p.time_ms = blocks[i].time_ms;
        p.difficulty = static_cast<float>(diff);
        points.push_back(p);
    }

    // Live tail:
    // from latest found block time -> now, draw CURRENT getblocktemplate difficulty.
    if (!blocks.isEmpty() && m_client_model) {
        double live_diff = 0.0;

        try {
            UniValue gbt_params(UniValue::VARR);
            UniValue capabilities(UniValue::VARR);
            capabilities.push_back("proposal");

            UniValue req(UniValue::VOBJ);
            req.pushKV("capabilities", capabilities);
            gbt_params.push_back(req);

            UniValue gbt = m_client_model->node().executeRpc("getblocktemplate", gbt_params, "/");
            if (gbt.isObject()) {
                const QString bits_hex = ToQString(gbt.find_value("bits"));
                const double d = DifficultyFromBitsHex(bits_hex);
                if (std::isfinite(d) && d > 0.0) {
                    live_diff = d;
                }
            }
        } catch (...) {
        }

        if ((!std::isfinite(live_diff) || live_diff <= 0.0) && !blocks.back().bits_hex.isEmpty()) {
            const double d = DifficultyFromBitsHex(blocks.back().bits_hex);
            if (std::isfinite(d) && d > 0.0) {
                live_diff = d;
            }
        }

        if (std::isfinite(live_diff) && live_diff > 0.0) {
            NetworkStatsGraphWidget::DifficultyPoint tail;
            tail.time_ms = blocks.back().time_ms;
            tail.difficulty = static_cast<float>(live_diff);
            points.push_back(tail);
        }
    }

    std::sort(points.begin(), points.end(),
        [](const NetworkStatsGraphWidget::DifficultyPoint& a,
           const NetworkStatsGraphWidget::DifficultyPoint& b) {
            if (a.time_ms != b.time_ms) return a.time_ms < b.time_ms;
            return a.difficulty < b.difficulty;
        });

    m_networkStatsGraph->setDifficultyHistory(points);
}

QSet<QString> ExplorerPage::collectInputAddressesForTx(const UniValue& tx) const
{
    QSet<QString> in_addrs;
    if (!m_client_model || !tx.isObject()) return in_addrs;

    const UniValue& vin = tx.find_value("vin");
    if (!vin.isArray()) return in_addrs;

    if (vin.size() > 0 && vin[0].find_value("coinbase").isStr()) return in_addrs;

    for (unsigned int i = 0; i < vin.size(); ++i) {
        const UniValue& in = vin[i];
        if (!in.isObject()) continue;
        if (in.find_value("coinbase").isStr()) continue;

        const QString prev_txid = ToQString(in.find_value("txid"));
        bool ok = false;
        const int prev_vout = ToQString(in.find_value("vout")).toInt(&ok);
        if (prev_txid.isEmpty() || !ok || prev_vout < 0) continue;

        try {
            UniValue prev_params(UniValue::VARR);
            prev_params.push_back(prev_txid.toStdString());
            prev_params.push_back(true);

            UniValue prev_tx = m_client_model->node().executeRpc("getrawtransaction", prev_params, "/");
            if (!prev_tx.isObject()) continue;

            const UniValue& prev_vouts = prev_tx.find_value("vout");
            if (!prev_vouts.isArray()) continue;
            if ((unsigned int)prev_vout >= prev_vouts.size()) continue;

            const UniValue& prev_out = prev_vouts[prev_vout];
            const UniValue& spk = prev_out.find_value("scriptPubKey");
            const QStringList addrs = addressesFromScriptPubKey(spk);
            for (const QString& a : addrs) {
                if (!a.isEmpty()) in_addrs.insert(a);
            }
        } catch (...) {
            continue;
        }
    }

    return in_addrs;
}

QString ExplorerPage::makeTxListLabel(const UniValue& tx) const
{
    if (!tx.isObject()) return tr("Transaction");

    const UniValue& vin = tx.find_value("vin");
    const bool is_coinbase =
        vin.isArray() && vin.size() > 0 && vin[0].find_value("coinbase").isStr();

    const QString conf_text = ToQString(tx.find_value("confirmations"));
    bool ok_conf = false;
    const int conf_count = conf_text.toInt(&ok_conf);

    const QSet<QString> input_addrs = (!is_coinbase) ? collectInputAddressesForTx(tx) : QSet<QString>{};

    const UniValue& vout = tx.find_value("vout");
    double total_out = 0.0;
    QString best_to;

    if (vout.isArray()) {
        for (unsigned int i = 0; i < vout.size(); ++i) {
            const UniValue& outv = vout[i];
            total_out += valueAsDouble(outv.find_value("value"));

            if (best_to.isEmpty()) {
                const QStringList addrs = addressesFromScriptPubKey(outv.find_value("scriptPubKey"));
                for (const QString& a : addrs) {
                    if (a.isEmpty()) continue;
                    if (!is_coinbase && input_addrs.contains(a)) {
                        continue;
                    }
                    best_to = a;
                    break;
                }
            }
        }

        if (best_to.isEmpty()) {
            for (unsigned int i = 0; i < vout.size(); ++i) {
                const UniValue& outv = vout[i];
                const QStringList addrs = addressesFromScriptPubKey(outv.find_value("scriptPubKey"));
                for (const QString& a : addrs) {
                    if (!a.isEmpty()) {
                        best_to = a;
                        break;
                    }
                }
                if (!best_to.isEmpty()) break;
            }
        }
    }

    const QString amount_text = FormatAmountFromDouble(total_out);

    if (is_coinbase) {
        QString maturity_text;

        if (ok_conf && conf_count > 0) {
            if (conf_count >= 101) {
                maturity_text = tr("Mature");
            } else {
                maturity_text = tr("Immature (%1 of 100)").arg(conf_count);
            }
        } else {
            maturity_text = tr("Pending");
        }

        return tr("Mining Reward - %1 — %2").arg(maturity_text, amount_text);
    }

    if (!best_to.isEmpty()) {
        return tr("To %1 — %2").arg(best_to, amount_text);
    }

    return tr("Transfer — %1").arg(amount_text);
}

QString ExplorerPage::formatBlockForDisplay(const UniValue& block) const
{
    if (!block.isObject()) {
        return QString("<pre>%1</pre>").arg(QString::fromStdString(block.write(2)).toHtmlEscaped());
    }

    const QString hash = ToQString(block.find_value("hash"));
    const QString height = ToQString(block.find_value("height"));
    const QString conf = ToQString(block.find_value("confirmations"));
    const QString miner_label = BlockMinerLabel(block);

    if (m_detailedToggle && m_detailedToggle->isChecked()) {
        QString out;
        out += "<b>Block</b><br>";
        out += tr("hash: %1<br>").arg(LinkBlockHash(hash, hash));
        out += tr("confirmations: %1<br>").arg(H(conf));
        out += tr("height: %1<br>").arg(H(height));
        out += tr("version: %1<br>").arg(H(ToQString(block.find_value("version"))));
        out += tr("versionHex: %1<br>").arg(H(ToQString(block.find_value("versionHex"))));
        out += tr("time: %1<br>").arg(H(HumanTimeOrRaw(block.find_value("time"))));
        out += tr("mediantime: %1<br>").arg(H(HumanTimeOrRaw(block.find_value("mediantime"))));
        out += tr("nonce: %1<br>").arg(H(ToQString(block.find_value("nonce"))));
        out += tr("bits: %1<br>").arg(H(ToQString(block.find_value("bits"))));
        out += tr("difficulty: %1<br>").arg(H(ToQString(block.find_value("difficulty"))));
        out += tr("chainwork: %1<br>").arg(SmallMono(ToQString(block.find_value("chainwork"))));
        out += tr("size: %1<br>").arg(H(ToQString(block.find_value("size"))));
        out += tr("strippedsize: %1<br>").arg(H(ToQString(block.find_value("strippedsize"))));
        out += tr("weight: %1<br>").arg(H(ToQString(block.find_value("weight"))));
        out += tr("merkleroot: %1<br>").arg(SmallMono(ToQString(block.find_value("merkleroot"))));
        out += tr("previousblockhash: %1<br>").arg(SmallMono(ToQString(block.find_value("previousblockhash"))));
        out += tr("nextblockhash: %1<br>").arg(SmallMono(ToQString(block.find_value("nextblockhash"))));
        const UniValue& txs = block.find_value("tx");
        if (txs.isArray()) out += tr("tx count: %1<br>").arg((int)txs.size());
        out += tr("total moved: %1<br>").arg(H(FormatAmountFromDouble(blockTotalMoved(block))));
        out += tr("mined by: %1<br>").arg(H(miner_label.isEmpty() ? tr("Unknown") : miner_label));
        out += "<br><i>Select a transaction to inspect it.</i><br>";
        return out;
    }

    QString out;
    out += "<b>Block Summary</b><br>";
    out += tr("Height: %1<br>").arg(H(height));
    out += tr("Confirmations: %1<br>").arg(H(conf));
    out += tr("Time: %1<br>").arg(H(HumanTimeOrRaw(block.find_value("time"))));

    const UniValue& txs = block.find_value("tx");
    if (txs.isArray()) out += tr("Transactions: %1<br>").arg((int)txs.size());
    out += tr("Total CAP Moved: %1<br>").arg(H(FormatAmountFromDouble(blockTotalMoved(block))));
    out += tr("Mined By: %1<br>").arg(H(miner_label.isEmpty() ? tr("Unknown") : miner_label));

    QString mined_to;
    if (txs.isArray() && txs.size() > 0 && txs[0].isObject()) {
        const UniValue& coinbase = txs[0];
        const UniValue& vout = coinbase.find_value("vout");
        double reward = 0.0;
        if (vout.isArray()) {
            for (unsigned int i = 0; i < vout.size(); ++i) {
                const UniValue& outv = vout[i];
                reward += valueAsDouble(outv.find_value("value"));
                if (mined_to.isEmpty()) {
                    const QStringList addrs = addressesFromScriptPubKey(outv.find_value("scriptPubKey"));
                    if (!addrs.isEmpty()) mined_to = addrs.front();
                }
            }
        }
        if (!mined_to.isEmpty()) out += tr("Mined To: %1<br>").arg(LinkAddr(mined_to));
        out += tr("Reward: %1<br>").arg(H(FormatAmountFromDouble(reward)));
    }

    out += tr("Block Hash: %1<br>").arg(LinkBlockHash(hash, hash));
    out += "<br><i>Tip: click a transaction for a simple summary.</i><br>";
    return out;
}

QString ExplorerPage::formatTxForDisplay(const UniValue& tx) const
{
    if (!tx.isObject()) {
        return QString("<pre>%1</pre>").arg(QString::fromStdString(tx.write(2)).toHtmlEscaped());
    }

    const QString txid = ToQString(tx.find_value("txid"));
    const QString blockhash = ToQString(tx.find_value("blockhash"));

    auto IsMineAddr = [&](const QString& addr) -> bool {
        if (!m_client_model || addr.isEmpty()) return false;
        try {
            UniValue p(UniValue::VARR);
            p.push_back(addr.toStdString());
            UniValue r = m_client_model->node().executeRpc("getaddressinfo", p, "/");
            if (!r.isObject()) return false;

            const UniValue& ismine = r.find_value("ismine");
            if (ismine.isBool() && ismine.get_bool()) return true;

            const UniValue& iswatch = r.find_value("iswatchonly");
            if (iswatch.isBool() && iswatch.get_bool()) return true;
        } catch (...) {
        }
        return false;
    };

    struct InLine {
        QString prev_txid;
        int vout{-1};
        QString addr;
        double amount{0.0};
        bool ismine{false};
    };

    struct OutLine {
        int n{-1};
        QString addr;
        QString type;
        double amount{0.0};
        bool ismine{false};
        bool is_change{false};
        bool has_address{false};
        bool is_zero_value{false};
        bool is_op_return{false};
    };

    const UniValue& vin = tx.find_value("vin");
    const bool is_coinbase =
        vin.isArray() && vin.size() > 0 && vin[0].find_value("coinbase").isStr();

    QList<InLine> inputs;
    double total_in = 0.0;
    bool any_input_mine = false;

    if (!is_coinbase && vin.isArray() && m_client_model) {
        for (unsigned int i = 0; i < vin.size(); ++i) {
            const UniValue& in = vin[i];
            if (!in.isObject()) continue;
            if (in.find_value("coinbase").isStr()) continue;

            const QString prev_txid = ToQString(in.find_value("txid"));
            bool ok = false;
            const int prev_vout = ToQString(in.find_value("vout")).toInt(&ok);
            if (prev_txid.isEmpty() || !ok || prev_vout < 0) continue;

            try {
                UniValue prev_params(UniValue::VARR);
                prev_params.push_back(prev_txid.toStdString());
                prev_params.push_back(true);
                UniValue prev_tx = m_client_model->node().executeRpc("getrawtransaction", prev_params, "/");
                if (!prev_tx.isObject()) continue;

                const UniValue& prev_vouts = prev_tx.find_value("vout");
                if (!prev_vouts.isArray()) continue;
                if ((unsigned int)prev_vout >= prev_vouts.size()) continue;

                const UniValue& prev_out = prev_vouts[prev_vout];
                const double amt = valueAsDouble(prev_out.find_value("value"));

                QString addr;
                const UniValue& spk = prev_out.find_value("scriptPubKey");
                const QStringList addrs = addressesFromScriptPubKey(spk);
                if (!addrs.isEmpty()) addr = addrs.front();

                InLine line;
                line.prev_txid = prev_txid;
                line.vout = prev_vout;
                line.addr = addr;
                line.amount = amt;
                line.ismine = IsMineAddr(addr);

                inputs.push_back(line);
                total_in += amt;
                if (line.ismine) any_input_mine = true;
            } catch (...) {
                continue;
            }
        }
    }

    QList<OutLine> outputs;
    double total_out = 0.0;
    bool any_output_mine = false;
    bool any_output_not_mine = false;

    const UniValue& vout = tx.find_value("vout");
    if (vout.isArray()) {
        for (unsigned int i = 0; i < vout.size(); ++i) {
            const UniValue& outv = vout[i];
            if (!outv.isObject()) continue;

            OutLine o;
            o.amount = valueAsDouble(outv.find_value("value"));
            o.is_zero_value = (o.amount == 0.0);

            bool okn = false;
            o.n = ToQString(outv.find_value("n")).toInt(&okn);
            if (!okn) o.n = (int)i;

            const UniValue& spk = outv.find_value("scriptPubKey");
            o.type = ToQString(spk.find_value("type"));
            o.is_op_return = (o.type == "nulldata");

            const QStringList addrs = addressesFromScriptPubKey(spk);
            if (!addrs.isEmpty()) {
                o.addr = addrs.front();
                o.has_address = true;
            }

            o.ismine = IsMineAddr(o.addr);

            outputs.push_back(o);
            total_out += o.amount;

            if (o.ismine) {
                any_output_mine = true;
            } else if (o.has_address) {
                any_output_not_mine = true;
            }
        }
    }

    double fee = 0.0;
    if (!is_coinbase && total_in > 0.0) {
        fee = total_in - total_out;
        if (fee < 0.0) fee = 0.0;
    }

    const bool looks_like_send = (!is_coinbase && any_input_mine && any_output_mine && any_output_not_mine);
    if (looks_like_send) {
        for (auto& o : outputs) {
            if (o.ismine) o.is_change = true;
        }
    } else {
        for (auto& o : outputs) o.is_change = false;
    }

    const QString conf_text = ToQString(tx.find_value("confirmations"));
    bool ok_conf = false;
    const int conf_count = conf_text.toInt(&ok_conf);

    const UniValue& blocktime_val = tx.find_value("blocktime");
    const UniValue& time_val = tx.find_value("time");
    const UniValue& best_time_val = blocktime_val.isNull() ? time_val : blocktime_val;
    const QString time_text = HumanTimeOrRaw(best_time_val);

    if (m_detailedToggle && m_detailedToggle->isChecked()) {
        QString out;
        out += "<b>Transaction</b><br>";
        out += tr("txid: %1<br>").arg(LinkTx(txid, txid));
        out += tr("hash: %1<br>").arg(SmallMono(ToQString(tx.find_value("hash"))));
        out += tr("version: %1<br>").arg(H(ToQString(tx.find_value("version"))));
        out += tr("size: %1<br>").arg(H(ToQString(tx.find_value("size"))));
        out += tr("vsize: %1<br>").arg(H(ToQString(tx.find_value("vsize"))));
        out += tr("weight: %1<br>").arg(H(ToQString(tx.find_value("weight"))));
        out += tr("locktime: %1<br>").arg(H(ToQString(tx.find_value("locktime"))));

        if (!blockhash.isEmpty()) {
            out += tr("blockhash: %1<br>").arg(LinkBlockHash(blockhash, ShortHash(blockhash)));
        }

        if (!conf_text.isEmpty()) out += tr("confirmations: %1<br>").arg(H(conf_text));
        if (!time_text.isEmpty()) out += tr("time: %1<br>").arg(H(time_text));

        if (is_coinbase) {
            out += tr("<br><b>Flow</b><br>");
            out += tr("Type: <b>Coinbase Reward</b><br>");
        } else if (looks_like_send) {
            out += tr("<br><b>Flow</b><br>");
            out += tr("Type: <b>Send</b><br>");
        } else if (!is_coinbase && any_output_mine && !any_output_not_mine) {
            out += tr("<br><b>Flow</b><br>");
            out += tr("Type: <b>Self Transfer</b><br>");
        } else if (!is_coinbase && !any_input_mine && any_output_mine) {
            out += tr("<br><b>Flow</b><br>");
            out += tr("Type: <b>Receive</b><br>");
        } else {
            out += tr("<br><b>Flow</b><br>");
            out += tr("Type: <b>Transfer</b><br>");
        }

        if (!is_coinbase && total_in > 0.0) {
            out += tr("Total In: %1<br>").arg(H(FormatAmountFromDouble(total_in)));
            out += tr("Total Out: %1<br>").arg(H(FormatAmountFromDouble(total_out)));
            out += tr("Fee: %1<br>").arg(H(FormatAmountFromDouble(fee)));
        } else {
            out += tr("Total Out: %1<br>").arg(H(FormatAmountFromDouble(total_out)));
        }

        out += "<br><b>Inputs</b><br>";
        if (is_coinbase) {
            out += tr("<i>Coinbase (newly minted)</i><br>");
        } else if (inputs.isEmpty()) {
            out += tr("<i>Unavailable (could not resolve prevouts)</i><br>");
        } else {
            out += "<ul>";
            for (const auto& inl : inputs) {
                const QString outpoint = QString("%1:%2").arg(inl.prev_txid, QString::number(inl.vout));

                QString label = inl.addr.isEmpty() ? tr("Unknown") : LinkAddr(inl.addr);
                if (inl.ismine) label += " <b>(mine)</b>";

                out += QString("<li>%1 — %2 — %3</li>")
                           .arg(label)
                           .arg(H(FormatAmountFromDouble(inl.amount)))
                           .arg(SmallMono(outpoint));
            }
            out += "</ul>";
        }

        out += "<br><b>Outputs</b><br>";
        if (outputs.isEmpty()) {
            out += tr("<i>No outputs</i><br>");
        } else {
            out += "<ul>";
            for (const auto& o : outputs) {
                const QString outpoint = QString("%1:%2").arg(txid, QString::number(o.n));

                QString label;
                if (o.has_address) {
                    label = LinkAddr(o.addr);
                    if (o.is_change) label += " <b>(CHANGE)</b>";
                    else if (o.ismine) label += " <b>(mine)</b>";
                } else if (o.is_op_return) {
                    label = tr("OP_RETURN Metadata");
                } else if (o.is_zero_value) {
                    label = tr("Metadata Output");
                } else {
                    label = tr("Non-standard Output");
                }

                out += QString("<li>%1 — %2 — %3</li>")
                           .arg(label)
                           .arg(H(FormatAmountFromDouble(o.amount)))
                           .arg(SmallMono(outpoint));
            }
            out += "</ul>";
        }

        out += "<br><b>Raw Inputs (vin)</b><br>";
        out += "<pre>" + QString::fromStdString(vin.write(2)).toHtmlEscaped() + "</pre>";
        out += "<br><b>Raw Outputs (vout)</b><br>";
        out += "<pre>" + QString::fromStdString(vout.write(2)).toHtmlEscaped() + "</pre>";

        return out;
    }

    QString out;
    out += "<b>Transaction Summary</b><br>";

    QString type_text = tr("Transfer");
    if (is_coinbase) type_text = tr("Mining Reward");
    else if (looks_like_send) type_text = tr("Send (with Change)");
    else if (!is_coinbase && any_output_mine && !any_output_not_mine) type_text = tr("Self Transfer");
    else if (!is_coinbase && !any_input_mine && any_output_mine) type_text = tr("Receive");
    else if (!is_coinbase && outputs.size() > 1) type_text = tr("Transfer (Multiple Outputs)");

    QString status_text = tr("Pending");
    if (ok_conf && conf_count > 0) {
        if (is_coinbase) {
            if (conf_count >= 101) status_text = tr("Confirmed - Mature");
            else status_text = tr("Confirmed - Immature (%1 of 100)").arg(conf_count);
        } else {
            if (conf_count >= 6) status_text = tr("Confirmed - Fully Confirmed");
            else status_text = tr("Confirmed - %1 Confirmation%2 (%1 of 6 Recommended)")
                                  .arg(conf_count)
                                  .arg(conf_count == 1 ? "" : "s");
        }
    }

    out += tr("Type: %1<br>").arg(H(type_text));
    if (!conf_text.isEmpty()) out += tr("Confirmations: %1<br>").arg(H(conf_text));
    if (!time_text.isEmpty()) out += tr("Time: %1<br>").arg(H(time_text));
    out += tr("Status: %1<br>").arg(H(status_text));

    out += "<br><b>Inputs</b><br>";
    if (is_coinbase) {
        out += tr("<i>Coinbase (newly minted)</i><br>");
    } else if (inputs.isEmpty()) {
        out += tr("<i>Unavailable (could not resolve prevouts)</i><br>");
    } else {
        out += "<ul>";
        for (const auto& inl : inputs) {
            const QString outpoint = QString("%1:%2").arg(inl.prev_txid, QString::number(inl.vout));

            QString label = inl.addr.isEmpty() ? tr("Unknown") : LinkAddr(inl.addr);
            if (inl.ismine) label += " <b>(mine)</b>";

            out += QString("<li>%1 — %2 — %3</li>")
                       .arg(label)
                       .arg(H(FormatAmountFromDouble(inl.amount)))
                       .arg(SmallMono(outpoint));
        }
        out += "</ul>";
    }

    out += "<b>Outputs</b><br>";

    QStringList visible_outputs;
    for (const auto& o : outputs) {
        if (!o.has_address && o.is_zero_value) continue;

        const QString outpoint = QString("%1:%2").arg(txid, QString::number(o.n));

        QString label;
        if (o.has_address) {
            label = LinkAddr(o.addr);
            if (o.is_change) label += " <b>(CHANGE)</b>";
            else if (o.ismine) label += " <b>(mine)</b>";
        } else if (o.is_op_return) {
            label = tr("OP_RETURN Metadata");
        } else {
            label = tr("Non-standard Output");
        }

        visible_outputs.push_back(
            QString("<li>%1 — %2 — %3</li>")
                .arg(label)
                .arg(H(FormatAmountFromDouble(o.amount)))
                .arg(SmallMono(outpoint)));
    }

    if (visible_outputs.isEmpty()) {
        out += tr("<i>No spendable outputs</i><br>");
    } else {
        out += "<ul>";
        for (const QString& line : visible_outputs) out += line;
        out += "</ul>";
    }

    out += "<b>Totals</b><br>";
    if (!is_coinbase && total_in > 0.0) {
        out += tr("Total In: %1<br>").arg(H(FormatAmountFromDouble(total_in)));
        out += tr("Total Out: %1<br>").arg(H(FormatAmountFromDouble(total_out)));
        out += tr("Fee: %1<br>").arg(H(FormatAmountFromDouble(fee)));
    } else {
        out += tr("Total Out: %1<br>").arg(H(FormatAmountFromDouble(total_out)));
    }

    if (!is_coinbase && ok_conf && conf_count > 0 && conf_count < 6) {
        out += tr("Recommended Safety: %1 of 6 confirmations<br>").arg(conf_count);
    }
    if (is_coinbase && ok_conf && conf_count > 0) {
        if (conf_count >= 101) {
            out += tr("Spendable: Yes<br>");
        } else {
            out += tr("Spendable: No<br>");
            out += tr("Matures In: %1 block(s)<br>").arg(101 - conf_count);
        }
    }

    out += tr("<br>Transaction ID: %1<br>").arg(LinkTx(txid, txid));

    return out;
}

QString ExplorerPage::formatAddressForDisplay(const QString& address) const
{
    if (!m_client_model || address.isEmpty()) {
        return tr("Please enter a valid address.");
    }

    try {
        UniValue validate_params(UniValue::VARR);
        validate_params.push_back(address.toStdString());
        UniValue validate_result = m_client_model->node().executeRpc("validateaddress", validate_params, "/");
        if (!validate_result.isObject()) return tr("Nothing found.");

        const UniValue& isvalid = validate_result.find_value("isvalid");
        if (!isvalid.isBool() || !isvalid.get_bool()) {
            return tr("Nothing found.\n\nThat does not appear to be a valid CapStash address.");
        }

        UniValue scan_result;
        bool scan_ok = false;
        int matching_unspent_count = 0;
        try {
            UniValue scan_params(UniValue::VARR);
            scan_params.push_back("start");
            UniValue scan_targets(UniValue::VARR);
            scan_targets.push_back(QString("addr(%1)").arg(address).toStdString());
            scan_params.push_back(scan_targets);
            scan_result = m_client_model->node().executeRpc("scantxoutset", scan_params, "/");
            scan_ok = scan_result.isObject();

            if (scan_ok) {
                const UniValue& unspents = scan_result.find_value("unspents");
                if (unspents.isArray()) {
                    matching_unspent_count = static_cast<int>(unspents.size());
                }
            }
        } catch (...) {
            scan_ok = false;
        }

        double total_received = 0.0;
        double total_sent = 0.0;
        int receive_count = 0;
        int send_count = 0;

        QStringList recent_activity;
        QSet<QString> receive_txids;
        QSet<QString> send_txids;

        const int tip = rpcGetBlockCount();
        if (tip >= 0) {
            for (int h = tip; h >= 0; --h) {
                QString block_hash;
                try {
                    block_hash = rpcGetBlockHash(h);
                } catch (...) {
                    continue;
                }
                if (block_hash.isEmpty()) continue;

                UniValue block_params(UniValue::VARR);
                block_params.push_back(block_hash.toStdString());
                block_params.push_back(2);

                UniValue block;
                try {
                    block = m_client_model->node().executeRpc("getblock", block_params, "/");
                } catch (...) {
                    continue;
                }
                if (!block.isObject()) continue;

                const QString block_time = HumanTimeOrRaw(block.find_value("time"));
                const UniValue& txs = block.find_value("tx");
                if (!txs.isArray()) continue;

                for (unsigned int i = 0; i < txs.size(); ++i) {
                    const UniValue& tx = txs[i];
                    if (!tx.isObject()) continue;

                    const QString txid = ToQString(tx.find_value("txid"));
                    if (txid.isEmpty()) continue;

                    double recv_amt = 0.0;
                    double sent_amt = 0.0;

                    const UniValue& vin = tx.find_value("vin");
                    const bool is_coinbase =
                        vin.isArray() && vin.size() > 0 && vin[0].find_value("coinbase").isStr();

                    const UniValue& vout = tx.find_value("vout");
                    if (vout.isArray()) {
                        for (unsigned int j = 0; j < vout.size(); ++j) {
                            const UniValue& outv = vout[j];
                            const QStringList addrs = addressesFromScriptPubKey(outv.find_value("scriptPubKey"));
                            if (addrs.contains(address)) {
                                const double amt = valueAsDouble(outv.find_value("value"));
                                recv_amt += amt;
                            }
                        }
                    }

                    if (vin.isArray()) {
                        for (unsigned int j = 0; j < vin.size(); ++j) {
                            const UniValue& in = vin[j];
                            if (in.find_value("coinbase").isStr()) continue;

                            const QString prev_txid = ToQString(in.find_value("txid"));
                            const QString prev_vout_text = ToQString(in.find_value("vout"));
                            bool ok = false;
                            const int prev_vout = prev_vout_text.toInt(&ok);
                            if (prev_txid.isEmpty() || !ok || prev_vout < 0) continue;

                            UniValue prev_params(UniValue::VARR);
                            prev_params.push_back(prev_txid.toStdString());
                            prev_params.push_back(true);

                            UniValue prev_tx;
                            try {
                                prev_tx = m_client_model->node().executeRpc("getrawtransaction", prev_params, "/");
                            } catch (...) {
                                continue;
                            }
                            if (!prev_tx.isObject()) continue;

                            const UniValue& prev_outs = prev_tx.find_value("vout");
                            if (!prev_outs.isArray()) continue;
                            if ((unsigned int)prev_vout >= prev_outs.size()) continue;

                            const UniValue& prev_out = prev_outs[prev_vout];
                            const QStringList prev_addrs = addressesFromScriptPubKey(prev_out.find_value("scriptPubKey"));
                            if (prev_addrs.contains(address)) {
                                const double amt = valueAsDouble(prev_out.find_value("value"));
                                sent_amt += amt;
                            }
                        }
                    }

                    const bool tx_received_match = recv_amt > 0.0;
                    const bool tx_sent_match = sent_amt > 0.0;

                    if (tx_received_match) receive_txids.insert(txid);
                    if (tx_sent_match) send_txids.insert(txid);

                    if (!tx_received_match && !tx_sent_match) continue;

                    total_received += recv_amt;
                    total_sent += sent_amt;

                    QString type;
                    if (is_coinbase && tx_received_match) {
                        type = "Coinbase";
                    } else if (tx_received_match && tx_sent_match) {
                        type = "Self";
                    } else if (tx_received_match) {
                        type = "Receive";
                    } else if (tx_sent_match) {
                        type = "Send";
                    } else {
                        type = "Unknown";
                    }

                    const double delta = recv_amt - sent_amt;
                    const QString delta_sign = (delta >= 0.0) ? "+" : "-";
                    const QString delta_amt = FormatAmountFromDouble(std::abs(delta));

                    const QString line = QString("[%1] %2%3 — %4 — block %5 — %6")
                        .arg(H(type))
                        .arg(delta_sign)
                        .arg(H(delta_amt))
                        .arg(H(block_time))
                        .arg(LinkBlockHeight(h, QString::number(h)))
                        .arg(LinkTx(txid, txid.left(16) + "…"));

                    PushRecent(recent_activity, line, -1);
                }
            }
        }

        receive_count = receive_txids.size();
        send_count = send_txids.size();

        if (m_detailedToggle && m_detailedToggle->isChecked()) {
            QString out;
            out += "<b>Address</b><br>";
            out += tr("Address: %1<br><br>").arg(LinkAddr(address));
            out += "<pre>" + QString::fromStdString(validate_result.write(2)).toHtmlEscaped() + "</pre>";
            if (scan_ok) {
                out += tr("<br><b>UTXO Scan</b><br>");
                out += "<pre>" + QString::fromStdString(scan_result.write(2)).toHtmlEscaped() + "</pre>";
            }
            out += tr("<br><b>History Summary</b><br>");
            out += tr("total_received: %1<br>").arg(H(FormatAmountFromDouble(total_received)));
            out += tr("total_sent: %1<br>").arg(H(FormatAmountFromDouble(total_sent)));
            out += tr("receive_transactions: %1<br>").arg(receive_count);
            out += tr("spend_transactions: %1<br>").arg(send_count);
            out += tr("matching_unspent_outputs: %1<br>").arg(matching_unspent_count);

            if (!recent_activity.isEmpty()) {
                out += tr("<br><b>Activity</b><br>");
                out += "<ul>";
                for (int i = 0; i < recent_activity.size(); ++i) {
                    out += QString("<li>%1</li>").arg(recent_activity[i]);
                }
                out += "</ul>";
            }
            return out;
        }

        QString out;
        out += "<b>Address Summary</b><br>";
        out += tr("Address: %1<br>").arg(LinkAddr(address));
        out += tr("Valid: Yes<br>");

        const UniValue& isscript = validate_result.find_value("isscript");
        if (!isscript.isNull()) {
            out += tr("Script Address: %1<br>").arg(ToQString(isscript) == "true" ? tr("Yes") : tr("No"));
        }

        const UniValue& iswitness = validate_result.find_value("iswitness");
        if (!iswitness.isNull()) {
            out += tr("Witness Address: %1<br>").arg(ToQString(iswitness) == "true" ? tr("Yes") : tr("No"));
        }

        const UniValue& script_pub_key = validate_result.find_value("scriptPubKey");
        const QString spk_hex = ToQString(script_pub_key);
        if (!spk_hex.isEmpty()) {
            const QStringList decoded = rpcDecodeScriptAddresses(spk_hex);
            if (!decoded.isEmpty()) {
                QStringList linked;
                for (const QString& a : decoded) linked.push_back(LinkAddr(a));
                out += tr("Decoded As: %1<br>").arg(linked.join(", "));
            }
        }

        if (scan_ok) {
            const QString total_amount = ToQString(scan_result.find_value("total_amount"));
            const QString best_height = ToQString(scan_result.find_value("height"));

            if (!total_amount.isEmpty()) {
                out += tr("Current Balance: %1 CAP<br>").arg(H(total_amount));
            }
            out += tr("Unspent Outputs: %1<br>").arg(matching_unspent_count);
            if (!best_height.isEmpty()) {
                out += tr("Scanned At Height: %1<br>").arg(H(best_height));
            }
        } else {
            out += tr("Current Balance: Unavailable<br>");
            out += tr("Unspent Outputs: Unavailable<br>");
        }

        out += tr("Total Received: %1<br>").arg(H(FormatAmountFromDouble(total_received)));
        out += tr("Total Sent: %1<br>").arg(H(FormatAmountFromDouble(total_sent)));
        out += tr("Net Flow: %1<br>").arg(H(FormatAmountFromDouble(total_received - total_sent)));
        out += tr("Receive Transactions: %1<br>").arg(receive_count);
        out += tr("Spend Transactions: %1<br>").arg(send_count);

        if (!recent_activity.isEmpty()) {
            out += tr("<br><b>Activity</b><br>");
            out += "<ul>";
            for (int i = 0; i < recent_activity.size(); ++i) {
                out += QString("<li>%1</li>").arg(recent_activity[i]);
            }
            out += "</ul>";
        } else {
            out += tr("<br><b>Activity</b><br><ul><li>No activity found.</li></ul>");
        }

        return out;
    } catch (...) {
        return tr("Nothing found.");
    }
}

void ExplorerPage::loadBlockRow(int height)
{
    const QString hash = rpcGetBlockHash(height);
    if (hash.isEmpty()) return;

    auto* item = new QTreeWidgetItem(m_blockList);
    item->setData(0, Qt::UserRole, height);
    item->setData(0, Qt::UserRole + 1, hash);
    item->setToolTip(0, hash);
    item->setToolTip(1, hash);
    item->setToolTip(2, hash);
    item->setToolTip(3, hash);

    try {
        UniValue params(UniValue::VARR);
        params.push_back(hash.toStdString());
        params.push_back(2);
        UniValue block = m_client_model->node().executeRpc("getblock", params, "/");
        if (block.isObject()) {
            fillBlockRow(item, height, block);

            const UniValue& t = block.find_value("time");
            bool ok = false;
            const int64_t ts = ToQString(t).toLongLong(&ok);
            if (ok && ts > 0 && m_networkStatsGraph) {
                m_networkStatsGraph->addBlockMarker(ts);
            }
            return;
        }
    } catch (...) {
    }

    item->setText(0, tr("Block # %1").arg(height));
    item->setText(1, "?");
    item->setText(2, tr("Unavailable"));
    item->setText(3, tr("Unknown"));

    item->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);
    item->setTextAlignment(1, Qt::AlignHCenter | Qt::AlignVCenter);
    item->setTextAlignment(2, Qt::AlignHCenter | Qt::AlignVCenter);
    item->setTextAlignment(3, Qt::AlignLeft | Qt::AlignVCenter);
}

void ExplorerPage::loadInitialBlocks()
{
    if (m_mempoolList) {
        m_mempoolList->clear();
    }

    m_blockList->clear();
    m_txList->clear();
    if (m_detailView) m_detailView->setText(QString());
    m_cachedTxJson.clear();
    m_cachedMempoolTxJson.clear();
    m_currentBlockHash.clear();
    m_currentBlockJson.clear();
    m_lastViewKind.clear();
    m_lastViewValue.clear();

    refreshMempool();

    const int tip = rpcGetBlockCount();
    if (tip < 0) {
        m_statusLabel->setText(tr("Explorer unavailable"));
        return;
    }

    m_knownTipHeight = tip;

    const int start = std::max(0, tip - 59);
    for (int h = tip; h >= start; --h) {
        loadBlockRow(h);
    }

    m_loadedOldestHeight = start;
    rebuildDifficultyHistoryGraph();
    m_statusLabel->setText(tr("Tip: %1").arg(tip));
}

void ExplorerPage::appendOlderBlocks(int count)
{
    if (m_loadingMore || m_loadedOldestHeight <= 0) return;
    m_loadingMore = true;

    const int next_end = std::max(0, m_loadedOldestHeight - count);
    for (int h = m_loadedOldestHeight - 1; h >= next_end; --h) {
        loadBlockRow(h);
    }

    m_loadedOldestHeight = next_end;
    rebuildDifficultyHistoryGraph();
    m_loadingMore = false;
}

void ExplorerPage::prependNewBlocks(int new_tip_height)
{
    for (int h = m_knownTipHeight + 1; h <= new_tip_height; ++h) {
        const QString hash = rpcGetBlockHash(h);
        if (hash.isEmpty()) continue;

        auto* item = new QTreeWidgetItem();
        item->setData(0, Qt::UserRole, h);
        item->setData(0, Qt::UserRole + 1, hash);
        item->setToolTip(0, hash);
        item->setToolTip(1, hash);
        item->setToolTip(2, hash);
        item->setToolTip(3, hash);

        bool filled = false;

        try {
            UniValue params(UniValue::VARR);
            params.push_back(hash.toStdString());
            params.push_back(2);
            UniValue block = m_client_model->node().executeRpc("getblock", params, "/");

            if (block.isObject()) {
                fillBlockRow(item, h, block);
                filled = true;

                const UniValue& t = block.find_value("time");
                bool ok = false;
                const int64_t ts = ToQString(t).toLongLong(&ok);
                if (ok && ts > 0 && m_networkStatsGraph) {
                    m_networkStatsGraph->addBlockMarker(ts);
                }
            }
        } catch (...) {
        }

        if (!filled) {
            item->setText(0, tr("Block # %1").arg(h));
            item->setText(1, "?");
            item->setText(2, tr("Unavailable"));
            item->setText(3, tr("Unknown"));

            item->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);
            item->setTextAlignment(1, Qt::AlignHCenter | Qt::AlignVCenter);
            item->setTextAlignment(2, Qt::AlignHCenter | Qt::AlignVCenter);
            item->setTextAlignment(3, Qt::AlignLeft | Qt::AlignVCenter);
        }

        m_blockList->insertTopLevelItem(0, item);
    }

    rebuildDifficultyHistoryGraph();
    m_knownTipHeight = new_tip_height;
    m_statusLabel->setText(tr("Tip: %1").arg(new_tip_height));
}

void ExplorerPage::refreshTip()
{
    refreshNetworkStats();
    refreshMempool();

    const int tip = rpcGetBlockCount();
    if (tip < 0) return;

    if (m_knownTipHeight < 0) {
        loadInitialBlocks();
        return;
    }

    if (tip > m_knownTipHeight) {
        prependNewBlocks(tip);
    } else {
        m_statusLabel->setText(tr("Tip: %1").arg(tip));
    }
}

void ExplorerPage::refreshCurrentView()
{
    if (m_lastViewKind == "block" && !m_currentBlockJson.isEmpty()) {
        UniValue block;
        if (ParseJson(m_currentBlockJson, block)) {
            m_detailView->setText(formatBlockForDisplay(block));
        }
        return;
    }

    if (m_lastViewKind == "tx" && m_cachedTxJson.contains(m_lastViewValue)) {
        UniValue tx;
        if (ParseJson(m_cachedTxJson.value(m_lastViewValue), tx)) {
            m_detailView->setText(formatTxForDisplay(tx));
        }
        return;
    }

    if (m_lastViewKind == "address") {
        m_detailView->setText(formatAddressForDisplay(m_lastViewValue));
    }
}

bool ExplorerPage::showBlockByHeight(int height)
{
    const QString hash = rpcGetBlockHash(height);
    if (hash.isEmpty()) {
        m_detailView->setText(tr("Nothing found.<br><br>That block height does not exist."));
        return false;
    }
    return showBlockByHash(hash);
}

bool ExplorerPage::showBlockByHash(const QString& block_hash)
{
    m_txList->clear();
    m_cachedTxJson.clear();
    m_currentBlockHash.clear();
    m_currentBlockJson.clear();

    if (!m_client_model) return false;

    try {
        UniValue params(UniValue::VARR);
        params.push_back(block_hash.toStdString());
        params.push_back(2);

        UniValue result = m_client_model->node().executeRpc("getblock", params, "/");
        if (!result.isObject()) {
            m_detailView->setText(tr("Nothing found.<br><br>That block could not be loaded."));
            return false;
        }

        m_currentBlockHash = block_hash;
        m_currentBlockJson = QString::fromStdString(result.write());
        m_lastViewKind = QStringLiteral("block");
        m_lastViewValue = block_hash;
        m_detailView->setText(formatBlockForDisplay(result));

        const UniValue& block_conf = result.find_value("confirmations");
        const UniValue& block_time = result.find_value("time");

        const UniValue& txs = result.find_value("tx");
        if (txs.isArray()) {
            for (unsigned int i = 0; i < txs.size(); ++i) {
                const UniValue& tx = txs[i];
                QString txid;
                QString label = tr("Transaction");

                if (tx.isObject()) {
                    UniValue tx_copy = tx;

                    if (tx_copy.find_value("confirmations").isNull() && !block_conf.isNull()) {
                        tx_copy.pushKV("confirmations", block_conf);
                    }
                    if (tx_copy.find_value("blocktime").isNull() && !block_time.isNull()) {
                        tx_copy.pushKV("blocktime", block_time);
                    }
                    if (tx_copy.find_value("blockhash").isNull()) {
                        tx_copy.pushKV("blockhash", block_hash.toStdString());
                    }

                    const UniValue& txid_val = tx_copy.find_value("txid");
                    if (txid_val.isStr()) {
                        txid = QString::fromStdString(txid_val.get_str());
                    }

                    if (!txid.isEmpty()) {
                        m_cachedTxJson.insert(txid, QString::fromStdString(tx_copy.write()));
                    }
                    label = makeTxListLabel(tx_copy);
                } else if (tx.isStr()) {
                    txid = QString::fromStdString(tx.get_str());
                }

                if (!txid.isEmpty()) {
                    auto* item = new QListWidgetItem(label, m_txList);
                    item->setData(Qt::UserRole, txid);
                    item->setToolTip(txid);
                }
            }
        }
        return true;
    } catch (...) {
        m_detailView->setText(tr("Nothing found.<br><br>That block could not be loaded."));
        return false;
    }
}

bool ExplorerPage::showTransaction(const QString& txid)
{
    if (m_cachedTxJson.contains(txid)) {
        UniValue tx;
        if (ParseJson(m_cachedTxJson.value(txid), tx)) {
            m_lastViewKind = QStringLiteral("tx");
            m_lastViewValue = txid;
            m_detailView->setText(formatTxForDisplay(tx));
            return true;
        }
    }

    if (m_cachedMempoolTxJson.contains(txid)) {
        UniValue tx;
        if (ParseJson(m_cachedMempoolTxJson.value(txid), tx)) {
            m_lastViewKind = QStringLiteral("tx");
            m_lastViewValue = txid;
            m_detailView->setText(formatTxForDisplay(tx));
            return true;
        }
    }

    if (!m_client_model) return false;

    try {
        UniValue params(UniValue::VARR);
        params.push_back(txid.toStdString());
        params.push_back(true);

        if (!m_currentBlockHash.isEmpty() && m_lastViewKind == QStringLiteral("block")) {
            params.push_back(m_currentBlockHash.toStdString());
        }

        UniValue result = m_client_model->node().executeRpc("getrawtransaction", params, "/");
        if (result.isObject()) {
            const QString json = QString::fromStdString(result.write());

            const UniValue& blockhash_val = result.find_value("blockhash");
            if (blockhash_val.isStr()) {
                m_cachedTxJson.insert(txid, json);
            } else {
                m_cachedMempoolTxJson.insert(txid, json);
            }

            m_lastViewKind = QStringLiteral("tx");
            m_lastViewValue = txid;
            m_detailView->setText(formatTxForDisplay(result));
        } else {
            m_detailView->setText(
                QString("<pre>%1</pre>").arg(QString::fromStdString(result.write(2)).toHtmlEscaped())
            );
        }
        return true;
    } catch (...) {
        m_detailView->setText(tr("Nothing found.<br><br>That transaction could not be loaded."));
        return false;
    }
}

void ExplorerPage::showAddress(const QString& address)
{
    m_txList->clear();
    m_cachedTxJson.clear();
    m_currentBlockHash.clear();
    m_currentBlockJson.clear();
    m_lastViewKind = QStringLiteral("address");
    m_lastViewValue = address;
    m_detailView->setText(formatAddressForDisplay(address));
}

void ExplorerPage::onSearchClicked()
{
    const QString q = m_searchEdit->text().trimmed();
    if (q.isEmpty()) return;

    m_searchEdit->clear();

    bool ok = false;
    const int height = q.toInt(&ok);
    if (ok && q == QString::number(height) && height >= 0) {
        if (showBlockByHeight(height)) return;
    }

    if (LooksLikeHex64(q)) {
        if (showBlockByHash(q)) return;
        if (showTransaction(q)) return;
    }

    showAddress(q);
}

void ExplorerPage::onSearchReturnPressed()
{
    onSearchClicked();
}

void ExplorerPage::onBlockItemClicked(QTreeWidgetItem* item, int)
{
    if (!item) return;
    showBlockByHash(item->data(0, Qt::UserRole + 1).toString());
}

void ExplorerPage::onTxItemClicked(QListWidgetItem* item)
{
    if (!item) return;
    showTransaction(item->data(Qt::UserRole).toString());
}

void ExplorerPage::onBlockListScrollChanged(int value)
{
    if (!m_blockList) return;

    QScrollBar* sb = m_blockList->verticalScrollBar();
    if (!sb) return;

    if (value >= sb->maximum() - 2) {
        appendOlderBlocks(60);
    }
}

void ExplorerPage::onDetailedToggled(bool)
{
    refreshCurrentView();
}

void ExplorerPage::onDetailLinkClicked(const QString& link)
{
    const QString s = link;

    if (s.startsWith("cap:tx:")) {
        showTransaction(s.mid(QString("cap:tx:").size()));
        return;
    }
    if (s.startsWith("cap:block:")) {
        showBlockByHash(s.mid(QString("cap:block:").size()));
        return;
    }
    if (s.startsWith("cap:blockheight:")) {
        bool ok = false;
        const int h = s.mid(QString("cap:blockheight:").size()).toInt(&ok);
        if (ok) showBlockByHeight(h);
        return;
    }
    if (s.startsWith("cap:addr:")) {
        showAddress(s.mid(QString("cap:addr:").size()));
        return;
    }
}
