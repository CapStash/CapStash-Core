// Portions Copyright (c) 2009-2021 The Bitcoin Core developers
// Portions Copyright (c) 2026 The CapStash Core & BitcoinII Core developers
//
// This file contains modifications and additions specific to CapStash.
// Original upstream portions remain subject to their original license terms.
// CapStash-specific original additions are not licensed for reuse,
// modification, or redistribution absent prior written permission
// from the copyright holder.
// Contact: satooshi@bitcoin-ii.org



#include <qt/minerpage.h>

#include <qt/clientmodel.h>
#include <qt/walletmodel.h>
#include <qt/hashrategraphwidget.h>

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>

#include <interfaces/node.h>
#include <univalue.h>
#include <util/strencodings.h>

namespace
{
constexpr double CAPSTASH_TARGET_SPACING_SECONDS = 60.0;

// Keep this matched to the backend miner tag rules.
constexpr int CAPSTASH_MAX_COINBASE_TAG_CHARS = 32;

// Diff-1 / powLimit markers for CapStash.
// These should match the chain's real pow limit / compact bits.
constexpr const char* CAPSTASH_DIFF1_BITS_HEX   = "1d01fffe";
constexpr const char* CAPSTASH_DIFF1_TARGET_HEX = "00000001fffe0000000000000000000000000000000000000000000000000000";

// Safe printable ASCII subset only.
// Allows things like:
//   Vault 1137 Overseer
//   Broken_Node-1
//   CapStash Miner: Alpha
const QRegularExpression CAPSTASH_COINBASE_TAG_REGEX(
    QStringLiteral("^[A-Za-z0-9 .:_-]{0,32}$"));

QString SanitizeCoinbaseTag(QString tag)
{
    tag = tag.trimmed();

    QString out;
    out.reserve(std::min(tag.size(), CAPSTASH_MAX_COINBASE_TAG_CHARS));

    for (const QChar& c : tag) {
        if (out.size() >= CAPSTASH_MAX_COINBASE_TAG_CHARS) {
            break;
        }

        if ((c >= QChar('A') && c <= QChar('Z')) ||
            (c >= QChar('a') && c <= QChar('z')) ||
            (c >= QChar('0') && c <= QChar('9')) ||
            c == QChar(' ') ||
            c == QChar('.') ||
            c == QChar(':') ||
            c == QChar('_') ||
            c == QChar('-')) {
            out.append(c);
        }
    }

    return out.trimmed();
}

QLineEdit* FindCoinbaseTagEdit(QWidget* parent)
{
    return parent ? parent->findChild<QLineEdit*>(QStringLiteral("coinbaseTagEdit")) : nullptr;
}

QString NormalizeHex(QString s)
{
    s = s.trimmed().toLower();
    if (s.startsWith(QStringLiteral("0x"))) {
        s.remove(0, 2);
    }
    return s;
}

bool IsTemplateDifficultyOne(const QString& bitsHex, const QString& targetHex)
{
    const QString bits   = NormalizeHex(bitsHex);
    const QString target = NormalizeHex(targetHex);

    if (!bits.isEmpty() && bits == QString::fromLatin1(CAPSTASH_DIFF1_BITS_HEX)) {
        return true;
    }

    if (!target.isEmpty() && target == QString::fromLatin1(CAPSTASH_DIFF1_TARGET_HEX)) {
        return true;
    }

    return false;
}

static bool IsCoinbaseTagChar(unsigned char c)
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

static QByteArray ParseHexBytes(const QString& hex)
{
    return QByteArray::fromHex(hex.toLatin1());
}

static QString SanitizeCoinbaseTagCandidate(QString s)
{
    s = s.trimmed();

    QString out;
    out.reserve(std::min(s.size(), CAPSTASH_MAX_COINBASE_TAG_CHARS));

    for (const QChar& c : s) {
        if (out.size() >= CAPSTASH_MAX_COINBASE_TAG_CHARS) break;

        if ((c >= QChar('A') && c <= QChar('Z')) ||
            (c >= QChar('a') && c <= QChar('z')) ||
            (c >= QChar('0') && c <= QChar('9')) ||
            c == QChar(' ') ||
            c == QChar('.') ||
            c == QChar(':') ||
            c == QChar('_') ||
            c == QChar('-')) {
            out.append(c);
        }
    }

    return out.trimmed();
}

static QString ExtractCoinbaseTagFromHexScript(const QString& hex_script)
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

static QString FirstAddressFromScriptPubKey(const UniValue& spk)
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

static QString TruncateAddress(const QString& address)
{
    if (address.size() <= 18) return address;
    return address.left(10) + QStringLiteral("…") + address.right(6);
}

static bool QueryRecentLotteryWinner(interfaces::Node& node,
                                     int tip_height,
                                     qint64 tip_time,
                                     QString& winner_text,
                                     int& winner_height)
{
    Q_UNUSED(tip_time);

    winner_text.clear();
    winner_height = -1;

    if (tip_height < 0) return false;

    try {
        UniValue hash_params(UniValue::VARR);
        hash_params.push_back(tip_height);
        UniValue hash_result = node.executeRpc("getblockhash", hash_params, "/");
        if (!hash_result.isStr()) return false;

        const std::string block_hash = hash_result.get_str();

        UniValue block_params(UniValue::VARR);
        block_params.push_back(block_hash);
        block_params.push_back(2);

        UniValue block = node.executeRpc("getblock", block_params, "/");
        if (!block.isObject()) return false;

        const UniValue& bits_val = block.find_value("bits");
        const QString bits_hex = bits_val.isStr()
            ? QString::fromStdString(bits_val.get_str())
            : QString();

        if (NormalizeHex(bits_hex) != QString::fromLatin1(CAPSTASH_DIFF1_BITS_HEX)) {
            return false;
        }

        const UniValue& txs = block.find_value("tx");
        if (!txs.isArray() || txs.size() == 0 || !txs[0].isObject()) {
            return false;
        }

        const UniValue& coinbase_tx = txs[0];
        QString who;

        const UniValue& vin = coinbase_tx.find_value("vin");
        if (vin.isArray() && vin.size() > 0 && vin[0].isObject()) {
            const UniValue& coinbase_hex_val = vin[0].find_value("coinbase");
            if (coinbase_hex_val.isStr()) {
                who = ExtractCoinbaseTagFromHexScript(QString::fromStdString(coinbase_hex_val.get_str()));
            }
        }

        if (who.isEmpty()) {
            const UniValue& vout = coinbase_tx.find_value("vout");
            if (vout.isArray()) {
                for (unsigned int i = 0; i < vout.size(); ++i) {
                    const UniValue& outv = vout[i];
                    if (!outv.isObject()) continue;

                    const QString addr = FirstAddressFromScriptPubKey(outv.find_value("scriptPubKey"));
                    if (!addr.isEmpty()) {
                        who = TruncateAddress(addr);
                        break;
                    }
                }
            }
        }

        if (who.isEmpty()) {
            who = QObject::tr("Unknown Miner");
        }

        winner_text = who;

        const UniValue& height_val = block.find_value("height");
        if (height_val.isNum()) {
            winner_height = height_val.getInt<int>();
        } else {
            winner_height = tip_height;
        }

        return true;
    } catch (...) {
        return false;
    }
}

QString FormatHashrate(double hashps)
{
    if (!std::isfinite(hashps) || hashps < 0.0) {
        hashps = 0.0;
    }

    static const char* units[] = {"H/s", "kH/s", "MH/s", "GH/s", "TH/s", "PH/s"};
    int unit_index = 0;

    while (hashps >= 1000.0 && unit_index < 5) {
        hashps /= 1000.0;
        ++unit_index;
    }

    int decimals = 2;
    if (hashps >= 100.0) {
        decimals = 0;
    } else if (hashps >= 10.0) {
        decimals = 1;
    }

    return QString("%1 %2")
        .arg(QString::number(hashps, 'f', decimals))
        .arg(units[unit_index]);
}

double GetExpectedBlocksPerDay(double local_hashps, double network_hashps, double target_spacing_secs)
{
    if (!std::isfinite(local_hashps) || !std::isfinite(network_hashps) || !std::isfinite(target_spacing_secs)) {
        return 0.0;
    }

    if (local_hashps <= 0.0 || network_hashps <= 0.0 || target_spacing_secs <= 0.0) {
        return 0.0;
    }

    const double total_hashps = std::max(network_hashps, local_hashps);
    const double share = std::clamp(local_hashps / total_hashps, 0.0, 1.0);
    const double network_blocks_per_day = 86400.0 / target_spacing_secs;

    return network_blocks_per_day * share;
}

double GetExpectedBlocksInWindow(double expected_blocks_per_day, double window_days)
{
    if (!std::isfinite(expected_blocks_per_day) || !std::isfinite(window_days)) {
        return 0.0;
    }

    if (expected_blocks_per_day <= 0.0 || window_days <= 0.0) {
        return 0.0;
    }

    return expected_blocks_per_day * window_days;
}

double GetProbabilityAtLeastOne(double expected_blocks_in_window)
{
    if (!std::isfinite(expected_blocks_in_window) || expected_blocks_in_window <= 0.0) {
        return 0.0;
    }

    return 1.0 - std::exp(-expected_blocks_in_window);
}

double GetMeanEtaSeconds(double expected_blocks_per_day)
{
    if (!std::isfinite(expected_blocks_per_day) || expected_blocks_per_day <= 0.0) {
        return 0.0;
    }

    return 86400.0 / expected_blocks_per_day;
}

QString FormatEta(double seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.0) {
        return QObject::tr("N/A");
    }

