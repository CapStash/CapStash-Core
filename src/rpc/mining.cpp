// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2026 The CapStash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/CapStash-config.h>
#endif

#include <chain.h>
#include <chainparams.h>
#include <common/system.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <deploymentinfo.h>
#include <deploymentstatus.h>
#include <key_io.h>
#include <net.h>
#include <node/context.h>
#include <node/miner.h>
#include <pow.h>
#include <rpc/blockchain.h>
#include <rpc/mining.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <script/descriptor.h>
#include <script/script.h>
#include <script/signingprovider.h>
#include <txmempool.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/time.h>
#include <util/translation.h>
#include <validation.h>
#include <validationinterface.h>
#include <warnings.h>
#include <crypto/whirlpool.h>

#include <memory>
#include <stdint.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <algorithm>

extern uint256 GetPoWHash(const CBlockHeader& header);

using node::BlockAssembler;
using node::CBlockTemplate;
using node::NodeContext;
using node::RegenerateCommitments;
using node::UpdateTime;

// ===== Simple internal CPU miner (for setgenerate) =====

static std::atomic<bool> g_mining_active{false};
static std::atomic<int>  g_mining_threads{0};

static std::mutex        g_mining_cs;
static NodeContext*      g_mining_node{nullptr};
static CScript           g_mining_script;
static std::string       g_mining_coinbase_tag;

static std::atomic<int64_t>  g_last_local_block_time{0};
static std::atomic<int>      g_last_local_block_height{0};

static std::mutex            g_last_local_block_hash_cs;
static uint256               g_last_local_block_hash;

static std::atomic<uint64_t> g_hashes_done{0};
static std::atomic<int64_t>  g_hashrate_window_start_us{0};
static std::atomic<double>   g_local_hashps{0.0};
static std::atomic<uint64_t> g_local_blocks_found{0};
static std::atomic<double>   g_best_diff_hit{0.0};

static constexpr uint64_t MINER_MAINTENANCE_POLL_MASK = 0xFFFFULL; // every 65536 tries
static constexpr size_t   MAX_MINING_COINBASE_TAG_CHARS = 48;

static std::pair<double, std::string> FormatHashrate(double hashps)
{
    static const char* units[] = {"H/s", "kH/s", "MH/s", "GH/s", "TH/s", "PH/s"};
    int unit_index = 0;

    while (hashps >= 1000.0 && unit_index < 5) {
        hashps /= 1000.0;
        ++unit_index;
    }

    return {hashps, units[unit_index]};
}

static inline bool IsAllowedMiningCoinbaseTagChar(unsigned char c)
{
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9')) {
        return true;
    }

    switch (c) {
    case ' ':
    case '.':
    case '_':
    case '-':
    case ':':
    case '/':
    case '#':
        return true;
    default:
        return false;
    }
}

static std::vector<unsigned char> SanitizeMiningCoinbaseTag(const std::string& in)
{
    std::vector<unsigned char> out;
    out.reserve(std::min(in.size(), MAX_MINING_COINBASE_TAG_CHARS));

    for (unsigned char c : in) {
        if (!IsAllowedMiningCoinbaseTagChar(c)) continue;
        out.push_back(c);
        if (out.size() >= MAX_MINING_COINBASE_TAG_CHARS) break;
    }

    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back() == ' ') out.pop_back();

    return out;
}

static inline std::vector<unsigned char> GetMiningCoinbaseTagBytes()
{
    std::lock_guard<std::mutex> l(g_mining_cs);
    return SanitizeMiningCoinbaseTag(g_mining_coinbase_tag);
}

static inline void BuildMiningHeader80(const CBlockHeader& hdr, unsigned char header80[80])
{
    unsigned char* p = header80;

    // nVersion (4 bytes, LE)
    WriteLE32(p, static_cast<uint32_t>(hdr.nVersion));
    p += 4;

    // hashPrevBlock (32 bytes)
    std::memcpy(p, hdr.hashPrevBlock.begin(), 32);
    p += 32;

    // hashMerkleRoot (32 bytes)
    std::memcpy(p, hdr.hashMerkleRoot.begin(), 32);
    p += 32;

    // nTime (4 bytes, LE)
    WriteLE32(p, hdr.nTime);
    p += 4;

    // nBits (4 bytes, LE)
    WriteLE32(p, hdr.nBits);
    p += 4;

    // nNonce (4 bytes, LE)
    WriteLE32(p, hdr.nNonce);
}

static inline void PatchMiningHeaderTime(unsigned char header80[80], uint32_t nTime)
{
    WriteLE32(header80 + 68, nTime);
}

static inline void PatchMiningHeaderBits(unsigned char header80[80], uint32_t nBits)
{
    WriteLE32(header80 + 72, nBits);
}

static inline void PatchMiningHeaderNonce(unsigned char header80[80], uint32_t nNonce)
{
    WriteLE32(header80 + 76, nNonce);
}

// Miner-local exact mirror of consensus header hashing.
// This does NOT change consensus behavior; it only avoids rebuilding the 80-byte
// header object path on every hash attempt.
static inline uint256 MiningHashHeader80(const unsigned char header80[80])
{
    thread_local CWhirlpool512 hasher;

    unsigned char wh[WHIRLPOOL512_OUTPUT_SIZE];
    hasher.Reset().Write(header80, 80).Finalize(wh);

    unsigned char out32[32];
    for (int i = 0; i < 32; ++i) {
        out32[i] = wh[i] ^ wh[i + 32];
    }

    uint256 ret;
    std::memcpy(ret.begin(), out32, 32);
    return ret;
}

