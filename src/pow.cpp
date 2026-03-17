// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2026 The CapStash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
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

// -----------------------------------------------------------------------------
// DGW / rescue tuning
// -----------------------------------------------------------------------------

// 24 blocks @ 60s target = ~24 minutes of history.
static constexpr int64_t nPastBlocks = 24;

// -----------------------------------------------------------------------------
// Rescue block helpers
// -----------------------------------------------------------------------------

static inline uint32_t PowLimitBits(const Consensus::Params& params)
{
    return UintToArith256(params.powLimit).GetCompact();
}

static inline bool IsRescueMinDifficultyBlock(const CBlockIndex* pindex,
                                              const Consensus::Params& params)
{
    if (pindex == nullptr || pindex->pprev == nullptr) {
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

    // Rescue rule: more than 2x target spacing late.
    return pindex->GetBlockTime() >
           pindex->pprev->GetBlockTime() + (params.nPowTargetSpacing * 2);
}

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

    // Non-rescue blocks always count.
    if (!IsRescueMinDifficultyBlock(pindex, params)) {
        return true;
    }

    // After quarantine activates, rescue blocks are ignored for one full DGW
    // window before they become eligible samples.
    //
    // Computing work for height H:
    //   rescue block at H-1 .. H-23  => ignored
    //   rescue block at <= H-24      => eligible
    return pindex->nHeight <= (next_height - nPastBlocks);
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

    // Until we have enough history, stay at floor.
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
            // Correct running average:
            // new_avg = ((old_avg * (count - 1)) + sample) / count
            bnPastTargetAvg = ((bnPastTargetAvg * (nBlockCount - 1)) + bnTarget) / nBlockCount;
        }

        if (nLastEligibleBlockTime > 0) {
            int64_t nDiff = nLastEligibleBlockTime - pindex->GetBlockTime();

            // Guard against bad timestamps.
            if (nDiff < 0) {
                nDiff = 0;
            }

            nActualTimespan += nDiff;
        }

        nLastEligibleBlockTime = pindex->GetBlockTime();
        pindex = pindex->pprev;
    }

    // If we cannot gather a full eligible sample set, fall back to minimum
    // difficulty rather than deriving work from a partial window.
    if (nBlockCount < nPastBlocks) {
        return bnPowLimit.GetCompact();
    }

    const int64_t nTargetTimespan = nPastBlocks * params.nPowTargetSpacing;

    // Clamp the adjustment window.
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

    // Genesis block
    if (pindexLast == nullptr) {
        return bnPowLimit.GetCompact();
    }

    if (params.fPowNoRetargeting) {
        return pindexLast->nBits;
    }

    const int64_t next_height = pindexLast->nHeight + 1;

    // 2x-spacing rescue min-difficulty rule.
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

    // If rescue rules are active, explicitly allow a jump to powLimit.
    // This covers legal rescue blocks without disabling all sanity checking
    // for the entire post-rescue era.
    if (MinDiffRescueActive(params, height) && new_nbits == pow_limit_bits) {
        return true;
    }

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

    // Broad per-block sanity rails for non-rescue transitions.
    if (new_target > old_target * 4) {
        return false;
    }
    if (new_target < old_target / 4) {
        return false;
    }

    return true;
}