    if (seconds < 90.0) {
        return QObject::tr("~%1 sec").arg(static_cast<int>(std::round(seconds)));
    }

    const double minutes = seconds / 60.0;
    if (minutes < 90.0) {
        return QObject::tr("~%1 min").arg(static_cast<int>(std::round(minutes)));
    }

    const double hours = minutes / 60.0;
    if (hours < 36.0) {
        return QObject::tr("~%1 hr").arg(QString::number(hours, 'f', 1));
    }

    const double days = hours / 24.0;
    if (days < 21.0) {
        return QObject::tr("~%1 days").arg(QString::number(days, 'f', 1));
    }

    const double weeks = days / 7.0;
    if (weeks < 12.0) {
        return QObject::tr("~%1 weeks").arg(QString::number(weeks, 'f', 1));
    }

    const double months = days / 30.4375;
    if (months < 24.0) {
        return QObject::tr("~%1 months").arg(QString::number(months, 'f', 1));
    }

    const double years = days / 365.25;
    return QObject::tr("~%1 years").arg(QString::number(years, 'f', 1));
}

QString FormatProbability(double p)
{
    if (!std::isfinite(p) || p <= 0.0) {
        return QObject::tr("0.0%");
    }

    if (p >= 0.999) {
        return QObject::tr("> 99.9%");
    }

    const double pct = p * 100.0;

    if (pct < 0.1) {
        return QObject::tr("< 0.1%");
    }

    if (pct >= 10.0) {
        return QObject::tr("%1%").arg(QString::number(pct, 'f', 1));
    }

    return QObject::tr("%1%").arg(QString::number(pct, 'f', 2));
}

