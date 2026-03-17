// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2026 The CapStash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CAPSTASH_POLICY_FEERATE_H
#define CAPSTASH_POLICY_FEERATE_H

#include <consensus/amount.h>
#include <serialize.h>


#include <cstdint>
#include <string>
#include <type_traits>

const std::string CURRENCY_UNIT = "Cap"; // One formatted unit
const std::string CURRENCY_ATOM = "sat"; // One indivisible minimum value unit

/* Used to determine type of fee estimation requested */
enum class FeeEstimateMode {
    UNSET,        //!< Use default settings based on other criteria
    ECONOMICAL,   //!< Force estimateSmartFee to use non-conservative estimates
    CONSERVATIVE, //!< Force estimateSmartFee to use conservative estimates
    Cap_KVB,      //!< Use Cap/kvB fee rate unit
    SAT_VB,       //!< Use sat/vB fee rate unit
};

/**
 * Fee rate in satoshis per kilovirtualbyte: CAmount / kvB
 */
class CFeeRate
{
private:
    /** Fee rate in sat/kvB (satoshis per 1000 virtualbytes) */
    CAmount nPipsPerK;

public:
    /** Fee rate of 0 satoshis per kvB */
    CFeeRate() : nPipsPerK(0) { }
    template<typename I>
    explicit CFeeRate(const I _nPipsPerK): nPipsPerK(_nPipsPerK) {
        // We've previously had bugs creep in from silent double->int conversion...
        static_assert(std::is_integral<I>::value, "CFeeRate should be used without floats");
    }

    /**
     * Construct a fee rate from a fee in satoshis and a vsize in vB.
     *
     * param@[in]   nFeePaid    The fee paid by a transaction, in satoshis
     * param@[in]   num_bytes   The vsize of a transaction, in vbytes
     */
    CFeeRate(const CAmount& nFeePaid, uint32_t num_bytes);

    /**
     * Return the fee in satoshis for the given vsize in vbytes.
     * If the calculated fee would have fractional satoshis, then the
     * returned fee will always be rounded up to the nearest satoshi.
     */
    CAmount GetFee(uint32_t num_bytes) const;

    /**
     * Return the fee in satoshis for a vsize of 1000 vbytes
     */
    CAmount GetFeePerK() const { return nPipsPerK; }
    friend bool operator<(const CFeeRate& a, const CFeeRate& b) { return a.nPipsPerK < b.nPipsPerK; }
    friend bool operator>(const CFeeRate& a, const CFeeRate& b) { return a.nPipsPerK > b.nPipsPerK; }
    friend bool operator==(const CFeeRate& a, const CFeeRate& b) { return a.nPipsPerK == b.nPipsPerK; }
    friend bool operator<=(const CFeeRate& a, const CFeeRate& b) { return a.nPipsPerK <= b.nPipsPerK; }
    friend bool operator>=(const CFeeRate& a, const CFeeRate& b) { return a.nPipsPerK >= b.nPipsPerK; }
    friend bool operator!=(const CFeeRate& a, const CFeeRate& b) { return a.nPipsPerK != b.nPipsPerK; }
    CFeeRate& operator+=(const CFeeRate& a) { nPipsPerK += a.nPipsPerK; return *this; }
    std::string ToString(const FeeEstimateMode& fee_estimate_mode = FeeEstimateMode::Cap_KVB) const;
    friend CFeeRate operator*(const CFeeRate& f, int a) { return CFeeRate(a * f.nPipsPerK); }
    friend CFeeRate operator*(int a, const CFeeRate& f) { return CFeeRate(a * f.nPipsPerK); }

    SERIALIZE_METHODS(CFeeRate, obj) { READWRITE(obj.nPipsPerK); }
};

#endif // CAPSTASH_POLICY_FEERATE_H
