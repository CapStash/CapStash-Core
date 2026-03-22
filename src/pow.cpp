// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2026 The CapStash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <hash.h>
#include <primitives/block.h>
#include <serialize.h>
#include <uint256.h>

#include <assert.h>

// -----------------------------------------------------------------------------
// Activation helpers
// -----------------------------------------------------------------------------
//
// Activation heights are defined per-network in chainparams.cpp via:
//
//   consensus.nMinDiffRescueHeight
//   consensus.nMinDiffQuarantineHeight
//   consensus.nLotteryConsensusHeight
//   consensus.nLotteryFinalConsensusHeight
//
static inline bool MinDiffRescueActive(const Consensus::Params& params, int64_t height)
{
    return params.fPowAllowMinDifficultyBlocks &&
           height >= params.nMinDiffRescueHeight;
}

static inline bool MinDiffQuarantineActive(const Consensus::Params& params, int64_t height)
{
    return params.fPowAllowMinDifficultyBlocks &&
           height >= params.nMinDiffQuarantineHeight;
}

static inline bool LotteryConsensusActive(const Consensus::Params& params, int64_t height)
{
    return params.fPowAllowMinDifficultyBlocks &&
           height >= params.nLotteryConsensusHeight;
}

static inline bool LotteryFinalConsensusActive(const Consensus::Params& params, int64_t height)
{
    return params.fPowAllowMinDifficultyBlocks &&
           height >= params.nLotteryFinalConsensusHeight;
}

// -----------------------------------------------------------------------------
// DGW / lottery tuning
// -----------------------------------------------------------------------------

// 24 blocks @ 60s target = ~24 minutes of history.
static constexpr int64_t nPastBlocks = 24;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static inline uint32_t PowLimitBits(const Consensus::Params& params)
{
    return UintToArith256(params.powLimit).GetCompact();
}