static QString FormatElapsedHMS(qint64 ms)
{
    if (ms < 0) ms = 0;

    qint64 secs = ms / 1000;
    const qint64 hours = secs / 3600;
    secs %= 3600;
    const qint64 mins = secs / 60;
    secs %= 60;

    return QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(mins, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
}

static QString LotteryDeactivatedStyle()
{
    return QStringLiteral(
        "QLabel {"
        " color: rgba(160, 160, 160, 140);"
        " font-weight: bold;"
        " background-color: rgba(0, 0, 0, 0);"
        " border: 1px solid rgba(0, 0, 0, 0);"
        " border-radius: 4px;"
        " padding: 2px 6px;"
        "}"
    );
}

static QString LotteryArmedDimStyle()
{
    return QStringLiteral(
        "QLabel {"
        " color: rgba(255, 156, 47, 90);"
        " font-weight: bold;"
        " background-color: rgba(255, 156, 47, 0);"
        " border: 1px solid rgba(255, 156, 47, 0);"
        " border-radius: 4px;"
        " padding: 2px 6px;"
        "}"
    );
}

static QString LotteryOnStyle()
{
    return QStringLiteral(
        "QLabel {"
        " color: rgb(255, 210, 120);"
        " font-weight: bold;"
        " background-color: rgba(255, 156, 47, 25);"
        " border: 1px solid rgba(255, 156, 47, 120);"
        " border-radius: 4px;"
        " padding: 2px 6px;"
        "}"
    );
}

} // namespace

MinerPage::MinerPage(QWidget* parent) : QWidget(parent)
{
    buildUi();

    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(1000);
    connect(m_updateTimer, &QTimer::timeout, this, &MinerPage::updateMiningStats);
    m_updateTimer->start();
}

void MinerPage::setClientModel(ClientModel* model)
{
    m_client_model = model;

    if (!m_client_model && m_updateTimer) {
        m_updateTimer->stop();
    }

    if (m_hashrateGraph) {
        m_hashrateGraph->clear();
    }
    updateMiningStats();
}

void MinerPage::setWalletModel(WalletModel* model)
{
    m_wallet_model = model;
}