// Wider extranonce handling:
// - extra_nonce1: fixed per thread
// - extra_nonce2: rolling 64-bit counter
static void IncrementExtraNonce(CBlock* pblock,
                                const CBlockIndex* pindexPrev,
                                uint32_t extra_nonce1,
                                uint64_t& extra_nonce2,
                                const std::vector<unsigned char>& coinbase_tag)
{
    assert(pblock);
    assert(pindexPrev);

    ++extra_nonce2;

    const int nHeight = pindexPrev->nHeight + 1;

    CMutableTransaction txCoinbase(*pblock->vtx[0]);

    CScript scriptSig;
    scriptSig << nHeight;

    if (!coinbase_tag.empty()) {
        scriptSig << coinbase_tag;
    }

    scriptSig << CScriptNum(static_cast<int64_t>(extra_nonce1))
              << CScriptNum(static_cast<int64_t>(extra_nonce2 & 0xffffffffULL))
              << CScriptNum(static_cast<int64_t>((extra_nonce2 >> 32) & 0xffffffffULL));

    // If the tag pushed us over the consensus 100-byte coinbase scriptSig limit,
    // retry without the tag rather than risk mining invalid blocks.
    if (scriptSig.size() > 100) {
        scriptSig = CScript()
            << nHeight
            << CScriptNum(static_cast<int64_t>(extra_nonce1))
            << CScriptNum(static_cast<int64_t>(extra_nonce2 & 0xffffffffULL))
            << CScriptNum(static_cast<int64_t>((extra_nonce2 >> 32) & 0xffffffffULL));
    }

    assert(scriptSig.size() <= 100);

    txCoinbase.vin[0].scriptSig = scriptSig;

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

// Re-evaluate candidate time / bits in-place for the CURRENT template.
// Returns false if the resulting target is invalid.
static bool RefreshMiningCandidate(CBlock& block,
                                   const CBlockIndex* pindexPrev,
                                   const Consensus::Params& consensus,
                                   const arith_uint256& pow_limit,
                                   arith_uint256& target,
                                   unsigned char header80[80])
{
    bool fNeg = false;
    bool fOv = false;

    const uint32_t old_time = block.nTime;
    const uint32_t old_bits = block.nBits;

    UpdateTime(&block, consensus, pindexPrev);
    block.nBits = GetNextWorkRequired(pindexPrev, &block, consensus);

    target.SetCompact(block.nBits, &fNeg, &fOv);
    if (fNeg || fOv || target == 0 || target > pow_limit) {
        return false;
    }

    if (block.nTime != old_time) {
        PatchMiningHeaderTime(header80, block.nTime);
    }
    if (block.nBits != old_bits) {
        PatchMiningHeaderBits(header80, block.nBits);
    }

    return true;
}


/** Miner thread: repeatedly builds blocks and tries nonces with Whirlpool PoW */
static void MinerThread(int thread_id)
{
    NodeContext* node = nullptr;
    {
        std::lock_guard<std::mutex> l(g_mining_cs);
        node = g_mining_node;
    }
    if (!node) return;

    ChainstateManager& chainman = EnsureChainman(*node);
    const CTxMemPool& mempool   = EnsureMemPool(*node);
    const Consensus::Params& consensus = chainman.GetConsensus();
    const arith_uint256 pow_limit = UintToArith256(consensus.powLimit);

    const uint32_t extra_nonce1 = static_cast<uint32_t>(thread_id);
    uint64_t extra_nonce2 = 0;

    while (g_mining_active.load(std::memory_order_relaxed) && !chainman.m_interrupt) {
        // Build a fresh block template paying to g_mining_script
        std::unique_ptr<CBlockTemplate> pblocktemplate(
            BlockAssembler{chainman.ActiveChainstate(), &mempool}.CreateNewBlock(g_mining_script)
        );
        if (!pblocktemplate) {
            if (chainman.m_interrupt || !g_mining_active.load(std::memory_order_relaxed)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        CBlock block = pblocktemplate->block;

        // Previous block index for height / extranonce / commitments
        CBlockIndex* pindexPrev = chainman.ActiveChainstate().m_chain.Tip();
        if (!pindexPrev) {
            continue;
        }

        const uint64_t mempool_update_base =
            static_cast<uint64_t>(mempool.GetTransactionsUpdated());

        const std::vector<unsigned char> coinbase_tag = GetMiningCoinbaseTagBytes();

        // Bake extranonce into the coinbase and update merkle root
        IncrementExtraNonce(&block, pindexPrev, extra_nonce1, extra_nonce2, coinbase_tag);

        // Regenerate segwit/taproot commitments after touching coinbase
        RegenerateCommitments(block, chainman);

        // Refresh nTime/nBits from consensus work-selection logic up front
        UpdateTime(&block, consensus, pindexPrev);
        block.nBits = GetNextWorkRequired(pindexPrev, &block, consensus);

        // Compute target for this candidate
        arith_uint256 target;
        bool fNeg = false, fOv = false;
        target.SetCompact(block.nBits, &fNeg, &fOv);
        if (fNeg || fOv || target == 0 || target > pow_limit) {
            continue;
        }

        // Build canonical 80-byte header buffer once, then patch only the changing fields
        unsigned char header80[80];
        BuildMiningHeader80(block, header80);

        const int threads = std::max(1, g_mining_threads.load(std::memory_order_relaxed));
        uint32_t nonce = static_cast<uint32_t>(thread_id);

        uint64_t tries = 0;

        // Batch global hash counter updates to avoid one atomic op per hash
        uint64_t local_hashes_done = 0;

        // Track best local share as raw hash (smaller is better), only convert/publish occasionally
        arith_uint256 local_best_hash;
        bool have_local_best_hash = false;

        int64_t last_rate_sample_us =
            Ticks<std::chrono::microseconds>(GetTime<std::chrono::microseconds>());
        uint64_t last_rate_sample_hashes =
            g_hashes_done.load(std::memory_order_relaxed);

        while (g_mining_active.load(std::memory_order_relaxed) &&
               !chainman.m_interrupt) {

            // Event-driven / state-driven maintenance poll:
            // - tip change
            // - mempool update
            // - nTime / nBits transition
            // - hashrate flush / sample
            if ((tries & MINER_MAINTENANCE_POLL_MASK) == 0) {
                const CBlockIndex* tip_now = chainman.ActiveChainstate().m_chain.Tip();
                if (tip_now != pindexPrev) {
                    // Someone else extended the chain; rebuild on new tip
                    break;
                }

                const uint64_t mempool_update_now =
                    static_cast<uint64_t>(mempool.GetTransactionsUpdated());
                if (mempool_update_now != mempool_update_base) {
                    // Template tx set may have changed; rebuild on fresh mempool view
                    break;
                }

                const uint32_t old_bits = block.nBits;
                if (!RefreshMiningCandidate(block, pindexPrev, consensus, pow_limit, target, header80)) {
                    break;
                }

                // Flush local hash count in batches
                if (local_hashes_done != 0) {
                    g_hashes_done.fetch_add(local_hashes_done, std::memory_order_relaxed);
                    local_hashes_done = 0;
                }

                // Update displayed hashrate periodically (still time-based for display only)
                if (thread_id == 0) {
                    const int64_t now_us =
                        Ticks<std::chrono::microseconds>(GetTime<std::chrono::microseconds>());
                    const int64_t elapsed_us = now_us - last_rate_sample_us;

                    if (elapsed_us >= 500000) {
                        const uint64_t total_hashes =
                            g_hashes_done.load(std::memory_order_relaxed);
                        const uint64_t delta_hashes = total_hashes - last_rate_sample_hashes;

                        if (elapsed_us > 0) {
                            const double hashps =
                                (static_cast<double>(delta_hashes) * 1000000.0) /
                                static_cast<double>(elapsed_us);
                            g_local_hashps.store(hashps, std::memory_order_relaxed);
                        }

                        last_rate_sample_hashes = total_hashes;
                        last_rate_sample_us = now_us;
                    }
                }

                // Publish best share occasionally
                if (have_local_best_hash && local_best_hash > 0) {
                    const double diff_hit =
                        pow_limit.getdouble() / local_best_hash.getdouble();

                    double prev_best = g_best_diff_hit.load(std::memory_order_relaxed);
                    if (diff_hit > prev_best) {
                        g_best_diff_hit.store(diff_hit, std::memory_order_relaxed);
                    }
                }

                // If work class changed (e.g. emergency diff-1 became available),
                // rebuild from a fresh template immediately.
                if (block.nBits != old_bits) {
                    break;
                }
            }

            block.nNonce = nonce;
            PatchMiningHeaderNonce(header80, nonce);

            // Whirlpool-based PoW hash for this exact canonical 80-byte header
            const uint256 pow_hash = MiningHashHeader80(header80);
            const arith_uint256 hash_val = UintToArith256(pow_hash);

            // Track best local share using integer comparison only
            if (!have_local_best_hash || hash_val < local_best_hash) {
                local_best_hash = hash_val;
                have_local_best_hash = true;
            }

            if (hash_val <= target) {
                // Sanity: ensure the SAME thing consensus uses also passes the compact target check
                if (!CheckProofOfWork(pow_hash, block.nBits, consensus)) {
                    std::cout << "CapStash: local target hit but CheckProofOfWork(pow_hash) failed "
                              << "pow_hash=" << pow_hash.GetHex()
                              << " nBits=" << strprintf("%08x", block.nBits)
                              << "\n";
                    break;
                }

                // FINAL stale-tip check right before submit
                if (chainman.ActiveChainstate().m_chain.Tip() != pindexPrev) {
                    break;
                }

                // Preflight structural/context-free checks so we get a real reason if it’s bad
                BlockValidationState st;
                if (!CheckBlock(block, st, consensus, /*fCheckPOW=*/true, /*fCheckMerkleRoot=*/true)) {
                    std::cout << "CapStash: CheckBlock failed: " << st.ToString()
                              << " pow_hash=" << pow_hash.GetHex()
                              << " header_hash=" << block.GetHash().GetHex()
                              << " nBits=" << strprintf("%08x", block.nBits)
                              << " nTime=" << block.nTime
                              << "\n";
                    break;
                }

                std::shared_ptr<const CBlock> block_out = std::make_shared<const CBlock>(block);

                bool new_block{false};
                if (chainman.ProcessNewBlock(block_out,
                                             /*force_processing=*/true,
                                             /*min_pow_checked=*/true,
                                             &new_block)) {
                    g_local_blocks_found.fetch_add(1, std::memory_order_relaxed);

                    // Record *our* block (don’t touch chain tip here)
                    g_last_local_block_height.store(pindexPrev->nHeight + 1, std::memory_order_relaxed);
                    g_last_local_block_time.store(block.nTime, std::memory_order_relaxed);
                    {
                        std::lock_guard<std::mutex> l(g_last_local_block_hash_cs);
                        g_last_local_block_hash = block_out->GetHash();
                    }

                    // Publish the best local share seen by this thread before reset/rebuild
                    if (have_local_best_hash && local_best_hash > 0) {
                        const double diff_hit =
                            pow_limit.getdouble() / local_best_hash.getdouble();
                        double prev_best = g_best_diff_hit.load(std::memory_order_relaxed);
                        if (diff_hit > prev_best) {
                            g_best_diff_hit.store(diff_hit, std::memory_order_relaxed);
                        }
                    }

                    std::cout << "CapStash: mined block "
                              << block_out->GetHash().GetHex()
                              << " (thread " << thread_id << ")\n";
                } else {
                    std::cout << "CapStash: found block but ProcessNewBlock rejected it"
                              << " (new_block=" << (new_block ? "true" : "false") << ")\n";
                }

                // After solving (or rejected), rebuild a new template
                break;
            }

            ++tries;
            ++local_hashes_done;

            // Advance by thread stride and detect uint32 overflow correctly
            const uint32_t old_nonce = nonce;
            nonce += static_cast<uint32_t>(threads);

            if (nonce < old_nonce) {
                IncrementExtraNonce(&block, pindexPrev, extra_nonce1, extra_nonce2, coinbase_tag);
                RegenerateCommitments(block, chainman);

                // Refresh time / difficulty from consensus logic after coinbase/merkle change
                UpdateTime(&block, consensus, pindexPrev);
                block.nBits = GetNextWorkRequired(pindexPrev, &block, consensus);

                // Merkle root changed, so rebuild the exact canonical header buffer
                BuildMiningHeader80(block, header80);

                target.SetCompact(block.nBits, &fNeg, &fOv);
                if (fNeg || fOv || target == 0 || target > pow_limit) {
                    break;
                }
            }

            // Flush occasionally even if the maintenance poll above is delayed for any reason
            if ((local_hashes_done & 0xFFF) == 0 && local_hashes_done != 0) {
                g_hashes_done.fetch_add(local_hashes_done, std::memory_order_relaxed);
                local_hashes_done = 0;
            }
        }

        // Flush any remaining batched hashes before rebuilding/exiting
        if (local_hashes_done != 0) {
            g_hashes_done.fetch_add(local_hashes_done, std::memory_order_relaxed);
        }

        // Publish any best share remaining for this template
        if (have_local_best_hash && local_best_hash > 0) {
            const double diff_hit =
                pow_limit.getdouble() / local_best_hash.getdouble();
            double prev_best = g_best_diff_hit.load(std::memory_order_relaxed);
            if (diff_hit > prev_best) {
                g_best_diff_hit.store(diff_hit, std::memory_order_relaxed);
            }
        }

        if (!g_mining_active.load(std::memory_order_relaxed) || chainman.m_interrupt) {
            break;
        }
    }
}

static void StopMiningThreads()
{
    std::lock_guard<std::mutex> l(g_mining_cs);
    g_mining_active.store(false);
    g_mining_threads.store(0);
    g_hashes_done.store(0, std::memory_order_relaxed);
    g_local_hashps.store(0.0, std::memory_order_relaxed);
    g_best_diff_hit.store(0.0, std::memory_order_relaxed);
    g_local_blocks_found.store(0, std::memory_order_relaxed);
    g_mining_coinbase_tag.clear();
    // Threads are detached; they will see g_mining_active == false and exit.
    g_mining_node = nullptr;
}

static void StartMiningThreads(NodeContext& node, int num_threads, const CScript& script)
{
    if (num_threads == 0) num_threads = 1;

    // Stop any existing miner first, OUTSIDE the lock.
    if (g_mining_active.load(std::memory_order_relaxed)) {
        StopMiningThreads();
    }

    // If caller passed -1, auto-detect core count
    if (num_threads < 0) {
        unsigned int hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = 1;
        num_threads = static_cast<int>(hw);
    }

    {
        std::lock_guard<std::mutex> l(g_mining_cs);
        g_mining_node   = &node;
        g_mining_script = script;
        g_mining_threads.store(num_threads);
        g_mining_active.store(true);
        g_hashes_done.store(0, std::memory_order_relaxed);
        g_local_hashps.store(0.0, std::memory_order_relaxed);
        g_best_diff_hit.store(0.0, std::memory_order_relaxed);
        g_local_blocks_found.store(0, std::memory_order_relaxed);
    }

    for (int i = 0; i < num_threads; ++i) {
        std::thread(MinerThread, i).detach();
    }
}

/**
 * Return average network hashes per second based on the last 'lookup' blocks,
 * or from the last difficulty change if 'lookup' is -1.
 *
 * Fast-reacting version tuned for short-interval chains:
 * - Defaults should be small (e.g. 24 blocks)
 * - Uses GetBlockTime() for responsiveness (not MedianTimePast)
 */
static UniValue GetNetworkHashPS(int lookup, int height, const CChain& active_chain)
{
    if (lookup < -1 || lookup == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid nblocks. Must be a positive number or -1.");
    }

    if (height < -1 || height > active_chain.Height()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block does not exist at specified height");
    }

    const CBlockIndex* pb = active_chain.Tip();
    if (height >= 0) pb = active_chain[height];

    if (pb == nullptr || pb->nHeight == 0) return 0;

    // If lookup is -1, then use blocks since last difficulty change.
    if (lookup == -1) {
        lookup = pb->nHeight % Params().GetConsensus().DifficultyAdjustmentInterval() + 1;
    }

    // Clamp lookup to available height.
    lookup = std::min(lookup, pb->nHeight);

    const CBlockIndex* pb0 = pb;
    for (int i = 0; i < lookup && pb0->pprev; ++i) {
        pb0 = pb0->pprev;
    }

    // Responsive timing (not MTP). Clamp to avoid divide-by-zero.
    int64_t timeDiff = pb->GetBlockTime() - pb0->GetBlockTime();
    if (timeDiff <= 0) return 0;

    const arith_uint256 workDiff = pb->nChainWork - pb0->nChainWork;

    return workDiff.getdouble() / static_cast<double>(timeDiff);
}

static RPCHelpMan getnetworkhashps()
{
    return RPCHelpMan{
        "getnetworkhashps",
        "\nReturns the estimated network hashes per second based on the last n blocks.\n"
        "Pass in [blocks] to override # of blocks, -1 specifies since last difficulty change.\n"
        "Pass in [height] to estimate the network speed at the time when a certain block was found.\n",
        {
            // Default changed from 120 -> 24 for fast DA chains
            {"nblocks", RPCArg::Type::NUM, RPCArg::Default{24},
             "The number of previous blocks to calculate estimate from, or -1 for blocks since last difficulty change."},
            {"height", RPCArg::Type::NUM, RPCArg::Default{-1},
             "To estimate at the time of the given height."},
        },
        RPCResult{RPCResult::Type::NUM, "", "Hashes per second estimated"},
        RPCExamples{HelpExampleCli("getnetworkhashps", "") + HelpExampleRpc("getnetworkhashps", "")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue {
            ChainstateManager& chainman = EnsureAnyChainman(request.context);
            LOCK(cs_main);
            return GetNetworkHashPS(self.Arg<int>(0), self.Arg<int>(1), chainman.ActiveChain());
        },
    };
}




static bool GenerateBlock(ChainstateManager& chainman, CBlock& block, uint64_t& max_tries, std::shared_ptr<const CBlock>& block_out, bool process_new_block)
{
    block_out.reset();
    block.hashMerkleRoot = BlockMerkleRoot(block);

    while (max_tries > 0 && block.nNonce < std::numeric_limits<uint32_t>::max()
       && !CheckProofOfWork(block, chainman.GetConsensus())
       && !chainman.m_interrupt) {
        ++block.nNonce;
        --max_tries;
    }
    if (max_tries == 0 || chainman.m_interrupt) {
        return false;
    }
    if (block.nNonce == std::numeric_limits<uint32_t>::max()) {
        return true;
    }

    block_out = std::make_shared<const CBlock>(block);

    if (!process_new_block) return true;

    if (!chainman.ProcessNewBlock(block_out, /*force_processing=*/true, /*min_pow_checked=*/true, nullptr)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "ProcessNewBlock, block not accepted");
    }

    return true;
}

static UniValue generateBlocks(ChainstateManager& chainman, const CTxMemPool& mempool, const CScript& coinbase_script, int nGenerate, uint64_t nMaxTries)
{
    UniValue blockHashes(UniValue::VARR);
    while (nGenerate > 0 && !chainman.m_interrupt) {
        std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler{chainman.ActiveChainstate(), &mempool}.CreateNewBlock(coinbase_script));
        if (!pblocktemplate.get())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't create new block");

        std::shared_ptr<const CBlock> block_out;
        if (!GenerateBlock(chainman, pblocktemplate->block, nMaxTries, block_out, /*process_new_block=*/true)) {
            break;
        }

        if (block_out) {
            --nGenerate;
            blockHashes.push_back(block_out->GetHash().GetHex());
        }
    }
    return blockHashes;
}

static bool getScriptFromDescriptor(const std::string& descriptor, CScript& script, std::string& error)
{
    FlatSigningProvider key_provider;
    const auto desc = Parse(descriptor, key_provider, error, /* require_checksum = */ false);
    if (desc) {
        if (desc->IsRange()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Ranged descriptor not accepted. Maybe pass through deriveaddresses first?");
        }

        FlatSigningProvider provider;
        std::vector<CScript> scripts;
        if (!desc->Expand(0, key_provider, scripts, provider)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Cannot derive script without private keys");
        }

        // Combo descriptors can have 2 or 4 scripts, so we can't just check scripts.size() == 1
        CHECK_NONFATAL(scripts.size() > 0 && scripts.size() <= 4);

        if (scripts.size() == 1) {
            script = scripts.at(0);
        } else if (scripts.size() == 4) {
            // For uncompressed keys, take the 3rd script, since it is p2wpkh
            script = scripts.at(2);
        } else {
            // Else take the 2nd script, since it is p2pkh
            script = scripts.at(1);
        }

        return true;
    } else {
        return false;
    }
}

static RPCHelpMan generatetodescriptor()
{
    return RPCHelpMan{
        "generatetodescriptor",
        "Mine to a specified descriptor and return the block hashes.",
        {
            {"num_blocks", RPCArg::Type::NUM, RPCArg::Optional::NO, "How many blocks are generated."},
            {"descriptor", RPCArg::Type::STR, RPCArg::Optional::NO, "The descriptor to send the newly generated CapStash to."},
            {"maxtries", RPCArg::Type::NUM, RPCArg::Default{DEFAULT_MAX_TRIES}, "How many iterations to try."},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "hashes of blocks generated",
            {
                {RPCResult::Type::STR_HEX, "", "blockhash"},
            }
        },
        RPCExamples{
            "\nGenerate 11 blocks to mydesc\n" + HelpExampleCli("generatetodescriptor", "11 \"mydesc\"")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const auto num_blocks{self.Arg<int>(0)};
    const auto max_tries{self.Arg<uint64_t>(2)};

    CScript coinbase_script;
    std::string error;
    if (!getScriptFromDescriptor(self.Arg<std::string>(1), coinbase_script, error)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, error);
    }

    NodeContext& node = EnsureAnyNodeContext(request.context);
    const CTxMemPool& mempool = EnsureMemPool(node);
    ChainstateManager& chainman = EnsureChainman(node);

    return generateBlocks(chainman, mempool, coinbase_script, num_blocks, max_tries);
},
    };
}

static RPCHelpMan generate()
{
    return RPCHelpMan{"generate", "has been replaced by the -generate cli option. Refer to -help for more information.", {}, {}, RPCExamples{""}, [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue {
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, self.ToString());
    }};
}

static RPCHelpMan generatetoaddress()
{
    return RPCHelpMan{"generatetoaddress",
        "Mine to a specified address and return the block hashes.",
         {
             {"nblocks", RPCArg::Type::NUM, RPCArg::Optional::NO, "How many blocks are generated."},
             {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address to send the newly generated CapStash to."},
             {"maxtries", RPCArg::Type::NUM, RPCArg::Default{DEFAULT_MAX_TRIES}, "How many iterations to try."},
         },
         RPCResult{
             RPCResult::Type::ARR, "", "hashes of blocks generated",
             {
                 {RPCResult::Type::STR_HEX, "", "blockhash"},
             }},
         RPCExamples{
            "\nGenerate 11 blocks to myaddress\n"
            + HelpExampleCli("generatetoaddress", "11 \"myaddress\"")
            + "If you are using the " PACKAGE_NAME " wallet, you can get a new address to send the newly generated CapStash to with:\n"
            + HelpExampleCli("getnewaddress", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const int num_blocks{request.params[0].getInt<int>()};
    const uint64_t max_tries{request.params[2].isNull() ? DEFAULT_MAX_TRIES : request.params[2].getInt<int>()};

    CTxDestination destination = DecodeDestination(request.params[1].get_str());
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid address");
    }

    NodeContext& node = EnsureAnyNodeContext(request.context);
    const CTxMemPool& mempool = EnsureMemPool(node);
    ChainstateManager& chainman = EnsureChainman(node);

    CScript coinbase_script = GetScriptForDestination(destination);

    return generateBlocks(chainman, mempool, coinbase_script, num_blocks, max_tries);
},
    };
}

static RPCHelpMan generateblock()
{
    return RPCHelpMan{"generateblock",
        "Mine a set of ordered transactions to a specified address or descriptor and return the block hash.",
        {
            {"output", RPCArg::Type::STR, RPCArg::Optional::NO, "The address or descriptor to send the newly generated CapStash to."},
            {"transactions", RPCArg::Type::ARR, RPCArg::Optional::NO, "An array of hex strings which are either txids or raw transactions.\n"
                "Txids must reference transactions currently in the mempool.\n"
                "All transactions must be valid and in valid order, otherwise the block will be rejected.",
                {
                    {"rawtx/txid", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, ""},
                },
            },
            {"submit", RPCArg::Type::BOOL, RPCArg::Default{true}, "Whether to submit the block before the RPC call returns or to return it as hex."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "hash", "hash of generated block"},
                {RPCResult::Type::STR_HEX, "hex", /*optional=*/true, "hex of generated block, only present when submit=false"},
            }
        },
        RPCExamples{
            "\nGenerate a block to myaddress, with txs rawtx and mempool_txid\n"
            + HelpExampleCli("generateblock", R"("myaddress" '["rawtx", "mempool_txid"]')")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const auto address_or_descriptor = request.params[0].get_str();
    CScript coinbase_script;
    std::string error;

    if (!getScriptFromDescriptor(address_or_descriptor, coinbase_script, error)) {
        const auto destination = DecodeDestination(address_or_descriptor);
        if (!IsValidDestination(destination)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid address or descriptor");
        }

        coinbase_script = GetScriptForDestination(destination);
    }

    NodeContext& node = EnsureAnyNodeContext(request.context);
    const CTxMemPool& mempool = EnsureMemPool(node);

    std::vector<CTransactionRef> txs;
    const auto raw_txs_or_txids = request.params[1].get_array();
    for (size_t i = 0; i < raw_txs_or_txids.size(); i++) {
        const auto str(raw_txs_or_txids[i].get_str());

        uint256 hash;
        CMutableTransaction mtx;
        if (ParseHashStr(str, hash)) {

            const auto tx = mempool.get(hash);
            if (!tx) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Transaction %s not in mempool.", str));
            }

            txs.emplace_back(tx);

        } else if (DecodeHexTx(mtx, str)) {
            txs.push_back(MakeTransactionRef(std::move(mtx)));

        } else {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("Transaction decode failed for %s. Make sure the tx has at least one input.", str));
        }
    }

    const bool process_new_block{request.params[2].isNull() ? true : request.params[2].get_bool()};
    CBlock block;

    ChainstateManager& chainman = EnsureChainman(node);
    {
        LOCK(cs_main);

        std::unique_ptr<CBlockTemplate> blocktemplate(BlockAssembler{chainman.ActiveChainstate(), nullptr}.CreateNewBlock(coinbase_script));
        if (!blocktemplate) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't create new block");
        }
        block = blocktemplate->block;
    }

    CHECK_NONFATAL(block.vtx.size() == 1);

    // Add transactions
    block.vtx.insert(block.vtx.end(), txs.begin(), txs.end());
    RegenerateCommitments(block, chainman);

    {
        LOCK(cs_main);

        BlockValidationState state;
        if (!TestBlockValidity(state, chainman.GetParams(), chainman.ActiveChainstate(), block, chainman.m_blockman.LookupBlockIndex(block.hashPrevBlock), false, false)) {
            throw JSONRPCError(RPC_VERIFY_ERROR, strprintf("TestBlockValidity failed: %s", state.ToString()));
        }
    }

    std::shared_ptr<const CBlock> block_out;
    uint64_t max_tries{DEFAULT_MAX_TRIES};

    if (!GenerateBlock(chainman, block, max_tries, block_out, process_new_block) || !block_out) {
        throw JSONRPCError(RPC_MISC_ERROR, "Failed to make block.");
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("hash", block_out->GetHash().GetHex());
    if (!process_new_block) {
        DataStream block_ser;
        block_ser << TX_WITH_WITNESS(*block_out);
        obj.pushKV("hex", HexStr(block_ser));
    }
    return obj;
},
    };
}

