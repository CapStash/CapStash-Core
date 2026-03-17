// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2025 The CapStash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CAPSTASH_POW_H
#define CAPSTASH_POW_H

#include <consensus/params.h>
#include <arith_uint256.h>

#include <stdint.h>

class CBlockHeader;
class CBlockIndex;
class uint256;

/** 
 * Compute the next required work based on the last block index and the
 * candidate block header.
 *
 * Uses classic CapStash 2016-block retarget rules via Consensus::Params.
 */
unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast,
                                 const CBlockHeader* pblock,
                                 const Consensus::Params& params);

/**
 * Internal helper: calculate next work given the last block and the time
 * of the first block in the adjustment interval.
 */
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast,
                                       int64_t nFirstBlockTime,
                                       const Consensus::Params& params);

/**
 * Return the work represented by the block.
 * Work is defined as 2^256 / (target+1).
 */
arith_uint256 GetBlockProof(const CBlockIndex& block);

/**
 * Return the equivalent time it would take to produce the amount of work
 * represented by "to" - "from", at the current network difficulty (tip).
 */
int64_t GetBlockProofEquivalentTime(const CBlockIndex& to,
                                    const CBlockIndex& from,
                                    const CBlockIndex& tip,
                                    const Consensus::Params& params);

/**
 * Check PoW for an already-computed hash value.
 * This uses nBits to build the target and compares hash <= target.
 */
bool CheckProofOfWork(uint256 hash,
                      unsigned int nBits,
                      const Consensus::Params& params);

/**
 * Header-based PoW check.
 * This calls GetPoWHash(header) (your Whirlpool-based PoW hash)
 * and applies the same target check as the hash-based overload.
 */
bool CheckProofOfWork(const CBlockHeader& block,
                      const Consensus::Params& params);

/**
 * Check whether a difficulty transition from old_nbits to new_nbits at
 * given height is permitted by consensus rules.
 *
 * - On networks that allow minimum difficulty blocks (e.g. regtest/testnet),
 *   this always returns true.
 * - On mainnet-style networks, difficulty can only change at the regular
 *   difficulty adjustment interval, and only within a factor of 4 up or down.
 */
bool PermittedDifficultyTransition(const Consensus::Params& params,
                                   int64_t height,
                                   uint32_t old_nbits,
                                   uint32_t new_nbits);

#endif // CAPSTASH_POW_H