void MinerPage::buildUi()
{
    auto* root = new QVBoxLayout(this);

    auto* controlsBox = new QGroupBox(QObject::tr("Miner"), this);
    auto* controls = new QGridLayout(controlsBox);

    auto* addressLabel = new QLabel(QObject::tr("Coinbase address:"), controlsBox);
    m_addressEdit = new QLineEdit(controlsBox);
    m_addressEdit->setPlaceholderText(QObject::tr("Enter CapStash address to mine to"));

    auto* tagLabel = new QLabel(QObject::tr("Mined by / tag:"), controlsBox);
    auto* tagEdit = new QLineEdit(controlsBox);
    tagEdit->setObjectName(QStringLiteral("coinbaseTagEdit"));
    tagEdit->setMaxLength(CAPSTASH_MAX_COINBASE_TAG_CHARS);
    tagEdit->setPlaceholderText(QObject::tr("Optional (e.g. Feral Ghoul)"));
    tagEdit->setToolTip(QObject::tr("Allowed: A-Z a-z 0-9 space . : _ -   |   Max %1 chars")
                        .arg(CAPSTASH_MAX_COINBASE_TAG_CHARS));
    tagEdit->setValidator(new QRegularExpressionValidator(CAPSTASH_COINBASE_TAG_REGEX, tagEdit));

    auto* threadsLabel = new QLabel(QObject::tr("Mining threads:"), controlsBox);
    m_threadsSpin = new QSpinBox(controlsBox);
    m_threadsSpin->setMinimum(1);
    m_threadsSpin->setMaximum(1024);
    m_threadsSpin->setValue(1);

    m_startButton = new QPushButton(QObject::tr("Start Mining"), controlsBox);
    m_stopButton = new QPushButton(QObject::tr("Stop Mining"), controlsBox);
    m_stopButton->setEnabled(false);

    m_statusLabel = new QLabel(QObject::tr("Status: Idle"), controlsBox);
    m_hashrateLabel = new QLabel(QObject::tr("Hashrate: 0.00 H/s"), controlsBox);
    m_blocksMinedLabel = new QLabel(QObject::tr("Blocks found: 0"), controlsBox);

    auto* estimateWindowLabel = new QLabel(QObject::tr("Estimate window:"), controlsBox);
    m_estimateWindowCombo = new QComboBox(controlsBox);
    m_estimateWindowCombo->addItem(QObject::tr("1 hour"),   1.0 / 24.0);
    m_estimateWindowCombo->addItem(QObject::tr("2 hours"),  2.0 / 24.0);
    m_estimateWindowCombo->addItem(QObject::tr("3 hours"),  3.0 / 24.0);
    m_estimateWindowCombo->addItem(QObject::tr("4 hours"),  4.0 / 24.0);
    m_estimateWindowCombo->addItem(QObject::tr("6 hours"),  6.0 / 24.0);
    m_estimateWindowCombo->addItem(QObject::tr("8 hours"),  8.0 / 24.0);
    m_estimateWindowCombo->addItem(QObject::tr("12 hours"), 12.0 / 24.0);
    m_estimateWindowCombo->addItem(QObject::tr("24 hours"), 1.0);
    m_estimateWindowCombo->addItem(QObject::tr("1 week"),   7.0);
    m_estimateWindowCombo->addItem(QObject::tr("2 weeks"),  14.0);
    m_estimateWindowCombo->addItem(QObject::tr("3 weeks"),  21.0);
    m_estimateWindowCombo->addItem(QObject::tr("4 weeks"),  28.0);
    m_estimateWindowCombo->addItem(QObject::tr("2 months"), 60.875);
    m_estimateWindowCombo->addItem(QObject::tr("3 months"), 91.3125);
    m_estimateWindowCombo->addItem(QObject::tr("6 months"), 182.625);
    m_estimateWindowCombo->addItem(QObject::tr("9 months"), 273.9375);
    m_estimateWindowCombo->addItem(QObject::tr("1 year"),   365.25);
    m_estimateWindowCombo->setCurrentIndex(7); // 24 hours

    m_etaLabel = new QLabel(QObject::tr("Mean ETA: N/A"), controlsBox);

    m_lotteryBlockLabel = new QLabel(QObject::tr("Lottery Block Deactivated."), controlsBox);
    m_lotteryBlockLabel->setObjectName(QStringLiteral("lotteryBlockLabel"));
    m_lotteryBlockLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_lotteryBlockLabel->setTextFormat(Qt::PlainText);
    m_lotteryBlockLabel->setWordWrap(false);
    m_lotteryBlockLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_lotteryBlockLabel->setStyleSheet(LotteryDeactivatedStyle());
    {
        QLabel probe_deactivated(QObject::tr("Lottery Block Deactivated."));
        probe_deactivated.setStyleSheet(LotteryDeactivatedStyle());

        QLabel probe_activated(QObject::tr("Lottery Block Activated!"));
        probe_activated.setStyleSheet(LotteryOnStyle());

        const int stable_width = std::max(probe_deactivated.sizeHint().width(),
                                          probe_activated.sizeHint().width());
        const int stable_height = std::max(probe_deactivated.sizeHint().height(),
                                           probe_activated.sizeHint().height());

        m_lotteryBlockLabel->setMinimumWidth(stable_width);
        m_lotteryBlockLabel->setMinimumHeight(stable_height);
        m_lotteryBlockLabel->setMaximumHeight(stable_height);
    }

    auto* expectedBlocksTitleLabel = new QLabel(QObject::tr("Expected Blocks:"), controlsBox);
    m_expectedBlocksLabel = new QLabel(QObject::tr("0.0000 in 24 hours"), controlsBox);

    auto* probabilityTitleLabel = new QLabel(QObject::tr("Chance of ≥1 Block:"), controlsBox);
    m_probabilityLabel = new QLabel(QObject::tr("0.0%"), controlsBox);

    controls->addWidget(addressLabel,   0, 0);
    controls->addWidget(m_addressEdit,  0, 1, 1, 3);

    controls->addWidget(tagLabel,       1, 0);
    controls->addWidget(tagEdit,        1, 1, 1, 3);

    controls->addWidget(threadsLabel,   2, 0);
    controls->addWidget(m_threadsSpin,  2, 1);

    controls->addWidget(m_startButton,  2, 2);
    controls->addWidget(m_stopButton,   2, 3);

    controls->addWidget(m_statusLabel,      3, 0, 1, 1);
    controls->addWidget(m_hashrateLabel,    3, 1, 1, 2);
    controls->addWidget(m_blocksMinedLabel, 3, 3, 1, 1);

    controls->addWidget(estimateWindowLabel,   4, 0);
    controls->addWidget(m_estimateWindowCombo, 4, 1);
    controls->addWidget(m_etaLabel,            4, 2, 1, 2);

    controls->addWidget(expectedBlocksTitleLabel, 5, 0);
    controls->addWidget(m_expectedBlocksLabel,    5, 1);
    controls->addWidget(m_lotteryBlockLabel,      5, 2, 1, 2);

    controls->addWidget(probabilityTitleLabel, 6, 0);
    controls->addWidget(m_probabilityLabel,    6, 1);

    root->addWidget(controlsBox);

    auto* graphBox = new QGroupBox(QObject::tr("Hashrate"), this);
    auto* graphLayout = new QVBoxLayout(graphBox);

    auto* topRow = new QHBoxLayout();
    auto* rangeLabel = new QLabel(QObject::tr("Time range:"), graphBox);
    m_rangeCombo = new QComboBox(graphBox);

    m_rangeCombo->addItem(QObject::tr("1 m"),     1);
    m_rangeCombo->addItem(QObject::tr("5 m"),     5);
    m_rangeCombo->addItem(QObject::tr("10 m"),   10);
    m_rangeCombo->addItem(QObject::tr("30 m"),   30);
    m_rangeCombo->addItem(QObject::tr("1 h"),    60);
    m_rangeCombo->addItem(QObject::tr("6 h"),   360);
    m_rangeCombo->addItem(QObject::tr("12 h"),  720);
    m_rangeCombo->addItem(QObject::tr("24 h"), 1440);
    m_rangeCombo->setCurrentIndex(3);

    topRow->addWidget(rangeLabel);
    topRow->addWidget(m_rangeCombo);
    topRow->addStretch();

    m_hashrateGraph = new HashrateGraphWidget(graphBox);
    m_hashrateGraph->setMinimumHeight(260);
    m_hashrateGraph->setGraphRange(std::chrono::minutes{30});

    graphLayout->addLayout(topRow);
    graphLayout->addWidget(m_hashrateGraph);

    root->addWidget(graphBox, 1);

    connect(m_startButton, &QPushButton::clicked, this, &MinerPage::onStartMiningClicked);
    connect(m_stopButton,  &QPushButton::clicked, this, &MinerPage::onStopMiningClicked);
    connect(m_rangeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MinerPage::onTimeRangeChanged);
    connect(m_estimateWindowCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MinerPage::updateMiningStats);
}