static RPCHelpMan getmininginfo()
{
    return RPCHelpMan{"getmininginfo",
                "\nReturns a json object containing mining-related information.",
                {},
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::NUM,  "blocks", "The current block"},
                        {RPCResult::Type::NUM,  "currentblockweight", /*optional=*/true, "The block weight of the last assembled block (only present if a block was ever assembled)"},
                        {RPCResult::Type::NUM,  "currentblocktx", /*optional=*/true, "The number of block transactions of the last assembled block (only present if a block was ever assembled)"},
                        {RPCResult::Type::NUM,  "difficulty", "The current difficulty"},
                        {RPCResult::Type::NUM,  "networkhashps", "The network hashes per second"},
                        {RPCResult::Type::BOOL, "mining", "True if the built-in CPU miner is active"},
                        {RPCResult::Type::NUM,  "threads", "Number of built-in miner threads"},
                        {RPCResult::Type::NUM,  "localhashps", "Approximate local miner hashes per second"},
                        {RPCResult::Type::NUM,  "bestlocaldiffhit", /*optional=*/true, "Highest difficulty hit during this mining session"},
                        {RPCResult::Type::NUM,  "localhashrate", "Human-scaled local miner hashrate value"},
                        {RPCResult::Type::STR,  "localhashunit", "Human-scaled local miner hashrate unit"},
                        {RPCResult::Type::NUM,  "lastlocalblocktime", /*optional=*/true, "Unix time of the most recently locally mined block"},
                        {RPCResult::Type::NUM,  "lastlocalblockheight", /*optional=*/true, "Height of the most recently locally mined block"},
                        {RPCResult::Type::STR,  "lastlocalblockhash", /*optional=*/true, "Hash of the most recently locally mined block"},
                        {RPCResult::Type::NUM,  "pooledtx", "The size of the mempool"},
                        {RPCResult::Type::STR,  "chain", "Current network name (main, test, signet, regtest)"},
                        {RPCResult::Type::STR,  "warnings", "Any network and blockchain warnings"},
                    }},
                RPCExamples{
                    HelpExampleCli("getmininginfo", "")
            + HelpExampleRpc("getmininginfo", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    NodeContext& node = EnsureAnyNodeContext(request.context);
    const CTxMemPool& mempool = EnsureMemPool(node);
    ChainstateManager& chainman = EnsureChainman(node);
    LOCK(cs_main);
    const CChain& active_chain = chainman.ActiveChain();

    const double local_hashps = g_local_hashps.load(std::memory_order_relaxed);
    const auto [scaled_rate, scaled_unit] = FormatHashrate(local_hashps);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("blocks", active_chain.Height());
    if (BlockAssembler::m_last_block_weight) obj.pushKV("currentblockweight", *BlockAssembler::m_last_block_weight);
    if (BlockAssembler::m_last_block_num_txs) obj.pushKV("currentblocktx", *BlockAssembler::m_last_block_num_txs);
    obj.pushKV("difficulty", active_chain.Tip() ? GetDifficulty(*active_chain.Tip()) : 1.0);
    obj.pushKV("networkhashps", getnetworkhashps().HandleRequest(request));

    obj.pushKV("mining", g_mining_active.load());
    obj.pushKV("threads", g_mining_threads.load());
    obj.pushKV("localhashps", local_hashps);
    obj.pushKV("localhashrate", scaled_rate);
    obj.pushKV("localhashunit", scaled_unit);
    obj.pushKV("localblocksfound", static_cast<uint64_t>(g_local_blocks_found.load(std::memory_order_relaxed)));
    obj.pushKV("bestlocaldiffhit", g_best_diff_hit.load(std::memory_order_relaxed));

    const int64_t last_local_block_time = g_last_local_block_time.load(std::memory_order_relaxed);
    const int last_local_block_height = g_last_local_block_height.load(std::memory_order_relaxed);

    if (last_local_block_time > 0) {
    obj.pushKV("lastlocalblocktime", last_local_block_time);
    }
    if (last_local_block_height > 0) {
    obj.pushKV("lastlocalblockheight", last_local_block_height);
    }
    {
    std::lock_guard<std::mutex> l(g_last_local_block_hash_cs);
    if (!g_last_local_block_hash.IsNull()) {
        obj.pushKV("lastlocalblockhash", g_last_local_block_hash.GetHex());
    }
    }
    obj.pushKV("pooledtx", (uint64_t)mempool.size());
    obj.pushKV("chain", chainman.GetParams().GetChainTypeString());
    obj.pushKV("warnings", GetWarnings(false).original);
    return obj;
},
    };
}