static uint64_t ReadLE64FromUint256(const uint256& v)
{
    const unsigned char* p = v.begin();
    return ((uint64_t)p[0]) |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

static inline int64_t BlockTimeForDGW(const CBlockIndex* pindex,
                                      const Consensus::Params& params,
                                      int64_t next_height)
{
    // Final fork: use MTP to harden difficulty against raw timestamp games.
    if (LotteryFinalConsensusActive(params, next_height)) {
        return pindex->GetMedianTimePast();
    }

    // Earlier eras: preserve legacy/raw-time behavior for compatibility.
    return pindex->GetBlockTime();
}

// -----------------------------------------------------------------------------
// Legacy rescue block helpers (pre-first-fork behavior)
// -----------------------------------------------------------------------------

static inline bool IsLegacyRescueMinDifficultyBlock(const CBlockIndex* pindex,
                                                    const Consensus::Params& params)
{
    if (pindex == nullptr || pindex->pprev == nullptr) {
        return false;
    }

    // Legacy rescue classification only applies before the lottery-consensus HF.
    if (LotteryConsensusActive(params, pindex->nHeight)) {
        return false;
    }

    // A block can only be considered a rescue block if rescue rules were active
    // at that block's own height.
    if (!MinDiffRescueActive(params, pindex->nHeight)) {
        return false;
    }

    if (pindex->nBits != PowLimitBits(params)) {
        return false;
    }

    // Legacy rescue rule: more than 2x target spacing late.
    return pindex->GetBlockTime() >
           pindex->pprev->GetBlockTime() + (params.nPowTargetSpacing * 2);
}

// -----------------------------------------------------------------------------
// Consensus lottery helpers
// -----------------------------------------------------------------------------
//
// Era 1 (>= nLotteryConsensusHeight, < nLotteryFinalConsensusHeight):
//   - lottery selected from prior chain state using v1 domain separator
//   - consecutive lottery blocks allowed
//
// Era 2 (>= nLotteryFinalConsensusHeight):
//   - lottery selected from prior chain state using v2 domain separator
//   - consecutive lottery blocks NOT allowed
//   - DGW uses MTP
//

static inline bool IsConsensusLotteryBlockV1(const CBlockIndex* pindex,
                                             const Consensus::Params& params);

static inline bool IsConsensusLotteryHeightV1(const CBlockIndex* pindexLast,
                                              const Consensus::Params& params,
                                              int64_t next_height)
{
    if (pindexLast == nullptr) {
        return false;
    }

    if (!LotteryConsensusActive(params, next_height) ||
        LotteryFinalConsensusActive(params, next_height)) {
        return false;
    }

    if (params.nLotteryModulo <= 0) {
        return false;
    }

    HashWriter ss{};
    ss << pindexLast->GetBlockHash();
    ss << next_height;

    if (pindexLast->pprev != nullptr) {
        ss << pindexLast->pprev->GetBlockHash();
    } else {
        ss << uint256();
    }

    ss << std::string("CapStash-Lottery-v1");

    const uint256 seed = ss.GetHash();
    const uint64_t r = ReadLE64FromUint256(seed);

    return (r % static_cast<uint64_t>(params.nLotteryModulo)) == 0;
}

static inline bool IsConsensusLotteryBlockV1(const CBlockIndex* pindex,
                                             const Consensus::Params& params)
{
    if (pindex == nullptr || pindex->pprev == nullptr) {
        return false;
    }

    if (!LotteryConsensusActive(params, pindex->nHeight) ||
        LotteryFinalConsensusActive(params, pindex->nHeight)) {
        return false;
    }

    if (pindex->nBits != PowLimitBits(params)) {
        return false;
    }

    return IsConsensusLotteryHeightV1(pindex->pprev, params, pindex->nHeight);
}

static inline bool IsConsensusLotteryBlockV2(const CBlockIndex* pindex,
                                             const Consensus::Params& params);

static inline bool IsConsensusLotteryHeightV2(const CBlockIndex* pindexLast,
                                              const Consensus::Params& params,
                                              int64_t next_height)
{
    if (pindexLast == nullptr) {
        return false;
    }

    if (!LotteryFinalConsensusActive(params, next_height)) {
        return false;
    }

    if (params.nLotteryModulo <= 0) {
        return false;
    }

    // Final-era hardening: never allow consecutive lottery blocks.
    if (IsConsensusLotteryBlockV2(pindexLast, params)) {
        return false;
    }

    HashWriter ss{};
    ss << pindexLast->GetBlockHash();
    ss << next_height;

    if (pindexLast->pprev != nullptr) {
        ss << pindexLast->pprev->GetBlockHash();
    } else {
        ss << uint256();
    }

    ss << std::string("CapStash-Lottery-v2");

    const uint256 seed = ss.GetHash();
    const uint64_t r = ReadLE64FromUint256(seed);

    return (r % static_cast<uint64_t>(params.nLotteryModulo)) == 0;
}

static inline bool IsConsensusLotteryBlockV2(const CBlockIndex* pindex,
                                             const Consensus::Params& params)
{
    if (pindex == nullptr || pindex->pprev == nullptr) {
        return false;
    }

    if (!LotteryFinalConsensusActive(params, pindex->nHeight)) {
        return false;
    }

    if (pindex->nBits != PowLimitBits(params)) {
        return false;
    }

    return IsConsensusLotteryHeightV2(pindex->pprev, params, pindex->nHeight);
}

static inline bool IsConsensusLotteryHeight(const CBlockIndex* pindexLast,
                                            const Consensus::Params& params,
                                            int64_t next_height)
{
    if (LotteryFinalConsensusActive(params, next_height)) {
        return IsConsensusLotteryHeightV2(pindexLast, params, next_height);
    }

    return IsConsensusLotteryHeightV1(pindexLast, params, next_height);
}

static inline bool IsConsensusLotteryBlock(const CBlockIndex* pindex,
                                           const Consensus::Params& params)
{
    if (LotteryFinalConsensusActive(params, pindex ? pindex->nHeight : 0)) {
        return IsConsensusLotteryBlockV2(pindex, params);
    }

    return IsConsensusLotteryBlockV1(pindex, params);
}

// -----------------------------------------------------------------------------
// DGW sample eligibility
// -----------------------------------------------------------------------------

static inline bool IsEligibleDGWBlock(const CBlockIndex* pindex,
                                      const Consensus::Params& params,
                                      int64_t next_height)
{
    if (pindex == nullptr) {
        return false;
    }

    // Before quarantine HF, DGW behaves traditionally and counts everything.
    if (!MinDiffQuarantineActive(params, next_height)) {
        return true;
    }

    // Legacy pre-fork rescue block quarantine.
    if (IsLegacyRescueMinDifficultyBlock(pindex, params)) {
        return pindex->nHeight <= (next_height - nPastBlocks);
    }

    // Post-fork consensus lottery block quarantine.
    if (IsConsensusLotteryBlock(pindex, params)) {
        return pindex->nHeight <= (next_height - nPastBlocks);
    }

    return true;
}

// -----------------------------------------------------------------------------
// Dark Gravity Wave v3-style difficulty adjustment
// -----------------------------------------------------------------------------

static unsigned int DarkGravityWave(const CBlockIndex* pindexLast,
                                    const Consensus::Params& params,
                                    int64_t next_height)
{
    assert(pindexLast != nullptr);

    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);

    if (params.fPowNoRetargeting) {
        return pindexLast->nBits;
    }

    if (pindexLast->nHeight < nPastBlocks) {
        return bnPowLimit.GetCompact();
    }

    const CBlockIndex* pindex = pindexLast;

    arith_uint256 bnPastTargetAvg;
    arith_uint256 bnTarget;
    bool fNegative = false;
    bool fOverflow = false;

    int64_t nActualTimespan = 0;
    int64_t nBlockCount = 0;
    int64_t nLastEligibleBlockTime = 0;

    while (pindex != nullptr && nBlockCount < nPastBlocks) {
        if (!IsEligibleDGWBlock(pindex, params, next_height)) {
            pindex = pindex->pprev;
            continue;
        }

        bnTarget.SetCompact(pindex->nBits, &fNegative, &fOverflow);
        if (fNegative || fOverflow || bnTarget == 0) {
            return bnPowLimit.GetCompact();
        }

        ++nBlockCount;

        if (nBlockCount == 1) {
            bnPastTargetAvg = bnTarget;
        } else {
            bnPastTargetAvg = ((bnPastTargetAvg * (nBlockCount - 1)) + bnTarget) / nBlockCount;
        }

        const int64_t nThisBlockTime = BlockTimeForDGW(pindex, params, next_height);

        if (nLastEligibleBlockTime > 0) {
            int64_t nDiff = nLastEligibleBlockTime - nThisBlockTime;

            if (nDiff < 0) {
                nDiff = 0;
            }

            nActualTimespan += nDiff;
        }

        nLastEligibleBlockTime = nThisBlockTime;
        pindex = pindex->pprev;
    }

    if (nBlockCount < nPastBlocks) {
        return bnPowLimit.GetCompact();
    }

    const int64_t nTargetTimespan = nPastBlocks * params.nPowTargetSpacing;

    if (nActualTimespan < nTargetTimespan / 3) {
        nActualTimespan = nTargetTimespan / 3;
    }
    if (nActualTimespan > nTargetTimespan * 3) {
        nActualTimespan = nTargetTimespan * 3;
    }

    arith_uint256 bnNew = bnPastTargetAvg;
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew == 0 || bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

// -----------------------------------------------------------------------------
// Exists only to satisfy linker requirements for clean compilation
// -----------------------------------------------------------------------------

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast,
                                       int64_t nFirstBlockTime,
                                       const Consensus::Params& params)
{
    (void)nFirstBlockTime;

    assert(pindexLast != nullptr);

    if (params.fPowNoRetargeting) {
        return pindexLast->nBits;
    }

    const int64_t next_height = pindexLast->nHeight + 1;
    return DarkGravityWave(pindexLast, params, next_height);
}