void MinerPage::setMiningUiState(bool mining)
{
    const bool wasMining = m_isMining;

    m_isMining = mining;
    m_startButton->setEnabled(!mining);
    m_stopButton->setEnabled(mining);

    if (!wasMining && mining) {
        m_miningElapsed.restart();
        m_miningElapsedStarted = true;
        m_statusLabel->setText(QObject::tr("Status: Mining — 00:00:00"));
    } else if (wasMining && !mining) {
        m_miningElapsed.invalidate();
        m_miningElapsedStarted = false;
        m_statusLabel->setText(QObject::tr("Status: Idle"));
    } else {
        if (!mining) {
            m_statusLabel->setText(QObject::tr("Status: Idle"));
        }
    }
}

void MinerPage::refreshTheme()
{
    update();
}

bool MinerPage::callSetGenerate(bool mine, int threads, const QString& address, const QString& coinbaseTag)
{
    if (!m_client_model) {
        QMessageBox::warning(this,
                             QObject::tr("Mining Error"),
                             QObject::tr("Client model is not available yet."));
        return false;
    }

    try {
        interfaces::Node& node = m_client_model->node();

        UniValue params(UniValue::VARR);

        if (mine) {
            params.push_back(true);
            params.push_back(threads);
            params.push_back(address.toStdString());
            params.push_back(coinbaseTag.toStdString());
        } else {
            params.push_back(false);
        }

        node.executeRpc("setgenerate", params, "/");
        return true;

    } catch (const UniValue& uv) {
        QString msg = QObject::tr("RPC error.");

        if (uv.isObject()) {
            const UniValue& msg_val = uv.find_value("message");
            const UniValue& code_val = uv.find_value("code");

            if (msg_val.isStr() && code_val.isNum()) {
                msg = QObject::tr("RPC error %1: %2")
                          .arg(code_val.getInt<int>())
                          .arg(QString::fromStdString(msg_val.get_str()));
            } else if (msg_val.isStr()) {
                msg = QString::fromStdString(msg_val.get_str());
            } else {
                msg = QString::fromStdString(uv.write());
            }
        } else if (uv.isStr()) {
            msg = QString::fromStdString(uv.get_str());
        } else {
            msg = QString::fromStdString(uv.write());
        }

        QMessageBox::critical(this, QObject::tr("Mining Error"), msg);
        return false;

    } catch (const std::exception& e) {
        QMessageBox::critical(this,
                              QObject::tr("Mining Error"),
                              QString::fromStdString(e.what()));
        return false;

    } catch (...) {
        QMessageBox::critical(this,
                              QObject::tr("Mining Error"),
                              QObject::tr("Unknown non-RPC exception while calling setgenerate."));
        return false;
    }
}

