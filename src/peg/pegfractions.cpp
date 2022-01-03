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
#include <utility>
#include <algorithm>
#include <type_traits>

#include <boost/multiprecision/cpp_int.hpp>

#include "util.h"

#include <zconf.h>
#include <zlib.h>

using namespace std;
using namespace boost;

CFractions::CFractions()
    :nFlags(VALUE)
    ,f(new int64_t[PEG_SIZE])
{
    f[0] = 0; // fast init first item
}
CFractions::CFractions(int64_t value, uint32_t flags)
    :nFlags(flags)
    ,f(new int64_t[PEG_SIZE])
{
    if (flags & VALUE)
        f[0] = value; // fast init first item
    else if (flags & STD) {
        f[0] = value;
        nFlags = VALUE;
        ToStd();
        nFlags = flags;
    }
    else {
        assert(0);
    }
}
CFractions::CFractions(const CFractions & o)
    :nFlags(o.nFlags)
    ,nLockTime(o.nLockTime)
    ,sReturnAddr(o.sReturnAddr)
    ,f(new int64_t[PEG_SIZE])
{
    for(int i=0; i< PEG_SIZE; i++) {
        f[i] = o.f[i];
    }
}

CFractions& CFractions::operator=(const CFractions& o)
{
    nFlags = o.nFlags;
    nLockTime = o.nLockTime;
    sReturnAddr = o.sReturnAddr;
    for(int i=0; i< PEG_SIZE; i++) {
        f[i] = o.f[i];
    }
    return *this;
}

void CFractions::ToDeltas(int64_t* deltas) const
{
    int64_t fp = 0;
    for(int i=0; i<PEG_SIZE; i++) {
        if (i==0) {
            fp = deltas[0] = f[0];
            continue;
        }
        deltas[i] = f[i]-fp*(PEG_RATE-1)/PEG_RATE;
        fp = f[i];
    }
}

void CFractions::FromDeltas(const int64_t* deltas)
{
    int64_t fp = 0;
    for(int i=0; i<PEG_SIZE; i++) {
        if (i==0) {
            fp = f[0] = deltas[0];
            continue;
        }
        f[i] = deltas[i]+fp*(PEG_RATE-1)/PEG_RATE;
        fp = f[i];
    }
}

bool CFractions::Pack(CDataStream& out, unsigned long* report_len, bool compress) const
{
    if (nFlags & VALUE) {
        if (report_len) *report_len = sizeof(int64_t);
        out << nVersion;
        out << uint32_t(nFlags | SER_VALUE);
        out << nLockTime;
        out << sReturnAddr;
        out << f[0];
    } else if (compress) {
        int64_t deltas[PEG_SIZE];
        ToDeltas(deltas);

        int zlevel = 9;
        unsigned char zout[2*PEG_SIZE*sizeof(int64_t)];
        unsigned long n = PEG_SIZE*sizeof(int64_t);
        unsigned long zlen = PEG_SIZE*2*sizeof(int64_t);
        auto src = reinterpret_cast<const unsigned char *>(deltas);
        int res = ::compress2(zout, &zlen, src, n, zlevel);
        if (res == Z_OK) {
            if (report_len) *report_len = zlen;
            out << nVersion;
            out << uint32_t(nFlags | SER_ZDELTA);
            out << nLockTime;
            out << sReturnAddr;
            auto ser = reinterpret_cast<const char *>(zout);
            out << zlen;
            out.write(ser, zlen);
        }
        else {
            if (report_len) *report_len = PEG_SIZE*sizeof(int64_t);
            out << nVersion;
            out << uint32_t(nFlags | SER_RAW);
            out << nLockTime;
            out << sReturnAddr;
            auto ser = reinterpret_cast<const char *>(f.get());
            out.write(ser, PEG_SIZE*sizeof(int64_t));
        }
    } else {
        if (report_len) *report_len = PEG_SIZE*sizeof(int64_t);
        out << nVersion;
        out << uint32_t(nFlags | SER_RAW);
        out << nLockTime;
        out << sReturnAddr;
        auto ser = reinterpret_cast<const char *>(f.get());
        out.write(ser, PEG_SIZE*sizeof(int64_t));
    }
    return true;
}