// -----------------------------------------------------------------------------
// Next work required
// -----------------------------------------------------------------------------

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast,
                                 const CBlockHeader* pblock,
                                 const Consensus::Params& params)
{
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);

    if (pindexLast == nullptr) {
        return bnPowLimit.GetCompact();
    }

    if (params.fPowNoRetargeting) {
        return pindexLast->nBits;
    }

    const int64_t next_height = pindexLast->nHeight + 1;

    // Post-36000 lottery era and post-36300 final lottery era both derive
    // lottery eligibility from prior confirmed chain state only.
    if (LotteryConsensusActive(params, next_height)) {
        if (IsConsensusLotteryHeight(pindexLast, params, next_height)) {
            return bnPowLimit.GetCompact();
        }
        return DarkGravityWave(pindexLast, params, next_height);
    }

    // Legacy pre-36000 rescue rule.
    if (MinDiffRescueActive(params, next_height)) {
        if (pblock->GetBlockTime() >
            pindexLast->GetBlockTime() + (params.nPowTargetSpacing * 2)) {
            return bnPowLimit.GetCompact();
        }
    }

    return DarkGravityWave(pindexLast, params, next_height);
}

// -----------------------------------------------------------------------------
// Proof-of-Work checks
// -----------------------------------------------------------------------------