void MinerPage::onStartMiningClicked()
{
    try {
        const QString address = m_addressEdit->text().trimmed();
        const int threads = m_threadsSpin->value();

        QLineEdit* tagEdit = FindCoinbaseTagEdit(this);
        const QString rawTag = tagEdit ? tagEdit->text() : QString();
        const QString sanitizedTag = SanitizeCoinbaseTag(rawTag);

        if (address.isEmpty()) {
            QMessageBox::warning(this,
                                 QObject::tr("Missing Address"),
                                 QObject::tr("Enter a coinbase address to mine to."));
            return;
        }

        if (!rawTag.isEmpty()) {
            if (sanitizedTag != rawTag.trimmed() || !CAPSTASH_COINBASE_TAG_REGEX.match(sanitizedTag).hasMatch()) {
                QMessageBox::warning(
                    this,
                    QObject::tr("Invalid Tag"),
                    QObject::tr("The Mined by / tag field may only contain A-Z, a-z, 0-9, space, period, colon, underscore, and dash, up to %1 characters.")
                        .arg(CAPSTASH_MAX_COINBASE_TAG_CHARS));
                return;
            }
        }

        if (tagEdit && tagEdit->text() != sanitizedTag) {
            tagEdit->setText(sanitizedTag);
        }

        if (callSetGenerate(true, threads, address, sanitizedTag)) {
            setMiningUiState(true);

            m_lastLocalBlocksFound = 0;
            m_lastLocalBlockHeight = 0;
            m_lastLocalBlockHash.clear();

            if (m_lotteryBlockLabel) {
                m_lotteryBlockLabel->setText(QObject::tr("Lottery Block Deactivated."));
                m_lotteryBlockLabel->setStyleSheet(LotteryDeactivatedStyle());
            }

            if (m_hashrateGraph) {
                m_hashrateGraph->setBestDiffHit(0.0);
            }

            updateMiningStats();
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this,
                              QObject::tr("Mining Error"),
                              QString::fromStdString(e.what()));
    } catch (...) {
        QMessageBox::critical(this,
                              QObject::tr("Mining Error"),
                              QObject::tr("Unhandled exception in Start Mining."));
    }
}

void MinerPage::onStopMiningClicked()
{
    try {
        if (callSetGenerate(false, 0, QString(), QString())) {
            setMiningUiState(false);

            m_lastLocalBlocksFound = 0;
            m_lastLocalBlockHeight = 0;
            m_lastLocalBlockHash.clear();

            if (m_lotteryBlockLabel) {
                m_lotteryBlockLabel->setText(QObject::tr("Lottery Block Deactivated."));
                m_lotteryBlockLabel->setStyleSheet(LotteryDeactivatedStyle());
            }

            if (m_hashrateGraph) {
                m_hashrateGraph->setBestDiffHit(0.0);
            }

            updateMiningStats();
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this,
                              QObject::tr("Mining Error"),
                              QString::fromStdString(e.what()));
    } catch (...) {
        QMessageBox::critical(this,
                              QObject::tr("Mining Error"),
                              QObject::tr("Unhandled exception in Stop Mining."));
    }
}

void MinerPage::onTimeRangeChanged(int)
{
    if (!m_hashrateGraph) return;

    const int minutes = m_rangeCombo->currentData().toInt();
    m_hashrateGraph->setGraphRange(std::chrono::minutes{minutes});
}

double MinerPage::getLocalHashrateKhs() const
{
    if (!m_client_model) return 0.0;

    try {
        interfaces::Node& node = m_client_model->node();

        UniValue params(UniValue::VARR);
        UniValue result = node.executeRpc("getmininginfo", params, "/");

        if (result.isObject()) {
            const UniValue& obj = result.get_obj();

            const UniValue& local_hashps = obj.find_value("localhashps");
            if (local_hashps.isNum()) {
                return local_hashps.get_real() / 1000.0;
            }

            const UniValue& hashes_per_sec = obj.find_value("hashespersec");
            if (hashes_per_sec.isNum()) {
                return hashes_per_sec.get_real() / 1000.0;
            }
        }
    } catch (...) {
    }

    return 0.0;
}

int MinerPage::getLocalBlocksMined() const
{
    if (!m_client_model) return 0;

    try {
        interfaces::Node& node = m_client_model->node();

        UniValue params(UniValue::VARR);
        UniValue result = node.executeRpc("getmininginfo", params, "/");

        if (result.isObject()) {
            const UniValue& obj = result.get_obj();

            const UniValue& local_blocks = obj.find_value("localblocksfound");
            if (local_blocks.isNum()) {
                return local_blocks.getInt<int>();
            }
        }
    } catch (...) {
    }

    return 0;
}

static bool QueryLatestLotteryWinner(interfaces::Node& node,
                                     int tip_height,
                                     QString& winner_text,
                                     int& winner_height,
                                     int max_lookback = 256)
{
    winner_text.clear();
    winner_height = -1;

    if (tip_height < 0) return false;

    const int min_height = std::max(0, tip_height - max_lookback + 1);

    for (int h = tip_height; h >= min_height; --h) {
        QString candidate_text;
        int candidate_height = -1;

        if (QueryRecentLotteryWinner(node, h, 0, candidate_text, candidate_height)) {
            winner_text = candidate_text;
            winner_height = candidate_height;
            return true;
        }
    }

    return false;
}

