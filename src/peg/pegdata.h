// Copyright (c) 2018 yshurik
//
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The use in another cyptocurrency project the code is licensed under
// Jelurida Public License (JPL). See https://www.jelurida.com/resources/jpl

#ifndef BITBAY_PEGDATA_H
#define BITBAY_PEGDATA_H

#include "bignum.h"
#include <cstdint>
#include <string>

enum
{
    PEG_RATE                = 100,
    PEG_SIZE                = 1200,
    PEG_MAKETX_FREEZE_VALUE = 5590,
    PEG_MAKETX_VFREEZE_VALUE= 5590,
    PEG_MAKETX_FEE_INP_OUT  = 5000,
    PEG_MAKETX_VOTE_VALUE   = 5554,
    PEG_SUBPREMIUM_RATING   = 200,
    PEG_DB_CHECK1           = 1, // testnet: update1 for votes calculation
    PEG_DB_CHECK2           = 2, // testnet: update2 for stake liquidity calculation
    PEG_DB_CHECK_ON_FORK    = 35,
    PEG_PRUNE_INTERVAL      = 10000
};

class CPegLevel;
class CFractions;

int64_t RatioPart(int64_t nValue,
                  int64_t nPartValue,
                  int64_t nTotalValue);

class CPegLevel {
public:
    uint8_t nVersion        = 2;
    int64_t nCycle          = 0;
    int64_t nCyclePrev      = 0;
    int16_t nBuffer         = 3;
    int16_t nSupply         = 0;
    int16_t nSupplyNext     = 0;
    int16_t nSupplyNextNext = 0;
    int16_t nShift          = 0;
    int64_t nShiftLastPart  = 0;
    int64_t nShiftLastTotal = 0;

    CPegLevel(int cycle,
              int cycle_prev,
              int buffer,
              int supply,
              int supply_next,
              int supply_next_next);
    CPegLevel(int cycle,
              int cycle_prev,
              int buffer,
              int supply,
              int supply_next,
              int supply_next_next,
              const CFractions & fractions,
              const CFractions & distortion);
    CPegLevel(std::string);
    CPegLevel();

    friend bool operator<(const CPegLevel &a, const CPegLevel &b);
    friend bool operator==(const CPegLevel &a, const CPegLevel &b);
    friend bool operator!=(const CPegLevel &a, const CPegLevel &b);

    bool IsValid() const;
    bool HasShift() const { return nShift != 0 || nShiftLastPart != 0; }
    bool Pack(CDataStream &) const;
    bool Unpack(CDataStream &);
    std::string ToString() const;

private:
    friend class CPegData;
    bool Unpack1(CDataStream &);
};

class CFractions {
public:
    uint8_t     nVersion   = 1;
    uint32_t    nFlags     = VALUE;
    uint64_t    nLockTime  = 0;
    std::string sReturnAddr;
    enum
    {
        VALUE       = (1 << 0),
        STD         = (1 << 1),
        NOTARY_F    = (1 << 2),
        NOTARY_L    = (1 << 3),
        NOTARY_V    = (1 << 4),
        NOTARY_C    = (1 << 5)
    };
    enum MarkAction {
        MARK_SET            = 0,
        MARK_TRANSFER       = 1,
        MARK_COLD_TO_FROZEN = 2
    };
    enum {
        SER_MASK    = 0xffff,
        SER_VALUE   = (1 << 16),
        SER_ZDELTA  = (1 << 17),
        SER_RAW     = (1 << 18)
    };
    std::unique_ptr<int64_t[]> f;

    CFractions();
    CFractions(int64_t, uint32_t flags);
    CFractions(const CFractions &);
    CFractions& operator=(const CFractions&);

    bool Pack(CDataStream &, unsigned long* len =nullptr, bool compress=true) const;
    bool Unpack(CDataStream &);

    CFractions Std() const;
    CFractions Positive(int64_t* total) const;
    CFractions Negative(int64_t* total) const;
    CFractions LowPart(int supply, int64_t* total) const;
    CFractions HighPart(int supply, int64_t* total) const;
    CFractions LowPart(const CPegLevel &, int64_t* total) const;
    CFractions HighPart(const CPegLevel &, int64_t* total) const;
    CFractions MidPart(const CPegLevel &, const CPegLevel &) const;
    CFractions RatioPart(int64_t part) const;

    CFractions& operator+=(const CFractions& b);
    CFractions& operator-=(const CFractions& b);
    friend CFractions operator+(CFractions a, const CFractions& b) { a += b; return a; }
    friend CFractions operator-(CFractions a, const CFractions& b) { a -= b; return a; }
    CFractions operator&(const CFractions& b) const;
    CFractions operator-() const;

    int64_t MoveRatioPartTo(int64_t nPartValue,
                            CFractions& b);

    void ToDeltas(int64_t* deltas) const;
    void FromDeltas(const int64_t* deltas);

    int64_t Low(int supply) const;
    int64_t High(int supply) const;
    int64_t Low(const CPegLevel &) const;
    int64_t High(const CPegLevel &) const;
    int64_t NChange(const CPegLevel &) const;
    int64_t NChange(int src_supply, int dst_supply) const;
    int16_t HLI() const;
    int64_t Total() const;
    double Distortion(const CFractions& b) const;
    bool IsPositive() const;
    bool IsNegative() const;

    bool SetMark(MarkAction, uint32_t nMark, uint64_t nTime);

private:
    void ToStd();
    friend class CPegData;
    bool Unpack1(CDataStream &);
    bool Unpack2(CDataStream &);
};

typedef std::map<uint320, CFractions> MapFractions;

class CPegData {
public:
    CPegData() {}
    CPegData(std::string);

    bool IsValid() const;

    int64_t     nVersion    = 1;
    CFractions  fractions;
    CPegLevel   peglevel;
    int64_t     nLiquid     = 0;
    int64_t     nReserve    = 0;
    int32_t     nId         = 0;

    bool Pack(CDataStream &) const;
    bool Unpack(CDataStream &);
    std::string ToString() const;

private: // compat
    bool Unpack1(CDataStream &);
    bool Unpack2(CDataStream &);
};

#endif