static RPCHelpMan setgenerate()
{
    return RPCHelpMan{
        "setgenerate",
        "Start or stop the built-in CPU miner.\n"
        "\nWhen starting mining, you must provide an address to mine to.\n"
        "You may also optionally provide a custom coinbase tag / 'Mined By' string.\n"
        "\nExamples:\n"
        "  setgenerate true 6 \"CapStashAddressHere\"                      -> start mining with 6 threads\n"
        "  setgenerate true 6 \"CapStashAddressHere\" \"Vault 1137 Overseer\" -> start mining with a custom tag\n"
        "  setgenerate false                                              -> stop mining\n",
        {
            {"generate", RPCArg::Type::BOOL, RPCArg::Optional::NO,
                "Set to true to start mining, false to stop mining."},
            {"genproclimit", RPCArg::Type::NUM, RPCArg::Default{-1},
                "Number of miner threads. -1 = use all cores."},
            {"address", RPCArg::Type::STR, RPCArg::Default{""},
                "Address to send the newly generated CapStash to (required when starting)."},
            {"coinbase_tag", RPCArg::Type::STR, RPCArg::Default{""},
                "Optional custom coinbase tag / 'Mined By' string. "
                "Allowed characters are letters, numbers, space, '.', '_', '-', ':', '/', '#'. "
                "The tag is sanitized and capped before being inserted into scriptSig."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "mining",  "true if CPU miner is running"},
                {RPCResult::Type::NUM,  "threads", "number of miner threads"},
                {RPCResult::Type::STR,  "address", "current mining address (if any)"},
                {RPCResult::Type::STR,  "coinbase_tag", "current sanitized mining coinbase tag (if any)"},
            }
        },
        RPCExamples{
            HelpExampleCli("setgenerate", "true 4 \"CapStashAddressHere\"") +
            HelpExampleCli("setgenerate", "true 4 \"CapStashAddressHere\" \"Vault 1137 Overseer\"") +
            HelpExampleCli("setgenerate", "false") +
            HelpExampleRpc("setgenerate", "true, 4, \"CapStashAddressHere\", \"Vault 1137 Overseer\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            const bool generate = request.params[0].get_bool();

            int genproclimit = -1;
            if (!request.params[1].isNull()) {
                genproclimit = request.params[1].getInt<int>();
            }

            const std::string address = request.params[2].isNull()
                                        ? std::string()
                                        : request.params[2].get_str();

            const std::string raw_coinbase_tag = request.params[3].isNull()
                                                 ? std::string()
                                                 : request.params[3].get_str();

            NodeContext& node = EnsureAnyNodeContext(request.context);

            if (!generate) {
                StopMiningThreads();

                {
                    std::lock_guard<std::mutex> l(g_mining_cs);
                    g_mining_coinbase_tag.clear();
                }
            } else {
                if (address.empty()) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER,
                        "When starting mining, 'address' must be provided.");
                }

                CTxDestination dest = DecodeDestination(address);
                if (!IsValidDestination(dest)) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
                }

                const std::vector<unsigned char> sanitized_tag =
                    SanitizeMiningCoinbaseTag(raw_coinbase_tag);

                if (!raw_coinbase_tag.empty() && sanitized_tag.empty()) {
                    throw JSONRPCError(
                        RPC_INVALID_PARAMETER,
                        "coinbase_tag contained no allowed characters after sanitization.");
                }

                CScript coinbase_script = GetScriptForDestination(dest);

                {
                    std::lock_guard<std::mutex> l(g_mining_cs);
                    g_mining_coinbase_tag.assign(sanitized_tag.begin(), sanitized_tag.end());
                }

                // Start threads (StartMiningThreads handles genproclimit == -1)
                StartMiningThreads(node, genproclimit, coinbase_script);
            }

            std::string current_tag;
            {
                std::lock_guard<std::mutex> l(g_mining_cs);
                current_tag = g_mining_coinbase_tag;
            }

            UniValue obj(UniValue::VOBJ);
            obj.pushKV("mining", g_mining_active.load());
            obj.pushKV("threads", g_mining_threads.load());
            obj.pushKV("address", generate ? address : "");
            obj.pushKV("coinbase_tag", current_tag);
            return obj;
        },
    };
}