bool CheckProofOfWork(uint256 hash,
                      unsigned int nBits,
                      const Consensus::Params& params)
{
    bool fNegative = false;
    bool fOverflow = false;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    if (fNegative || bnTarget == 0 || fOverflow ||
        bnTarget > UintToArith256(params.powLimit)) {
        return false;
    }

    if (UintToArith256(hash) > bnTarget) {
        return false;
    }

    return true;
}

bool CheckProofOfWork(const CBlockHeader& block,
                      const Consensus::Params& params)
{
    const uint256 powhash = block.GetPoWHash();
    return CheckProofOfWork(powhash, block.nBits, params);
}

// -----------------------------------------------------------------------------
// Difficulty transition sanity check
// -----------------------------------------------------------------------------

bool PermittedDifficultyTransition(const Consensus::Params& params,
                                   int64_t height,
                                   uint32_t old_nbits,
                                   uint32_t new_nbits)
{
    if (params.fPowNoRetargeting) {
        return true;
    }

    const uint32_t pow_limit_bits = PowLimitBits(params);

    // Legacy pre-36000 rescue allowance.
    if (!LotteryConsensusActive(params, height) &&
        MinDiffRescueActive(params, height) &&
        new_nbits == pow_limit_bits) {
        return true;
    }

    // Era 1 (36000..36299): preserve existing lottery-era permissive allowance
    // so the already-deployed intermediate fork remains compatible.
    if (LotteryConsensusActive(params, height) &&
        !LotteryFinalConsensusActive(params, height) &&
        new_nbits == pow_limit_bits) {
        return true;
    }

    // Era 2 (>= 36300): no broad powLimit allowance here. Exact legality of a
    // powLimit block must come from the real next-work calculation.
    bool fNeg = false;
    bool fOverflow = false;
    arith_uint256 old_target, new_target;

    old_target.SetCompact(old_nbits, &fNeg, &fOverflow);
    if (fNeg || fOverflow || old_target == 0) {
        return false;
    }

    fNeg = false;
    fOverflow = false;
    new_target.SetCompact(new_nbits, &fNeg, &fOverflow);
    if (fNeg || fOverflow || new_target == 0 ||
        new_target > UintToArith256(params.powLimit)) {
        return false;
    }

    if (new_target > old_target * 4) {
        return false;
    }
    if (new_target < old_target / 4) {
        return false;
    }

    return true;
}