void MinerPage::updateMiningStats()
{
    double local_hashps = 0.0;
    double network_hashps = 0.0;
    int blocksFound = 0;
    bool mining = m_isMining;

    int network_block_count = -1;

    qint64 gui_tip_time = 0;
    int gui_tip_height = -1;

    bool template_diff_one = false;

    QString recent_lottery_winner;
    int recent_lottery_height = -1;
    bool show_recent_lottery_winner = false;

    int64_t lastLocalBlockTime = 0;
    int lastLocalBlockHeight = 0;
    QString lastLocalBlockHash;

    double bestDiffHit = 0.0;

    if (qApp) {
        const QVariant tipTimeVar = qApp->property("capstash_last_tip_time");
        const QVariant tipHeightVar = qApp->property("capstash_last_tip_height");

        if (tipTimeVar.isValid()) {
            gui_tip_time = tipTimeVar.toLongLong();
        }
        if (tipHeightVar.isValid()) {
            gui_tip_height = tipHeightVar.toInt();
        }
    }

    if (m_client_model) {
        try {
            interfaces::Node& node = m_client_model->node();

            {
                UniValue params(UniValue::VARR);
                UniValue result = node.executeRpc("getmininginfo", params, "/");

                if (result.isObject()) {
                    const UniValue& obj = result.get_obj();

                    const UniValue& local_hashps_val = obj.find_value("localhashps");
                    if (local_hashps_val.isNum()) {
                        local_hashps = local_hashps_val.get_real();
                    } else {
                        const UniValue& hashes_per_sec = obj.find_value("hashespersec");
                        if (hashes_per_sec.isNum()) {
                            local_hashps = hashes_per_sec.get_real();
                        }
                    }

                    const UniValue& network_hashps_val = obj.find_value("networkhashps");
                    if (network_hashps_val.isNum()) {
                        network_hashps = network_hashps_val.get_real();
                    }

                    const UniValue& blocks_val = obj.find_value("blocks");
                    if (blocks_val.isNum()) {
                        network_block_count = blocks_val.getInt<int>();
                    }

                    const UniValue& local_blocks = obj.find_value("localblocksfound");
                    if (local_blocks.isNum()) {
                        blocksFound = local_blocks.getInt<int>();
                    }

                    const UniValue& mining_val = obj.find_value("mining");
                    if (mining_val.isBool()) {
                        mining = mining_val.get_bool();
                    }

                    const UniValue& last_time_val = obj.find_value("lastlocalblocktime");
                    if (last_time_val.isNum()) {
                        lastLocalBlockTime = last_time_val.getInt<int64_t>();
                    }

                    const UniValue& last_height_val = obj.find_value("lastlocalblockheight");
                    if (last_height_val.isNum()) {
                        lastLocalBlockHeight = last_height_val.getInt<int>();
                    }

                    const UniValue& last_hash_val = obj.find_value("lastlocalblockhash");
                    if (last_hash_val.isStr()) {
                        lastLocalBlockHash = QString::fromStdString(last_hash_val.get_str());
                    }

                    const UniValue& best_diff_hit_val = obj.find_value("bestlocaldiffhit");
                    if (best_diff_hit_val.isNum()) {
                        bestDiffHit = best_diff_hit_val.get_real();
                    }
                }
            }

            {
                UniValue params(UniValue::VARR);
                UniValue req(UniValue::VOBJ);
                UniValue rules(UniValue::VARR);
                rules.push_back("segwit");
                req.pushKV("rules", rules);
                params.push_back(req);

                UniValue gbt = node.executeRpc("getblocktemplate", params, "/");
                if (gbt.isObject()) {
                    const UniValue& bits_val = gbt.find_value("bits");
                    const UniValue& target_val = gbt.find_value("target");

                    const QString bits_hex = bits_val.isStr()
                        ? QString::fromStdString(bits_val.get_str())
                        : QString();

                    const QString target_hex = target_val.isStr()
                        ? QString::fromStdString(target_val.get_str())
                        : QString();

                    template_diff_one = IsTemplateDifficultyOne(bits_hex, target_hex);
                }
            }

            {
                if (gui_tip_height >= 0) {
                    show_recent_lottery_winner =
                        QueryLatestLotteryWinner(node, gui_tip_height,
                                                 recent_lottery_winner, recent_lottery_height);
                }
            }
        } catch (...) {
        }
    }

    setMiningUiState(mining);

    if (m_isMining) {
        qint64 elapsed_ms = 0;
        if (m_miningElapsedStarted && m_miningElapsed.isValid()) {
            elapsed_ms = m_miningElapsed.elapsed();
        }
        m_statusLabel->setText(
            QObject::tr("Status: Mining — %1").arg(FormatElapsedHMS(elapsed_ms))
        );
    } else {
        m_statusLabel->setText(QObject::tr("Status: Idle"));
    }

    const bool newLocalBlockFound = (mining && blocksFound > m_lastLocalBlocksFound);

    if (newLocalBlockFound) {
        m_miningElapsedStarted = true;
    }

    if (newLocalBlockFound && m_hashrateGraph && lastLocalBlockTime > 0 && lastLocalBlockHeight > 0) {
        if (lastLocalBlockHeight != m_lastLocalBlockHeight || lastLocalBlockHash != m_lastLocalBlockHash) {
            const qint64 ts_ms = static_cast<qint64>(lastLocalBlockTime) * 1000ll;

            const QString timeText =
                QDateTime::fromSecsSinceEpoch(lastLocalBlockTime)
                    .toString(QLocale().dateTimeFormat(QLocale::ShortFormat));

            m_hashrateGraph->addBlockMarker(ts_ms, lastLocalBlockHeight, timeText, lastLocalBlockHash);

            m_lastLocalBlockHeight = lastLocalBlockHeight;
            m_lastLocalBlockHash = lastLocalBlockHash;
        }
    }

    if (m_hashrateGraph) {
        if (mining) {
            m_hashrateGraph->setBestDiffHit(bestDiffHit);
        } else {
            m_hashrateGraph->setBestDiffHit(0.0);
        }
    }

    const qint64 now_secs = QDateTime::currentSecsSinceEpoch();
    const bool have_tip_height = (gui_tip_height >= 0 || network_block_count >= 0);

    const bool show_lottery_block_incoming =
        mining &&
        template_diff_one &&
        have_tip_height;

    if (m_lotteryBlockLabel) {
        if (show_lottery_block_incoming) {
            const bool blink_on = ((now_secs & 1) == 0);
            m_lotteryBlockLabel->setText(QObject::tr("Lottery Block Activated!"));
            m_lotteryBlockLabel->setStyleSheet(
                blink_on ? LotteryOnStyle() : LotteryArmedDimStyle());
        } else if (show_recent_lottery_winner) {
            m_lotteryBlockLabel->setText(
                QObject::tr("Last Lottery Block Found By: %1  |  #%2")
                    .arg(recent_lottery_winner)
                    .arg(recent_lottery_height));
            m_lotteryBlockLabel->setStyleSheet(LotteryOnStyle());
        } else {
            m_lotteryBlockLabel->setText(QObject::tr("Lottery Block Deactivated."));
            m_lotteryBlockLabel->setStyleSheet(LotteryDeactivatedStyle());
        }
    }

    if (mining) {
        m_lastLocalBlocksFound = blocksFound;
    } else {
        m_lastLocalBlocksFound = 0;
        m_lastLocalBlockHeight = 0;
        m_lastLocalBlockHash.clear();

        if (m_lotteryBlockLabel) {
            if (show_lottery_block_incoming) {
                const bool blink_on = ((now_secs & 1) == 0);
                m_lotteryBlockLabel->setText(QObject::tr("Lottery Block Activated!"));
                m_lotteryBlockLabel->setStyleSheet(
                    blink_on ? LotteryOnStyle() : LotteryArmedDimStyle());
            } else if (show_recent_lottery_winner) {
                m_lotteryBlockLabel->setText(
                    QObject::tr("Last Lottery Block Found By: %1  |  #%2")
                        .arg(recent_lottery_winner)
                        .arg(recent_lottery_height));
                m_lotteryBlockLabel->setStyleSheet(LotteryOnStyle());
            } else {
                m_lotteryBlockLabel->setText(QObject::tr("Lottery Block Deactivated."));
                m_lotteryBlockLabel->setStyleSheet(LotteryDeactivatedStyle());
            }
        }
    }

    m_hashrateLabel->setText(
        QObject::tr("Hashrate: %1").arg(FormatHashrate(local_hashps))
    );

    m_blocksMinedLabel->setText(
        QObject::tr("Blocks found: %1").arg(blocksFound)
    );

    const double expected_blocks_per_day =
        GetExpectedBlocksPerDay(local_hashps, network_hashps, CAPSTASH_TARGET_SPACING_SECONDS);

    const double eta_seconds = GetMeanEtaSeconds(expected_blocks_per_day);

    const double window_days = m_estimateWindowCombo
        ? m_estimateWindowCombo->currentData().toDouble()
        : 1.0;

    const QString window_label = m_estimateWindowCombo
        ? m_estimateWindowCombo->currentText()
        : QObject::tr("24 hours");

    const double expected_blocks_in_window =
        GetExpectedBlocksInWindow(expected_blocks_per_day, window_days);

    const double probability_at_least_one =
        GetProbabilityAtLeastOne(expected_blocks_in_window);

    m_etaLabel->setText(
        QObject::tr("Mean ETA: %1").arg(FormatEta(eta_seconds))
    );

    m_expectedBlocksLabel->setText(
        QObject::tr("%1 in %2")
            .arg(QString::number(expected_blocks_in_window, 'f', 4))
            .arg(window_label)
    );

    m_probabilityLabel->setText(
        FormatProbability(probability_at_least_one)
    );

    if (m_hashrateGraph) {
        m_hashrateGraph->setHashrate(local_hashps);
    }
}