bool CFractions::Unpack(CDataStream& inp)
{
    uint32_t nSerFlags = 0;
    inp >> nVersion;
    inp >> nSerFlags;
    inp >> nLockTime;
    inp >> sReturnAddr;

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

CFractions CFractions::Std() const
{
    if ((nFlags & VALUE) ==0)
        return *this;

    CFractions fstd;
    fstd.sReturnAddr = sReturnAddr;
    fstd.nLockTime = nLockTime;
    fstd.nFlags = nFlags;
    fstd.nFlags &= ~uint32_t(VALUE);
    fstd.nFlags |= STD;

    int64_t v = f[0];
    for(int i=0;i<PEG_SIZE;i++) {
        if (i == PEG_SIZE-1) {
            fstd.f[i] = v;
            break;
        }
        int64_t frac = v/PEG_RATE;
        fstd.f[i] = frac;
        v -= frac;
    }
    return fstd;
}

bool CFractions::IsPositive() const
{
    if (nFlags & VALUE)
        return true;

    for(int i=0;i<PEG_SIZE;i++) {
        if (f[i] <0)
            return false;
    }
    return true;
}

bool CFractions::IsNegative() const
{
    if (nFlags & VALUE)
        return false;

    for(int i=0;i<PEG_SIZE;i++) {
        if (f[i] >0)
            return false;
    }
    return true;
}

int64_t CFractions::Total() const
{
    int64_t nValue =0;
    if (nFlags & VALUE)
        return f[0];

    for(int i=0;i<PEG_SIZE;i++) {
        nValue += f[i];
    }
    return nValue;
}

int64_t CFractions::Low(int supply) const
{
    int64_t nValue =0;
    if (nFlags & VALUE)
        return Std().Low(supply);

    for(int i=0;i<supply;i++) {
        nValue += f[i];
    }
    return nValue;
}

int64_t CFractions::High(int supply) const
{
    int64_t nValue =0;
    if (nFlags & VALUE)
        return Std().High(supply);

    for(int i=supply;i<PEG_SIZE;i++) {
        nValue += f[i];
    }
    return nValue;
}

int64_t CFractions::Low(const CPegLevel & peglevel) const
{
    int64_t nValue =0;
    if (nFlags & VALUE)
        return Std().Low(peglevel);

    int to = peglevel.nSupply + peglevel.nShift;
    if (to <0) return 0;
    if (to >= PEG_SIZE) return 0;

    if (peglevel.nShiftLastPart >0 &&
        peglevel.nShiftLastTotal >0) {
        // partial value to use
        int64_t v = f[to];
        int64_t vpart = ::RatioPart(v,
                                    peglevel.nShiftLastPart,
                                    peglevel.nShiftLastTotal);
        if (vpart < v) vpart++; // better rounding
        nValue += vpart;
    }

    for(int i=0;i<to;i++) {
        nValue += f[i];
    }

    return nValue;
}

int64_t CFractions::High(const CPegLevel & peglevel) const
{
    int64_t nValue =0;
    if (nFlags & VALUE)
        return Std().High(peglevel);

    int from = peglevel.nSupply + peglevel.nShift;
    if (from <0) return 0;
    if (from >= PEG_SIZE) return 0;

    if (peglevel.nShiftLastPart >0 &&
        peglevel.nShiftLastTotal >0) {
        // partial value to use
        int64_t v = f[from];
        int64_t vpart = ::RatioPart(v,
                                    peglevel.nShiftLastPart,
                                    peglevel.nShiftLastTotal);
        if (vpart < v) vpart++; // better rounding
        nValue += (v - vpart);
        from++;
    }

    for(int i=from;i<PEG_SIZE;i++) {
        nValue += f[i];
    }

    return nValue;
}

int64_t CFractions::NChange(const CPegLevel & peglevel) const
{
    CPegLevel peglevel_next = peglevel;
    peglevel_next.nSupply = peglevel_next.nSupplyNext;
    peglevel_next.nSupplyNext = peglevel_next.nSupplyNextNext;

    int64_t nValueSrc = High(peglevel);
    int64_t nValueDst = High(peglevel_next);
    return nValueDst - nValueSrc;
}

int64_t CFractions::NChange(int src_supply, int dst_supply) const
{
    int64_t nValueSrc = High(src_supply);
    int64_t nValueDst = High(dst_supply);
    return nValueDst - nValueSrc;
}

int16_t CFractions::HLI() const
{
    if (nFlags & VALUE)
        return Std().HLI();

    int64_t half = 0;
    int64_t total = Total();
    if (total ==0) {
        return 0;
    }
    for(int16_t i=0;i<PEG_SIZE;i++) {
        half += f[i];
        if (half > total/2) {
            return i;
        }
    }
    return 0;
}

void CFractions::ToStd()
{
    if ((nFlags & VALUE) == 0)
        return;

    nFlags &= ~uint32_t(VALUE);
    nFlags |= STD;

    int64_t v = f[0];
    for(int i=0;i<PEG_SIZE;i++) {
        if (i == PEG_SIZE-1) {
            f[i] = v;
            break;
        }
        int64_t frac = v/PEG_RATE;
        f[i] = frac;
        v -= frac;
    }
}

CFractions CFractions::Positive(int64_t* total) const
{
    if ((nFlags & STD) == 0) {
        return Std().Positive(total);
    }
    CFractions frPositive(0, CFractions::STD);
    for(int i=0; i<PEG_SIZE; i++) {
        if (f[i] <=0) continue;
        frPositive.f[i] = f[i];
        if (total) *total += f[i];
    }
    return frPositive;
}
CFractions CFractions::Negative(int64_t* total) const
{
    if ((nFlags & STD) == 0) {
        return Std().Negative(total);
    }
    CFractions frNegative(0, CFractions::STD);
    for(int i=0; i<PEG_SIZE; i++) {
        if (f[i] >=0) continue;
        frNegative.f[i] = f[i];
        if (total) *total += f[i];
    }
    return frNegative;
}


CFractions CFractions::LowPart(int supply, int64_t* total) const
{
    if ((nFlags & STD) == 0) {
        return Std().LowPart(supply, total);
    }
    CFractions frLowPart(0, CFractions::STD);
    for(int i=0; i<supply; i++) {
        if (total) *total += f[i];
        frLowPart.f[i] += f[i];
    }
    return frLowPart;
}
CFractions CFractions::HighPart(int supply, int64_t* total) const
{
    if ((nFlags & STD) == 0) {
        return Std().HighPart(supply, total);
    }
    CFractions frHighPart(0, CFractions::STD);
    for(int i=supply; i<PEG_SIZE; i++) {
        if (total) *total += f[i];
        frHighPart.f[i] += f[i];
    }
    return frHighPart;
}

CFractions CFractions::LowPart(const CPegLevel & peglevel, int64_t* total) const
{
    if ((nFlags & STD) == 0) {
        return Std().LowPart(peglevel, total);
    }

    CFractions frLowPart(0, CFractions::STD);

    int to = peglevel.nSupply + peglevel.nShift;
    if (to >=0 &&
            to <PEG_SIZE &&
            peglevel.nShiftLastPart >0 &&
            peglevel.nShiftLastTotal >0) {
        // partial value to use
        int64_t v = f[to];
        int64_t vpart = ::RatioPart(v,
                                    peglevel.nShiftLastPart,
                                    peglevel.nShiftLastTotal);
        if (vpart < v) vpart++;
        frLowPart.f[to] = vpart;
        if (total) *total += vpart;
    }

    for(int i=0; i<to; i++) {
        if (total) *total += f[i];
        frLowPart.f[i] = f[i];
    }
    return frLowPart;
}
CFractions CFractions::HighPart(const CPegLevel & peglevel, int64_t* total) const
{
    if ((nFlags & STD) == 0) {
        return Std().HighPart(peglevel, total);
    }

    CFractions frHighPart(0, CFractions::STD);

    int from = peglevel.nSupply + peglevel.nShift;
    if (from >=0 &&
            from <PEG_SIZE &&
            peglevel.nShiftLastPart >0 &&
            peglevel.nShiftLastTotal >0) {
        // partial value to use
        int64_t v = f[from];
        int64_t vpart = ::RatioPart(v,
                                    peglevel.nShiftLastPart,
                                    peglevel.nShiftLastTotal);
        if (vpart < v) vpart++;
        frHighPart.f[from] = (v - vpart);
        if (total) *total += (v - vpart);
        from++;
    }

    for(int i=from; i<PEG_SIZE; i++) {
        if (total) *total += f[i];
        frHighPart.f[i] = f[i];
    }
    return frHighPart;
}

CFractions CFractions::MidPart(const CPegLevel & peglevel_low,
                               const CPegLevel & peglevel_high) const
{
    int from = peglevel_low.nSupply + peglevel_low.nShift;
    int to = peglevel_high.nSupply + peglevel_high.nShift;

    if (from != to) {
        return HighPart(peglevel_low, nullptr).LowPart(peglevel_high, nullptr);
    }

    // same supply, different partial ratio
    CFractions mid(0, STD);
    int one = from;
    int64_t v = f[one];
    int64_t vone = f[one];

    if (peglevel_low.nShiftLastPart >0 &&
        peglevel_low.nShiftLastTotal >0) {
        int64_t vpart = ::RatioPart(vone,
                                    peglevel_low.nShiftLastPart,
                                    peglevel_low.nShiftLastTotal);
        v -= vpart;
    }

    if (peglevel_high.nShiftLastPart >0 &&
        peglevel_high.nShiftLastTotal >0) {
        int64_t vpart = ::RatioPart(vone,
                                    peglevel_high.nShiftLastPart,
                                    peglevel_high.nShiftLastTotal);
        if (vpart < vone) vpart++;
        v -= (vone - vpart);
    }

    mid.f[from] = v;
    return mid;
}

/** Take a part as ration part/total where part is also value (sum fraction)
 *  Returned fractions are also adjusted for part for rounding differences.
 *  #NOTE8
 */
CFractions CFractions::RatioPart(int64_t nPartValue) const {
    if ((nFlags & STD) == 0) {
        return Std().RatioPart(nPartValue);
    }
    int64_t nTotalValue = Total();
    int64_t nPartValueSum = 0;
    CFractions fPart(0, CFractions::STD);

    if (nPartValue == 0 && nTotalValue == 0)
        return fPart;
    if (nPartValue == 0)
        return fPart;
    if (nPartValue > nTotalValue)
        return Std();

    int adjust_from = PEG_SIZE;
    for(int i=0; i<PEG_SIZE; i++) {
        int64_t v = f[i];

        if (v != 0 && i < adjust_from) {
            adjust_from = i;
        }

        bool has_overflow = false;
        if (std::is_same<int64_t,long>()) {
            long m_test;
            has_overflow = __builtin_smull_overflow(v, nPartValue, &m_test);
        } else if (std::is_same<int64_t,long long>()) {
            long long m_test;
            has_overflow = __builtin_smulll_overflow(v, nPartValue, &m_test);
        } else {
            assert(0); // todo: compile error
        }

        if (has_overflow) {
            multiprecision::uint128_t v128(v);
            multiprecision::uint128_t part128(nPartValue);
            multiprecision::uint128_t f128 = (v128*part128)/nTotalValue;
            fPart.f[i] = f128.convert_to<int64_t>();
        }
        else {
            fPart.f[i] = (v*nPartValue)/nTotalValue;
        }

        nPartValueSum += fPart.f[i];
    }

    if (nPartValueSum == nPartValue)
        return fPart;
    if (nPartValueSum > nPartValue)
        return fPart; // todo:peg: validate if possible

    int idx = adjust_from;
    int64_t nAdjustValue = nPartValue - nPartValueSum;
    while(nAdjustValue >0) {
        // todo:peg: review all possible cases if rounding mismatch with adjust_from
        if (fPart.f[idx] < f[idx]) {
            nAdjustValue--;
            fPart.f[idx]++;
        }
        idx++;
        if (idx >= PEG_SIZE) {
            idx = adjust_from;
        }
    }

    return fPart;
}

/** Take a part as ration part/total where part is also value (sum fraction)
 *  Add taken part into destination fractions. Adjusted from adjust_from.
 *  Returns not completed amount (if source("this") has no enough)
 *  #NOTE8
 */
int64_t CFractions::MoveRatioPartTo(int64_t nValueToMove,
                                    CFractions& b)
{
    int64_t nTotalValue = Total();
    int64_t nPartValue = nValueToMove;

    if (nTotalValue == 0)
        return nValueToMove;
    if (nValueToMove == 0)
        return 0;

    if ((nFlags & STD) == 0)
        ToStd();
    if ((b.nFlags & STD) == 0)
        b.ToStd();

    if (nPartValue >= nTotalValue) {
        nPartValue = nTotalValue;
        b += *this; // move all
        for(int i=0; i<PEG_SIZE; i++) f[i] = 0; // taken all
        return nValueToMove - nPartValue;
    }

    int64_t nPartValueSum = 0;
    int adjust_from = PEG_SIZE;
    for(int i=0; i<PEG_SIZE; i++) {
        int64_t v = f[i];

        if (v != 0 && i < adjust_from) {
            adjust_from = i;
        }

        bool has_overflow = false;
        if (std::is_same<int64_t,long>()) {
            long m_test;
            has_overflow = __builtin_smull_overflow(v, nPartValue, &m_test);
        } else if (std::is_same<int64_t,long long>()) {
            long long m_test;
            has_overflow = __builtin_smulll_overflow(v, nPartValue, &m_test);
        } else {
            assert(0); // todo: compile error
        }

        int64_t vp = 0;

        if (has_overflow) {
            multiprecision::uint128_t v128(v);
            multiprecision::uint128_t part128(nPartValue);
            multiprecision::uint128_t f128 = (v128*part128)/nTotalValue;
            vp = f128.convert_to<int64_t>();
        }
        else {
            vp = (v*nPartValue)/nTotalValue;
        }

        nPartValueSum += vp;
        b.f[i] += vp;
        f[i] -= vp;
    }

    if (nPartValueSum == nPartValue)
        return 0;
    if (nPartValueSum > nPartValue)
        return 0; // todo:peg: validate if possible

    int idx = adjust_from;
    int64_t nAdjustValue = nPartValue - nPartValueSum;
    while(nAdjustValue >0) {
        if (f[idx] >0) {
            nAdjustValue--;
            b.f[idx]++;
            f[idx]--;
        }
        idx++;
        if (idx >= PEG_SIZE) {
            idx = adjust_from;
        }
    }

    return 0;
}

CFractions& CFractions::operator+=(const CFractions& b)
{
    if ((b.nFlags & STD) == 0) {
        return operator+=(b.Std());
    }
    if ((nFlags & STD) == 0) {
        ToStd();
    }
    for(int i=0; i<PEG_SIZE; i++) {
        f[i] += b.f[i];
    }
    return *this;
}

CFractions& CFractions::operator-=(const CFractions& b)
{
    if ((b.nFlags & STD) == 0) {
        return operator-=(b.Std());
    }
    if ((nFlags & STD) == 0) {
        ToStd();
    }
    for(int i=0; i<PEG_SIZE; i++) {
        f[i] -= b.f[i];
    }
    return *this;
}

CFractions CFractions::operator&(const CFractions& b) const
{
    CFractions a = *this;
    for(int i=0; i<PEG_SIZE; i++) {
        int64_t va = a.f[i];
        int64_t vb = b.f[i];
        if      (va >=0 && vb >=0) a.f[i] = std::min(va, vb);
        else if (va >=0 && vb < 0) a.f[i] = 0;
        else if (va < 0 && vb >=0) a.f[i] = 0;
        else if (va < 0 && vb < 0) a.f[i] = std::max(va, vb);
    }
    return a;
}

CFractions CFractions::operator-() const
{
    CFractions a = *this;
    for(int i=0; i<PEG_SIZE; i++) {
        int64_t va = a.f[i];
        a.f[i] = -va;
    }
    return a;
}

double CFractions::Distortion(const CFractions& b) const
{
    int64_t nTotalA = Total();
    int64_t nTotalB = b.Total();

    if (nTotalA == nTotalB) {

        if (nTotalA == 0) {
            return 0;
        }

        int64_t nDiff = 0;

        for(int i=0; i<PEG_SIZE; i++) {
            int64_t va = f[i];
            int64_t vb = b.f[i];
            if (va > vb) {
                nDiff += (va - vb);
            }
        }

        return double(nDiff) / double(nTotalA);
    }

    else if (nTotalA < nTotalB) {
        if (nTotalA == 0) {
            return nTotalB; // does not make sense to scale
        }

        CFractions b1 = b.RatioPart(nTotalA);
        return Distortion(b1);
    }

    else if (nTotalA > nTotalB) {
        if (nTotalB == 0) {
            return nTotalA; // does not make sense to scale
        }

        CFractions a1 = RatioPart(nTotalB);
        return a1.Distortion(b);
    }

    return 0;
}

