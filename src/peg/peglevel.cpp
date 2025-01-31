// Copyright (c) 2018 yshurik
//
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The use in another cyptocurrency project the code is licensed under
// Jelurida Public License (JPL). See https://www.jelurida.com/resources/jpl

#include "pegdata.h"
#include "utilstrencodings.h"

#include <map>
#include <set>
#include <cstdint>
#include <utility>
#include <algorithm>
#include <type_traits>

#include <boost/multiprecision/cpp_int.hpp>

#include <zconf.h>
#include <zlib.h>

using namespace std;
using namespace boost;

CPegLevel::CPegLevel() {
    // invalid
    nSupply         = -1;
    nSupplyNext     = -1;
    nSupplyNextNext = -1;
}

CPegLevel::CPegLevel(std::string str) {
    vector<unsigned char> data(ParseHex(str));
    if (str.length() == 80) {
        // previous version
        CDataStream finp(data, SER_DISK, CLIENT_VERSION);
        if (!Unpack1(finp)) {
            // invalid
            nSupply         = -1;
            nSupplyNext     = -1;
            nSupplyNextNext = -1;
            nShift          = 0;
            nShiftLastPart  = 0;
            nShiftLastTotal = 0;
        }
        return;
    }
    CDataStream finp(data, SER_NETWORK, CLIENT_VERSION);
    if (!Unpack(finp)) {
        // previous version
        CDataStream finp(data, SER_DISK, CLIENT_VERSION);
        if (!Unpack1(finp)) {
            // invalid
            nSupply         = -1;
            nSupplyNext     = -1;
            nSupplyNextNext = -1;
            nShift          = 0;
            nShiftLastPart  = 0;
            nShiftLastTotal = 0;
        }
    }
}

CPegLevel::CPegLevel(int cycle,
                     int cycle_prev,
                     int buffer,
                     int supply,
                     int supply_next,
                     int supply_next_next) {
    nCycle          = cycle;
    nCyclePrev      = cycle_prev;
    nBuffer         = buffer;
    nSupply         = std::min(supply+buffer, PEG_SIZE-1);
    nSupplyNext     = std::min(supply_next+buffer, PEG_SIZE-1);
    nSupplyNextNext = std::min(supply_next_next+buffer, PEG_SIZE-1);
    nShift          = 0;
    nShiftLastPart  = 0;
    nShiftLastTotal = 0;
}

CPegLevel::CPegLevel(int cycle,
                     int cycle_prev,
                     int buffer,
                     int supply,
                     int supply_next,
                     int supply_next_next,
                     const CFractions & frInput,
                     const CFractions & frDistortion) {
    nCycle          = cycle;
    nCyclePrev      = cycle_prev;
    nBuffer         = buffer;
    nSupply         = std::min(supply+buffer, PEG_SIZE-1);
    nSupplyNext     = std::min(supply_next+buffer, PEG_SIZE-1);
    nSupplyNextNext = std::min(supply_next_next+buffer, PEG_SIZE-1);
    nShift          = 0;
    nShiftLastPart  = 0;
    nShiftLastTotal = 0;

    CFractions frOutput = frInput + frDistortion;
    int64_t nInputLiquid = frInput.High(nSupply);
    int64_t nOutputLiquid = frOutput.High(nSupply);

    if (nOutputLiquid < nInputLiquid) {

        int64_t nLiquidDiff = nInputLiquid - nOutputLiquid;
        int64_t nLiquidDiffLeft = nLiquidDiff;
        nShiftLastTotal = 0;

        int i = nSupply;
        while(nLiquidDiffLeft > 0 && i < PEG_SIZE) {
            int64_t nLiquid = frInput.f[i];
            if (nLiquid > nLiquidDiffLeft) {
                // this fraction to distribute
                // with ratio nLiquidCutLeft/nLiquid
                nShiftLastTotal = nLiquid;
                break;
            }

            nShift++;
            nLiquidDiffLeft -= nLiquid;
            i++;
        }
        nShiftLastPart = nLiquidDiffLeft;
    }
}

