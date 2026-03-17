// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The CapStash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>
#include <crypto/whirlpool.h>
#include <string.h>    
#include <hash.h>
#include <tinyformat.h>
#include <cstring>
#include <array>



uint256 CBlockHeader::GetHash() const
{
    // Standard double-SHA256 header ID using the new HashWriter API
    HashWriter hasher{};
    hasher << *this;
    return hasher.GetHash();
}

uint256 CBlockHeader::GetPoWHash() const
{
    // Build the canonical 80-byte block header buffer:
    // [ nVersion | hashPrevBlock | hashMerkleRoot | nTime | nBits | nNonce ]
    unsigned char buf[80];
    unsigned char* p = buf;

    auto write32 = [](unsigned char* out, uint32_t x) {
        out[0] = static_cast<unsigned char>(x & 0xff);
        out[1] = static_cast<unsigned char>((x >> 8) & 0xff);
        out[2] = static_cast<unsigned char>((x >> 16) & 0xff);
        out[3] = static_cast<unsigned char>((x >> 24) & 0xff);
    };

    // nVersion (4 bytes, LE)
    write32(p, static_cast<uint32_t>(nVersion));
    p += 4;

    // hashPrevBlock (32 bytes)
    std::memcpy(p, hashPrevBlock.begin(), 32);
    p += 32;

    // hashMerkleRoot (32 bytes)
    std::memcpy(p, hashMerkleRoot.begin(), 32);
    p += 32;

    // nTime (4 bytes, LE)
    write32(p, nTime);
    p += 4;

    // nBits (4 bytes, LE)
    write32(p, nBits);
    p += 4;

    // nNonce (4 bytes, LE)
    write32(p, nNonce);

    // 1) Whirlpool-512 over the 80-byte header
    unsigned char wh[WHIRLPOOL512_OUTPUT_SIZE];
    Whirlpool512(buf, sizeof(buf), wh);

    // 2) Compress 512 -> 256 bits:
    //    XOR the lower 32 bytes with the upper 32 bytes.
    unsigned char out32[32];
    for (int i = 0; i < 32; ++i) {
        out32[i] = wh[i] ^ wh[i + 32];
    }

    // 3) Interpret as uint256 (Core stores these as little-endian internally)
    uint256 ret;
    std::memcpy(ret.begin(), out32, 32);
    return ret;
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}
