// Copyright (c) 2018 yshurik
//
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The use in another cyptocurrency project the code is licensed under
// Jelurida Public License (JPL). See https://www.jelurida.com/resources/jpl

#include "pegdata.h"

#include <map>
#include <set>
#include <cstdint>

#include <zconf.h>
#include <zlib.h>

bool CPegLevel::Unpack1(CDataStream & finp) {
    try {
        finp >> nCycle;
        finp >> nCyclePrev;
        finp >> nSupply;
        finp >> nSupplyNext;
        finp >> nSupplyNextNext;
        finp >> nShift;
        finp >> nShiftLastPart;     // to distribute (part)
        finp >> nShiftLastTotal;    // to distribute (total)
        nBuffer = 3;
    }
    catch (std::exception &) {
        return false;
    }
    return true;
}

bool CFractions::Unpack2(CDataStream& inp)
{
    uint32_t nSerFlags = 0;
    inp >> nSerFlags;
    inp >> nLockTime;
    if (nSerFlags & SER_VALUE) {
        nFlags = nSerFlags | VALUE;
        inp >> f[0];
    }
    else if (nSerFlags & SER_ZDELTA) {
        unsigned long zlen = 0;
        inp >> zlen;

        if (zlen>(2*PEG_SIZE*sizeof(int64_t))) {
            // data are broken, no read
            return false;
        }

        unsigned char zinp[2*PEG_SIZE*sizeof(int64_t)];
        unsigned long n = PEG_SIZE*sizeof(int64_t);
        auto ser = reinterpret_cast<char *>(zinp);
        inp.read(ser, zlen);

        int64_t deltas[PEG_SIZE];
        auto src = reinterpret_cast<const unsigned char *>(ser);
        auto dst = reinterpret_cast<unsigned char *>(deltas);
        int res = ::uncompress(dst, &n, src, zlen);
        if (res != Z_OK) {
            // data are broken, can not uncompress
            return false;
        }
        FromDeltas(deltas);
        nFlags = nSerFlags | STD;
    }
    else if (nSerFlags & SER_RAW) {
        auto ser = reinterpret_cast<char *>(f.get());
        inp.read(ser, PEG_SIZE*sizeof(int64_t));
        nFlags = nSerFlags | STD;
    }
    nFlags &= SER_MASK;

    inp >> sReturnAddr;

    return true;
}

bool CFractions::Unpack1(CDataStream& inp)
{
    uint32_t nSerFlags = 0;
    inp >> nSerFlags;
    inp >> nLockTime;
    if (nSerFlags & SER_VALUE) {
        nFlags = nSerFlags | VALUE;
        inp >> f[0];
    }
    else if (nSerFlags & SER_ZDELTA) {
        unsigned long zlen = 0;
        inp >> zlen;

        if (zlen>(2*PEG_SIZE*sizeof(int64_t))) {
            // data are broken, no read
            return false;
        }

        unsigned char zinp[2*PEG_SIZE*sizeof(int64_t)];
        unsigned long n = PEG_SIZE*sizeof(int64_t);
        auto ser = reinterpret_cast<char *>(zinp);
        inp.read(ser, zlen);

        int64_t deltas[PEG_SIZE];
        auto src = reinterpret_cast<const unsigned char *>(ser);
        auto dst = reinterpret_cast<unsigned char *>(deltas);
        int res = ::uncompress(dst, &n, src, zlen);
        if (res != Z_OK) {
            // data are broken, can not uncompress
            return false;
        }
        FromDeltas(deltas);
        nFlags = nSerFlags | STD;
    }
    else if (nSerFlags & SER_RAW) {
        auto ser = reinterpret_cast<char *>(f.get());
        inp.read(ser, PEG_SIZE*sizeof(int64_t));
        nFlags = nSerFlags | STD;
    }
    nFlags &= SER_MASK;
    return true;
}

bool CPegData::Unpack2(CDataStream & finp) {
    try {
        if (!fractions.Unpack2(finp)) return false;
        if (!peglevel.Unpack1(finp)) return false;
        finp >> nReserve;
        finp >> nLiquid;

        // match total
        if ((nReserve+nLiquid) != fractions.Total()) return false;

        // validate liquid/reserve match peglevel
        int nSupplyEffective = peglevel.nSupply+peglevel.nShift;
        bool fPartial = peglevel.nShiftLastPart >0 && peglevel.nShiftLastTotal >0;
        if (fPartial) {
            nSupplyEffective++;
            int64_t nLiquidWithoutPartial = fractions.High(nSupplyEffective);
            int64_t nReserveWithoutPartial = fractions.Low(nSupplyEffective-1);
            if (nLiquid < nLiquidWithoutPartial) return false;
            if (nReserve < nReserveWithoutPartial) return false;
        }
        else {
            int64_t nLiquidCalc = fractions.High(nSupplyEffective);
            int64_t nReserveCalc = fractions.Low(nSupplyEffective);
            if (nLiquid != nLiquidCalc) return false;
            if (nReserve != nReserveCalc) return false;
        }
    }
    catch (std::exception &) {
        return false;
    }

    fractions = fractions.Std();
    return true;
}

bool CPegData::Unpack1(CDataStream & finp) {
    try {
        if (!fractions.Unpack1(finp)) return false;
        if (!peglevel.Unpack1(finp)) return false;
        finp >> nReserve;
        finp >> nLiquid;

        // match total
        if ((nReserve+nLiquid) != fractions.Total()) return false;

        // validate liquid/reserve match peglevel
        int nSupplyEffective = peglevel.nSupply+peglevel.nShift;
        bool fPartial = peglevel.nShiftLastPart >0 && peglevel.nShiftLastTotal >0;
        if (fPartial) {
            nSupplyEffective++;
            int64_t nLiquidWithoutPartial = fractions.High(nSupplyEffective);
            int64_t nReserveWithoutPartial = fractions.Low(nSupplyEffective-1);
            if (nLiquid < nLiquidWithoutPartial) return false;
            if (nReserve < nReserveWithoutPartial) return false;
        }
        else {
            int64_t nLiquidCalc = fractions.High(nSupplyEffective);
            int64_t nReserveCalc = fractions.Low(nSupplyEffective);
            if (nLiquid != nLiquidCalc) return false;
            if (nReserve != nReserveCalc) return false;
        }
    }
    catch (std::exception &) {
        return false;
    }

    fractions = fractions.Std();
    return true;
}