bool CPegLevel::IsValid() const {
    return  nBuffer             >=0 &&
            nSupply             >=0 &&
            nSupply             < PEG_SIZE &&
            nSupplyNext         >=0 &&
            nSupplyNext         < PEG_SIZE &&
            nSupplyNextNext     >=0 &&
            nSupplyNextNext     < PEG_SIZE &&
            nShift              >= 0 &&
            (nSupply+nShift)    < PEG_SIZE &&
            nShiftLastPart      >= 0 &&
            nShiftLastTotal     >= 0;
}

bool CPegLevel::Pack(CDataStream & fout) const {
    fout << uint8_t(2);
    fout << nCycle;
    fout << nCyclePrev;
    fout << nBuffer;
    fout << nSupply;
    fout << nSupplyNext;
    fout << nSupplyNextNext;
    fout << nShift;
    fout << nShiftLastPart;     // to distribute (part)
    fout << nShiftLastTotal;    // to distribute (total)
    return true;
}

bool CPegLevel::Unpack(CDataStream & finp) {
    try {
        finp >> nVersion;
        if (nVersion == 1) {
            finp >> nCycle;
            finp >> nCyclePrev;
            finp >> nSupply;
            finp >> nSupplyNext;
            finp >> nSupplyNextNext;
            finp >> nShift;
            finp >> nShiftLastPart;     // to distribute (part)
            finp >> nShiftLastTotal;    // to distribute (total)
            nBuffer = 3;
        } else if (nVersion >1) {
            finp >> nCycle;
            finp >> nCyclePrev;
            finp >> nBuffer;
            finp >> nSupply;
            finp >> nSupplyNext;
            finp >> nSupplyNextNext;
            finp >> nShift;
            finp >> nShiftLastPart;     // to distribute (part)
            finp >> nShiftLastTotal;    // to distribute (total)
        }
    }
    catch (std::exception &) {
        return false;
    }
    return true;
}

std::string CPegLevel::ToString() const {
    CDataStream fout(SER_NETWORK, CLIENT_VERSION);
    Pack(fout);
    return HexStr(fout.begin(), fout.end());
}

bool operator<(const CPegLevel &a, const CPegLevel &b) {
    int16_t nSupplyA = a.nSupply+a.nShift;
    int16_t nSupplyB = b.nSupply+b.nShift;
    if (nSupplyA < nSupplyB) return true;
    if (nSupplyA > nSupplyB) return false;

    multiprecision::uint128_t nShiftLastPartA(a.nShiftLastPart);
    multiprecision::uint128_t nShiftLastPartB(b.nShiftLastPart);
    multiprecision::uint128_t nShiftLastTotalA(a.nShiftLastTotal);
    multiprecision::uint128_t nShiftLastTotalB(b.nShiftLastTotal);

    multiprecision::uint128_t nPartNormA = nShiftLastPartA * nShiftLastTotalB;
    multiprecision::uint128_t nPartNormB = nShiftLastPartB * nShiftLastTotalA;

    return nPartNormA < nPartNormB;
}

bool operator==(const CPegLevel &a, const CPegLevel &b) {
    int16_t nSupplyA = a.nSupply+a.nShift;
    int16_t nSupplyB = b.nSupply+b.nShift;
    if (nSupplyA != nSupplyB) return false;

    multiprecision::uint128_t nShiftLastPartA(a.nShiftLastPart);
    multiprecision::uint128_t nShiftLastPartB(b.nShiftLastPart);
    multiprecision::uint128_t nShiftLastTotalA(a.nShiftLastTotal);
    multiprecision::uint128_t nShiftLastTotalB(b.nShiftLastTotal);

    multiprecision::uint128_t nPartNormA = nShiftLastPartA * nShiftLastTotalB;
    multiprecision::uint128_t nPartNormB = nShiftLastPartB * nShiftLastTotalA;

    return nPartNormA == nPartNormB;
}

bool operator!=(const CPegLevel &a, const CPegLevel &b) {
    return !operator==(a,b);
}