// NOTE: Unlike wallet RPC (which use Cap values), mining RPCs follow GBT (BIP 22) in using satoshi amounts
static RPCHelpMan prioritisetransaction()
{
    return RPCHelpMan{"prioritisetransaction",
                "Accepts the transaction into mined blocks at a higher (or lower) priority\n",
                {
                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id."},
                    {"dummy", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "API-Compatibility for previous API. Must be zero or null.\n"
            "                  DEPRECATED. For forward compatibility use named arguments and omit this parameter."},
                    {"fee_delta", RPCArg::Type::NUM, RPCArg::Optional::NO, "The fee value (in satoshis) to add (or subtract, if negative).\n"
            "                  Note, that this value is not a fee rate. It is a value to modify absolute fee of the TX.\n"
            "                  The fee is not actually paid, only the algorithm for selecting transactions into a block\n"
            "                  considers the transaction as it would have paid a higher (or lower) fee."},
                },
                RPCResult{
                    RPCResult::Type::BOOL, "", "Returns true"},
                RPCExamples{
                    HelpExampleCli("prioritisetransaction", "\"txid\" 0.0 10000")
            + HelpExampleRpc("prioritisetransaction", "\"txid\", 0.0, 10000")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    LOCK(cs_main);

    uint256 hash(ParseHashV(request.params[0], "txid"));
    const auto dummy{self.MaybeArg<double>(1)};
    CAmount nAmount = request.params[2].getInt<int64_t>();

    if (dummy && *dummy != 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Priority is no longer supported, dummy argument to prioritisetransaction must be 0.");
    }

    EnsureAnyMemPool(request.context).PrioritiseTransaction(hash, nAmount);
    return true;
},
    };
}

