// Copyright (c) 2019 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp>

#include "base58.h"
#include "rpcserver.h"
#include "txdb.h"
#include "init.h"
#include "main.h"
#include "net.h"
#include "keystore.h"
#include "wallet.h"

#include "pegops.h"
#include "pegdata.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;

void printpegshift(const CFractions & frPegShift,
                   const CPegLevel & peglevel,
                   Object & result)
{
    int64_t nValue = frPegShift.Positive(nullptr).Total();
    CFractions frLiquid = frPegShift.HighPart(peglevel, nullptr);
    CFractions frReserve = frPegShift.LowPart(peglevel, nullptr);
    
    int64_t nLiquidNegative = 0;
    int64_t nLiquidPositive = 0;
    frLiquid.Negative(&nLiquidNegative);
    frLiquid.Positive(&nLiquidPositive);
    
    int64_t nReserveNegative = 0;
    int64_t nReservePositive = 0;
    frReserve.Negative(&nReserveNegative);
    frReserve.Positive(&nReservePositive);
    
    int64_t nLiquidPart = std::min(nLiquidPositive, -nLiquidNegative);
    int64_t nReservePart = std::min(nReservePositive, -nReserveNegative);

    result.push_back(Pair("pegshift_absvalue", nValue));
    result.push_back(Pair("pegshift_liquidpart", nLiquidPart));
    result.push_back(Pair("pegshift_reservepart", nReservePart));

    CPegData pegdata;
    pegdata.fractions = frPegShift;
    pegdata.peglevel = peglevel;
    pegdata.nLiquid = frPegShift.High(peglevel);
    pegdata.nReserve = frPegShift.Low(peglevel);
    
    result.push_back(Pair("pegshift_pegdata", pegdata.ToString()));
}

void printpeglevel(const CPegLevel & peglevel,
                   Object & result)
{
    result.push_back(Pair("peglevel", peglevel.ToString()));
    
    Object info;
    info.push_back(Pair("cycle", peglevel.nCycle));
    info.push_back(Pair("peg", peglevel.nSupply));
    info.push_back(Pair("pegnext", peglevel.nSupplyNext));
    info.push_back(Pair("pegnextnext", peglevel.nSupplyNextNext));
    info.push_back(Pair("shift", peglevel.nShift));
    info.push_back(Pair("shiftlastpart", peglevel.nShiftLastPart));
    info.push_back(Pair("shiftlasttotal", peglevel.nShiftLastTotal));
    result.push_back(Pair("peglevel_info", info));
}

void printpegbalance(const CPegData & pegdata,
                     Object & result,
                     string prefix)
{
    int64_t nValue      = pegdata.fractions.Total();
    int64_t nNChange    = pegdata.fractions.NChange(pegdata.peglevel);

    int16_t nValueHli   = pegdata.fractions.HLI();
    int16_t nLiquidHli  = pegdata.fractions.HighPart(pegdata.peglevel, nullptr).HLI();
    int16_t nReserveHli = pegdata.fractions.LowPart(pegdata.peglevel, nullptr).HLI();

    result.push_back(Pair(prefix+"value", nValue));
    result.push_back(Pair(prefix+"value_hli", nValueHli));
    result.push_back(Pair(prefix+"liquid", pegdata.nLiquid));
    result.push_back(Pair(prefix+"liquid_hli", nLiquidHli));
    result.push_back(Pair(prefix+"reserve", pegdata.nReserve));
    result.push_back(Pair(prefix+"reserve_hli", nReserveHli));
    result.push_back(Pair(prefix+"nchange", nNChange));
    result.push_back(Pair(prefix+"pegdata", pegdata.ToString()));
}

void printpegtxout(const CPegData & pegdata,
                   Object & result,
                   string prefix)
{
    int64_t nValue      = pegdata.fractions.Total();

    // network peg in next cycle
    int pegn_liquid = pegdata.peglevel.nSupplyNext - pegdata.peglevel.nBuffer;
    int pegn_reserve = pegdata.peglevel.nSupplyNext;
    
    int64_t nLiquid = pegdata.fractions.High(pegn_liquid);
    int64_t nReserve = pegdata.fractions.Low(pegn_reserve);
    
    int16_t nValueHli   = pegdata.fractions.HLI();
    int16_t nLiquidHli  = pegdata.fractions.HighPart(pegn_liquid, nullptr).HLI();
    int16_t nReserveHli = pegdata.fractions.LowPart(pegn_reserve, nullptr).HLI();

    result.push_back(Pair(prefix+"value", nValue));
    result.push_back(Pair(prefix+"value_hli", nValueHli));
    result.push_back(Pair(prefix+"nliquid", nLiquid));
    result.push_back(Pair(prefix+"nliquid_hli", nLiquidHli));
    result.push_back(Pair(prefix+"nreserve", nReserve));
    result.push_back(Pair(prefix+"nreserve_hli", nReserveHli));
    result.push_back(Pair(prefix+"pegdata", pegdata.ToString()));
}