static RPCHelpMan getprioritisedtransactions()
{
    return RPCHelpMan{"getprioritisedtransactions",
        "Returns a map of all user-created (see prioritisetransaction) fee deltas by txid, and whether the tx is present in mempool.",
        {},
        RPCResult{
            RPCResult::Type::OBJ_DYN, "", "prioritisation keyed by txid",
            {
                {RPCResult::Type::OBJ, "<transactionid>", "", {
                    {RPCResult::Type::NUM, "fee_delta", "transaction fee delta in satoshis"},
                    {RPCResult::Type::BOOL, "in_mempool", "whether this transaction is currently in mempool"},
                    {RPCResult::Type::NUM, "modified_fee", /*optional=*/true, "modified fee in satoshis. Only returned if in_mempool=true"},
                }}
            },
        },
        RPCExamples{
            HelpExampleCli("getprioritisedtransactions", "")
            + HelpExampleRpc("getprioritisedtransactions", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            NodeContext& node = EnsureAnyNodeContext(request.context);
            CTxMemPool& mempool = EnsureMemPool(node);
            UniValue rpc_result{UniValue::VOBJ};
            for (const auto& delta_info : mempool.GetPrioritisedTransactions()) {
                UniValue result_inner{UniValue::VOBJ};
                result_inner.pushKV("fee_delta", delta_info.delta);
                result_inner.pushKV("in_mempool", delta_info.in_mempool);
                if (delta_info.in_mempool) {
                    result_inner.pushKV("modified_fee", *delta_info.modified_fee);
                }
                rpc_result.pushKV(delta_info.txid.GetHex(), result_inner);
            }
            return rpc_result;
        },
    };
}


// NOTE: Assumes a conclusive result; if result is inconclusive, it must be handled by caller
static UniValue BIP22ValidationResult(const BlockValidationState& state)
{
    if (state.IsValid())
        return UniValue::VNULL;

    if (state.IsError())
        throw JSONRPCError(RPC_VERIFY_ERROR, state.ToString());
    if (state.IsInvalid())
    {
        std::string strRejectReason = state.GetRejectReason();
        if (strRejectReason.empty())
            return "rejected";
        return strRejectReason;
    }
    // Should be impossible
    return "valid?";
}

static std::string gbt_vb_name(const Consensus::DeploymentPos pos) {
    const struct VBDeploymentInfo& vbinfo = VersionBitsDeploymentInfo[pos];
    std::string s = vbinfo.name;
    if (!vbinfo.gbt_force) {
        s.insert(s.begin(), '!');
    }
    return s;
}

static RPCHelpMan getblocktemplate()
{
    return RPCHelpMan{"getblocktemplate",
        "\nIf the request parameters include a 'mode' key, that is used to explicitly select between the default 'template' request or a 'proposal'.\n"
        "It returns data needed to construct a block to work on.\n"
        "For full specification, see BIPs 22, 23, 9, and 145:\n"
        "    https://github.com/CapStash/bips/blob/master/bip-0022.mediawiki\n"
        "    https://github.com/CapStash/bips/blob/master/bip-0023.mediawiki\n"
        "    https://github.com/CapStash/bips/blob/master/bip-0009.mediawiki#getblocktemplate_changes\n"
        "    https://github.com/CapStash/bips/blob/master/bip-0145.mediawiki\n",
        {
            {"template_request", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Format of the template",
            {
                {"mode", RPCArg::Type::STR, /* treat as named arg */ RPCArg::Optional::OMITTED, "This must be set to \"template\", \"proposal\" (see BIP 23), or omitted"},
                {"capabilities", RPCArg::Type::ARR, /* treat as named arg */ RPCArg::Optional::OMITTED, "A list of strings",
                {
                    {"str", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "client side supported feature, 'longpoll', 'coinbasevalue', 'proposal', 'serverlist', 'workid'"},
                }},
                {"rules", RPCArg::Type::ARR, RPCArg::Optional::NO, "A list of strings",
                {
                    {"segwit", RPCArg::Type::STR, RPCArg::Optional::NO, "(literal) indicates client side segwit support"},
                    {"str", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "other client side supported softfork deployment"},
                }},
                {"longpollid", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "delay processing request until the result would vary significantly from the \"longpollid\" of a prior template"},
                {"data", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "proposed block data to check, encoded in hexadecimal; valid only for mode=\"proposal\""},
            },
            },
        },
        {
            RPCResult{"If the proposal was accepted with mode=='proposal'", RPCResult::Type::NONE, "", ""},
            RPCResult{"If the proposal was not accepted with mode=='proposal'", RPCResult::Type::STR, "", "According to BIP22"},
            RPCResult{"Otherwise", RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::NUM, "version", "The preferred block version"},
                {RPCResult::Type::ARR, "rules", "specific block rules that are to be enforced",
                {
                    {RPCResult::Type::STR, "", "name of a rule the client must understand to some extent; see BIP 9 for format"},
                }},
                {RPCResult::Type::OBJ_DYN, "vbavailable", "set of pending, supported versionbit (BIP 9) softfork deployments",
                {
                    {RPCResult::Type::NUM, "rulename", "identifies the bit number as indicating acceptance and readiness for the named softfork rule"},
                }},
                {RPCResult::Type::ARR, "capabilities", "",
                {
                    {RPCResult::Type::STR, "value", "A supported feature, for example 'proposal'"},
                }},
                {RPCResult::Type::NUM, "vbrequired", "bit mask of versionbits the server requires set in submissions"},
                {RPCResult::Type::STR, "previousblockhash", "The hash of current highest block"},
                {RPCResult::Type::ARR, "transactions", "contents of non-coinbase transactions that should be included in the next block",
                {
                    {RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR_HEX, "data", "transaction data encoded in hexadecimal (byte-for-byte)"},
                        {RPCResult::Type::STR_HEX, "txid", "transaction id encoded in little-endian hexadecimal"},
                        {RPCResult::Type::STR_HEX, "hash", "hash encoded in little-endian hexadecimal (including witness data)"},
                        {RPCResult::Type::ARR, "depends", "array of numbers",
                        {
                            {RPCResult::Type::NUM, "", "transactions before this one (by 1-based index in 'transactions' list) that must be present in the final block if this one is"},
                        }},
                        {RPCResult::Type::NUM, "fee", "difference in value between transaction inputs and outputs (in satoshis); for coinbase transactions, this is a negative Number of the total collected block fees (ie, not including the block subsidy); if key is not present, fee is unknown and clients MUST NOT assume there isn't one"},
                        {RPCResult::Type::NUM, "sigops", "total SigOps cost, as counted for purposes of block limits; if key is not present, sigop cost is unknown and clients MUST NOT assume it is zero"},
                        {RPCResult::Type::NUM, "weight", "total transaction weight, as counted for purposes of block limits"},
                    }},
                }},
                {RPCResult::Type::OBJ_DYN, "coinbaseaux", "data that should be included in the coinbase's scriptSig content",
                {
                    {RPCResult::Type::STR_HEX, "key", "values must be in the coinbase (keys may be ignored)"},
                }},
                {RPCResult::Type::NUM, "coinbasevalue", "maximum allowable input to coinbase transaction, including the generation award and transaction fees (in satoshis)"},
                {RPCResult::Type::STR, "longpollid", "an id to include with a request to longpoll on an update to this template"},
                {RPCResult::Type::STR, "target", "The hash target"},
                {RPCResult::Type::NUM_TIME, "mintime", "The minimum timestamp appropriate for the next block time, expressed in " + UNIX_EPOCH_TIME},
                {RPCResult::Type::ARR, "mutable", "list of ways the block template may be changed",
                {
                    {RPCResult::Type::STR, "value", "A way the block template may be changed, e.g. 'time', 'transactions', 'prevblock'"},
                }},
                {RPCResult::Type::STR_HEX, "noncerange", "A range of valid nonces"},
                {RPCResult::Type::NUM, "sigoplimit", "limit of sigops in blocks"},
                {RPCResult::Type::NUM, "sizelimit", "limit of block size"},
                {RPCResult::Type::NUM, "weightlimit", /*optional=*/true, "limit of block weight"},
                {RPCResult::Type::NUM_TIME, "curtime", "current timestamp in " + UNIX_EPOCH_TIME},
                {RPCResult::Type::STR, "bits", "compressed target of next block"},
                {RPCResult::Type::NUM, "height", "The height of the next block"},
                {RPCResult::Type::STR_HEX, "signet_challenge", /*optional=*/true, "Only on signet"},
                {RPCResult::Type::STR_HEX, "default_witness_commitment", /*optional=*/true, "a valid witness commitment for the unmodified block template"},
            }},
        },
        RPCExamples{
                    HelpExampleCli("getblocktemplate", "'{\"rules\": [\"segwit\"]}'")
            + HelpExampleRpc("getblocktemplate", "{\"rules\": [\"segwit\"]}")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    NodeContext& node = EnsureAnyNodeContext(request.context);
    ChainstateManager& chainman = EnsureChainman(node);
    LOCK(cs_main);

    std::string strMode = "template";
    UniValue lpval = NullUniValue;
    std::set<std::string> setClientRules;
    Chainstate& active_chainstate = chainman.ActiveChainstate();
    CChain& active_chain = active_chainstate.m_chain;
    if (!request.params[0].isNull())
    {
        const UniValue& oparam = request.params[0].get_obj();
        const UniValue& modeval = oparam.find_value("mode");
        if (modeval.isStr())
            strMode = modeval.get_str();
        else if (modeval.isNull())
        {
            /* Do nothing */
        }
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
        lpval = oparam.find_value("longpollid");

        if (strMode == "proposal")
        {
            const UniValue& dataval = oparam.find_value("data");
            if (!dataval.isStr())
                throw JSONRPCError(RPC_TYPE_ERROR, "Missing data String key for proposal");

            CBlock block;
            if (!DecodeHexBlk(block, dataval.get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

            uint256 hash = block.GetHash();
            const CBlockIndex* pindex = chainman.m_blockman.LookupBlockIndex(hash);
            if (pindex) {
                if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                    return "duplicate";
                if (pindex->nStatus & BLOCK_FAILED_MASK)
                    return "duplicate-invalid";
                return "duplicate-inconclusive";
            }

            CBlockIndex* const pindexPrev = active_chain.Tip();
            // TestBlockValidity only supports blocks built on the current Tip
            if (block.hashPrevBlock != pindexPrev->GetBlockHash())
                return "inconclusive-not-best-prevblk";
            BlockValidationState state;
            TestBlockValidity(state, chainman.GetParams(), active_chainstate, block, pindexPrev, false, true);
            return BIP22ValidationResult(state);
        }

        const UniValue& aClientRules = oparam.find_value("rules");
        if (aClientRules.isArray()) {
            for (unsigned int i = 0; i < aClientRules.size(); ++i) {
                const UniValue& v = aClientRules[i];
                setClientRules.insert(v.get_str());
            }
        }
    }

    if (strMode != "template")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");

    if (!chainman.GetParams().IsTestChain()) {
        const CConnman& connman = EnsureConnman(node);
        if (connman.GetNodeCount(ConnectionDirection::Both) == 0) {
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, PACKAGE_NAME " is not connected!");
        }

        if (chainman.IsInitialBlockDownload()) {
            throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, PACKAGE_NAME " is in initial sync and waiting for blocks...");
        }
    }

    static unsigned int nTransactionsUpdatedLast;
    const CTxMemPool& mempool = EnsureMemPool(node);

    if (!lpval.isNull())
    {
        // Wait to respond until either the best block changes, OR a minute has passed and there are more transactions
        uint256 hashWatchedChain;
        std::chrono::steady_clock::time_point checktxtime;
        unsigned int nTransactionsUpdatedLastLP;

        if (lpval.isStr())
        {
            // Format: <hashBestChain><nTransactionsUpdatedLast>
            const std::string& lpstr = lpval.get_str();

            hashWatchedChain = ParseHashV(lpstr.substr(0, 64), "longpollid");
            nTransactionsUpdatedLastLP = LocaleIndependentAtoi<int64_t>(lpstr.substr(64));
        }
        else
        {
            // NOTE: Spec does not specify behaviour for non-string longpollid, but this makes testing easier
            hashWatchedChain = active_chain.Tip()->GetBlockHash();
            nTransactionsUpdatedLastLP = nTransactionsUpdatedLast;
        }

        // Release lock while waiting
        LEAVE_CRITICAL_SECTION(cs_main);
        {
            checktxtime = std::chrono::steady_clock::now() + std::chrono::minutes(1);

            WAIT_LOCK(g_best_block_mutex, lock);
            while (g_best_block == hashWatchedChain && IsRPCRunning())
            {
                if (g_best_block_cv.wait_until(lock, checktxtime) == std::cv_status::timeout)
                {
                    // Timeout: Check transactions for update
                    // without holding the mempool lock to avoid deadlocks
                    if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLastLP)
                        break;
                    checktxtime += std::chrono::seconds(10);
                }
            }
        }
        ENTER_CRITICAL_SECTION(cs_main);

        if (!IsRPCRunning())
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Shutting down");
        // TODO: Maybe recheck connections/IBD and (if something wrong) send an expires-immediately template to stop miners?
    }

    const Consensus::Params& consensusParams = chainman.GetParams().GetConsensus();

    // GBT must be called with 'signet' set in the rules for signet chains
    if (consensusParams.signet_blocks && setClientRules.count("signet") != 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "getblocktemplate must be called with the signet rule set (call with {\"rules\": [\"segwit\", \"signet\"]})");
    }

    // GBT must be called with 'segwit' set in the rules
    if (setClientRules.count("segwit") != 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "getblocktemplate must be called with the segwit rule set (call with {\"rules\": [\"segwit\"]})");
    }

    // Update block
    static CBlockIndex* pindexPrev;
    static int64_t time_start;
    static std::unique_ptr<CBlockTemplate> pblocktemplate;
    if (pindexPrev != active_chain.Tip() ||
        (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - time_start > 5))
    {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = nullptr;

        // Store the pindexBest used before CreateNewBlock, to avoid races
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex* pindexPrevNew = active_chain.Tip();
        time_start = GetTime();

        // Create new block
        CScript scriptDummy = CScript() << OP_TRUE;
        pblocktemplate = BlockAssembler{active_chainstate, &mempool}.CreateNewBlock(scriptDummy);
        if (!pblocktemplate)
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew;
    }
    CHECK_NONFATAL(pindexPrev);
    CBlock* pblock = &pblocktemplate->block; // pointer for convenience

    // Update nTime
    UpdateTime(pblock, consensusParams, pindexPrev);
    pblock->nNonce = 0;

    // NOTE: If at some point we support pre-segwit miners post-segwit-activation, this needs to take segwit support into consideration
    const bool fPreSegWit = !DeploymentActiveAfter(pindexPrev, chainman, Consensus::DEPLOYMENT_SEGWIT);

    UniValue aCaps(UniValue::VARR); aCaps.push_back("proposal");

    UniValue transactions(UniValue::VARR);
    std::map<uint256, int64_t> setTxIndex;
    int i = 0;
    for (const auto& it : pblock->vtx) {
        const CTransaction& tx = *it;
        uint256 txHash = tx.GetHash();
        setTxIndex[txHash] = i++;

        if (tx.IsCoinBase())
            continue;

        UniValue entry(UniValue::VOBJ);

        entry.pushKV("data", EncodeHexTx(tx));
        entry.pushKV("txid", txHash.GetHex());
        entry.pushKV("hash", tx.GetWitnessHash().GetHex());

        UniValue deps(UniValue::VARR);
        for (const CTxIn &in : tx.vin)
        {
            if (setTxIndex.count(in.prevout.hash))
                deps.push_back(setTxIndex[in.prevout.hash]);
        }
        entry.pushKV("depends", deps);

        int index_in_template = i - 1;
        entry.pushKV("fee", pblocktemplate->vTxFees[index_in_template]);
        int64_t nTxSigOps = pblocktemplate->vTxSigOpsCost[index_in_template];
        if (fPreSegWit) {
            CHECK_NONFATAL(nTxSigOps % WITNESS_SCALE_FACTOR == 0);
            nTxSigOps /= WITNESS_SCALE_FACTOR;
        }
        entry.pushKV("sigops", nTxSigOps);
        entry.pushKV("weight", GetTransactionWeight(tx));

        transactions.push_back(entry);
    }

    UniValue aux(UniValue::VOBJ);

    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);

    UniValue aMutable(UniValue::VARR);
    aMutable.push_back("time");
    aMutable.push_back("transactions");
    aMutable.push_back("prevblock");

    UniValue result(UniValue::VOBJ);
    result.pushKV("capabilities", aCaps);

    UniValue aRules(UniValue::VARR);
    aRules.push_back("csv");
    if (!fPreSegWit) aRules.push_back("!segwit");
    if (consensusParams.signet_blocks) {
        // indicate to miner that they must understand signet rules
        // when attempting to mine with this template
        aRules.push_back("!signet");
    }

    UniValue vbavailable(UniValue::VOBJ);
    for (int j = 0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
        Consensus::DeploymentPos pos = Consensus::DeploymentPos(j);
        ThresholdState state = chainman.m_versionbitscache.State(pindexPrev, consensusParams, pos);
        switch (state) {
            case ThresholdState::DEFINED:
            case ThresholdState::FAILED:
                // Not exposed to GBT at all
                break;
            case ThresholdState::LOCKED_IN:
                // Ensure bit is set in block version
                pblock->nVersion |= chainman.m_versionbitscache.Mask(consensusParams, pos);
                [[fallthrough]];
            case ThresholdState::STARTED:
            {
                const struct VBDeploymentInfo& vbinfo = VersionBitsDeploymentInfo[pos];
                vbavailable.pushKV(gbt_vb_name(pos), consensusParams.vDeployments[pos].bit);
                if (setClientRules.find(vbinfo.name) == setClientRules.end()) {
                    if (!vbinfo.gbt_force) {
                        // If the client doesn't support this, don't indicate it in the [default] version
                        pblock->nVersion &= ~chainman.m_versionbitscache.Mask(consensusParams, pos);
                    }
                }
                break;
            }
            case ThresholdState::ACTIVE:
            {
                // Add to rules only
                const struct VBDeploymentInfo& vbinfo = VersionBitsDeploymentInfo[pos];
                aRules.push_back(gbt_vb_name(pos));
                if (setClientRules.find(vbinfo.name) == setClientRules.end()) {
                    // Not supported by the client; make sure it's safe to proceed
                    if (!vbinfo.gbt_force) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Support for '%s' rule requires explicit client support", vbinfo.name));
                    }
                }
                break;
            }
        }
    }
    result.pushKV("version", pblock->nVersion);
    result.pushKV("rules", aRules);
    result.pushKV("vbavailable", vbavailable);
    result.pushKV("vbrequired", int(0));

    result.pushKV("previousblockhash", pblock->hashPrevBlock.GetHex());
    result.pushKV("transactions", transactions);
    result.pushKV("coinbaseaux", aux);
    result.pushKV("coinbasevalue", (int64_t)pblock->vtx[0]->vout[0].nValue);
    result.pushKV("longpollid", active_chain.Tip()->GetBlockHash().GetHex() + ToString(nTransactionsUpdatedLast));
    result.pushKV("target", hashTarget.GetHex());
    result.pushKV("mintime", (int64_t)pindexPrev->GetMedianTimePast()+1);
    result.pushKV("mutable", aMutable);
    result.pushKV("noncerange", "00000000ffffffff");
    int64_t nSigOpLimit = MAX_BLOCK_SIGOPS_COST;
    int64_t nSizeLimit = MAX_BLOCK_SERIALIZED_SIZE;
    if (fPreSegWit) {
        CHECK_NONFATAL(nSigOpLimit % WITNESS_SCALE_FACTOR == 0);
        nSigOpLimit /= WITNESS_SCALE_FACTOR;
        CHECK_NONFATAL(nSizeLimit % WITNESS_SCALE_FACTOR == 0);
        nSizeLimit /= WITNESS_SCALE_FACTOR;
    }
    result.pushKV("sigoplimit", nSigOpLimit);
    result.pushKV("sizelimit", nSizeLimit);
    if (!fPreSegWit) {
        result.pushKV("weightlimit", (int64_t)MAX_BLOCK_WEIGHT);
    }
    result.pushKV("curtime", pblock->GetBlockTime());
    result.pushKV("bits", strprintf("%08x", pblock->nBits));
    result.pushKV("height", (int64_t)(pindexPrev->nHeight+1));

    if (consensusParams.signet_blocks) {
        result.pushKV("signet_challenge", HexStr(consensusParams.signet_challenge));
    }

    if (!pblocktemplate->vchCoinbaseCommitment.empty()) {
        result.pushKV("default_witness_commitment", HexStr(pblocktemplate->vchCoinbaseCommitment));
    }

    return result;
},
    };
}

class submitblock_StateCatcher final : public CValidationInterface
{
public:
    uint256 hash;
    bool found{false};
    BlockValidationState state;

    explicit submitblock_StateCatcher(const uint256 &hashIn) : hash(hashIn), state() {}

protected:
    void BlockChecked(const CBlock& block, const BlockValidationState& stateIn) override {
        if (block.GetHash() != hash)
            return;
        found = true;
        state = stateIn;
    }
};

static RPCHelpMan submitblock()
{
    // We allow 2 arguments for compliance with BIP22. Argument 2 is ignored.
    return RPCHelpMan{"submitblock",
        "\nAttempts to submit new block to network.\n"
        "See https://en.CapStash.it/wiki/BIP_0022 for full specification.\n",
        {
            {"hexdata", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "the hex-encoded block data to submit"},
            {"dummy", RPCArg::Type::STR, RPCArg::DefaultHint{"ignored"}, "dummy value, for compatibility with BIP22. This value is ignored."},
        },
        {
            RPCResult{"If the block was accepted", RPCResult::Type::NONE, "", ""},
            RPCResult{"Otherwise", RPCResult::Type::STR, "", "According to BIP22"},
        },
        RPCExamples{
                    HelpExampleCli("submitblock", "\"mydata\"")
            + HelpExampleRpc("submitblock", "\"mydata\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CBlock> blockptr = std::make_shared<CBlock>();
    CBlock& block = *blockptr;
    if (!DecodeHexBlk(block, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
    }

    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block does not start with a coinbase");
    }

    ChainstateManager& chainman = EnsureAnyChainman(request.context);
    uint256 hash = block.GetHash();
    {
        LOCK(cs_main);
        const CBlockIndex* pindex = chainman.m_blockman.LookupBlockIndex(hash);
        if (pindex) {
            if (pindex->IsValid(BLOCK_VALID_SCRIPTS)) {
                return "duplicate";
            }
            if (pindex->nStatus & BLOCK_FAILED_MASK) {
                return "duplicate-invalid";
            }
        }
    }

    {
        LOCK(cs_main);
        const CBlockIndex* pindex = chainman.m_blockman.LookupBlockIndex(block.hashPrevBlock);
        if (pindex) {
            chainman.UpdateUncommittedBlockStructures(block, pindex);
        }
    }

    bool new_block;
    auto sc = std::make_shared<submitblock_StateCatcher>(block.GetHash());
    RegisterSharedValidationInterface(sc);
    bool accepted = chainman.ProcessNewBlock(blockptr, /*force_processing=*/true, /*min_pow_checked=*/true, /*new_block=*/&new_block);
    UnregisterSharedValidationInterface(sc);
    if (!new_block && accepted) {
        return "duplicate";
    }
    if (!sc->found) {
        return "inconclusive";
    }
    return BIP22ValidationResult(sc->state);
},
    };
}

static RPCHelpMan submitheader()
{
    return RPCHelpMan{"submitheader",
                "\nDecode the given hexdata as a header and submit it as a candidate chain tip if valid."
                "\nThrows when the header is invalid.\n",
                {
                    {"hexdata", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "the hex-encoded block header data"},
                },
                RPCResult{
                    RPCResult::Type::NONE, "", "None"},
                RPCExamples{
                    HelpExampleCli("submitheader", "\"aabbcc\"") +
                    HelpExampleRpc("submitheader", "\"aabbcc\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    CBlockHeader h;
    if (!DecodeHexBlockHeader(h, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block header decode failed");
    }
    ChainstateManager& chainman = EnsureAnyChainman(request.context);
    {
        LOCK(cs_main);
        if (!chainman.m_blockman.LookupBlockIndex(h.hashPrevBlock)) {
            throw JSONRPCError(RPC_VERIFY_ERROR, "Must submit previous header (" + h.hashPrevBlock.GetHex() + ") first");
        }
    }

    BlockValidationState state;
    chainman.ProcessNewBlockHeaders({h}, /*min_pow_checked=*/true, state);
    if (state.IsValid()) return UniValue::VNULL;
    if (state.IsError()) {
        throw JSONRPCError(RPC_VERIFY_ERROR, state.ToString());
    }
    throw JSONRPCError(RPC_VERIFY_ERROR, state.GetRejectReason());
},
    };
}

void RegisterMiningRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        {"mining", &getnetworkhashps},
        {"mining", &getmininginfo},
        {"mining", &prioritisetransaction},
        {"mining", &getprioritisedtransactions},
        {"mining", &getblocktemplate},
        {"mining", &submitblock},
        {"mining", &submitheader},
        {"mining", &setgenerate},
        {"hidden", &generatetoaddress},
        {"hidden", &generatetodescriptor},
        {"hidden", &generateblock},
        {"hidden", &generate},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
