// Copyright (c) 2018 yshurik
//
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The use in another cyptocurrency project the code is licensed under
// Jelurida Public License (JPL). See https://www.jelurida.com/resources/jpl

#include "pegopsp.h"
#include "pegdata.h"
#include "pegstd.h"

#include <map>
#include <set>
#include <cstdint>
#include <utility>
#include <algorithm>
#include <type_traits>
#include <iostream>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <zconf.h>
#include <zlib.h>

#include <base58.h>
#include <script.h>
#include <keystore.h>
#include <main.h>

using namespace std;
using namespace boost;

namespace pegops {

// API calls

bool getpeglevel(
        int                 nCycleNow,
        int                 nCyclePrev,
        int                 nBuffer,
        int                 nPegNow,
        int                 nPegNext,
        int                 nPegNextNext,
        const CPegData &    pdExchange,
        const CPegData &    pdPegShift,

        CPegLevel &     peglevel,
        CPegData &      pdPegPool,
        std::string &   sErr)
{
    sErr.clear();

    pdPegPool = CPegData();
    // exchange peglevel, should have +buffer values
    pdPegPool.peglevel = CPegLevel(nCycleNow,
                                   nCyclePrev,
                                   nBuffer,
                                   nPegNow,
                                   nPegNext,
                                   nPegNextNext,
                                   pdExchange.fractions,
                                   pdPegShift.fractions);

    int nPegEffective = pdPegPool.peglevel.nSupply + pdPegPool.peglevel.nShift;
    // pool to include both, liquidity and (may) last partial reserve fractions
    pdPegPool.fractions = pdExchange.fractions.HighPart(nPegEffective, nullptr);

    int64_t nPegPoolValue = pdPegPool.fractions.Total();
    pdPegPool.nReserve = pdPegPool.peglevel.nShiftLastPart;
    pdPegPool.nLiquid = nPegPoolValue - pdPegPool.nReserve;
    peglevel = pdPegPool.peglevel;
    return true;
}

bool updatepegbalances(
        CPegData &          pdBalance,
        CPegData &          pdPegPool,
        const CPegLevel &   peglevelNew,

        std::string &   sErr)
{
    sErr.clear();

    if (pdPegPool.peglevel.nCycle != peglevelNew.nCycle) {
        sErr = "PegPool has other cycle than peglevel";
        return false;
    }

    if (pdBalance.peglevel.nCycle == peglevelNew.nCycle) { // already up-to-dated
        sErr = "Already up-to-dated";
        return true;
    }

    if (pdBalance.peglevel.nCycle > peglevelNew.nCycle) {
        sErr = "Balance has greater cycle than peglevel";
        return false;
    }

    int64_t nValue = pdBalance.fractions.Total();

    if (nValue != 0 && /*allow cross-cycle update as zero to take from pool*/
        pdBalance.peglevel.nCycle != 0 && /*new users with empty*/
        pdBalance.peglevel.nCycle != peglevelNew.nCyclePrev) {
        std::stringstream ss;
        ss << "Mismatch for peglevel_new.nCyclePrev "
           << peglevelNew.nCyclePrev
           << " vs peglevel_old.nCycle "
           << pdBalance.peglevel.nCycle;
        sErr = ss.str();
        return false;
    }

    CFractions frLiquid(0, CFractions::STD);
    CFractions frReserve(0, CFractions::STD);

    int64_t nReserve = 0;
    // current part of balance turns to reserve
    // the balance is to be updated at previous cycle
    frReserve = pdBalance.fractions.LowPart(peglevelNew, &nReserve);

    frLiquid = CFractions(0, CFractions::STD);

    if (nReserve != frReserve.Total()) {
        std::stringstream ss;
        ss << "Reserve mimatch on LowPart " << frReserve.Total() << " vs " << nValue;
        sErr = ss.str();
        return false;
    }

    // if partial last reserve fraction then take reserve from this idx
    int nSupplyEffective = peglevelNew.nSupply + peglevelNew.nShift;
    if (nSupplyEffective <0 &&
        nSupplyEffective >=PEG_SIZE) {
        std::stringstream ss;
        ss << "Effective supply out of range " << nSupplyEffective;
        sErr = ss.str();
        return false;
    }
    bool fPartial = peglevelNew.nShiftLastPart >0 && peglevelNew.nShiftLastTotal >0;

    int nLastIdx = nSupplyEffective;
    if (fPartial) {

        int64_t nLastTotal = pdPegPool.fractions.f[nLastIdx];
        int64_t nLastReserve = frReserve.f[nLastIdx];
        int64_t nTakeReserve = nLastReserve;
        nTakeReserve = std::min(nTakeReserve, nLastTotal);
        nTakeReserve = std::min(nTakeReserve, pdPegPool.nReserve);

        pdPegPool.nReserve -= nTakeReserve;
        pdPegPool.fractions.f[nLastIdx] -= nTakeReserve;

        if (nLastReserve > nTakeReserve) { // take it from liquid
            int64_t nDiff = nLastReserve - nTakeReserve;
            frReserve.f[nLastIdx] -= nDiff;
            nReserve -= nDiff;
        }

        // for liquid of partial we need to take proportionally
        // from liquid of the fraction as nLiquid/nLiquidPool
        nLastTotal = pdPegPool.fractions.f[nLastIdx];
        pdPegPool.nReserve = std::min(pdPegPool.nReserve, nLastTotal);

        int64_t nLastLiquid = nLastTotal - pdPegPool.nReserve;
        int64_t nLiquid = nValue - nReserve;
        int64_t nLiquidPool = pdPegPool.fractions.Total() - pdPegPool.nReserve;
        int64_t nTakeLiquid = RatioPart(nLastLiquid,
                                        nLiquid,
                                        nLiquidPool);
        nTakeLiquid = std::min(nTakeLiquid, nLastTotal);

        frLiquid.f[nLastIdx] += nTakeLiquid;
        pdPegPool.fractions.f[nLastIdx] -= nTakeLiquid;
    }

    // liquid is just normed to pool
    int64_t nLiquid = nValue - nReserve;
    int64_t nLiquidTodo = nValue - nReserve - frLiquid.Total();
    int64_t nLiquidPool = pdPegPool.fractions.Total() - pdPegPool.nReserve;
    if (nLiquidTodo > nLiquidPool) { // exchange liquidity mismatch
        std::stringstream ss;
        ss << "Not enough liquid " << nLiquidPool
           << " on 'pool' to balance " << nLiquidTodo;
        sErr = ss.str();
        return false;
    }

    int64_t nHoldLastPart = 0;
    if (pdPegPool.nReserve >0) {
        nHoldLastPart = pdPegPool.fractions.f[nLastIdx];
        pdPegPool.fractions.f[nLastIdx] = 0;
    }

    nLiquidTodo = pdPegPool.fractions.MoveRatioPartTo(nLiquidTodo, frLiquid);

    if (nLiquidTodo >0 && nLiquidTodo <= nHoldLastPart) {
        frLiquid.f[nLastIdx] += nLiquidTodo;
        nHoldLastPart -= nLiquidTodo;
        nLiquidTodo = 0;
    }

    if (nHoldLastPart > 0) {
        pdPegPool.fractions.f[nLastIdx] = nHoldLastPart;
        nHoldLastPart = 0;
    }

    if (nLiquidTodo >0) {
        std::stringstream ss;
        ss << "Liquid not enough after MoveRatioPartTo "
           << nLiquidTodo;
        sErr = ss.str();
        return false;
    }

    pdBalance.fractions = frReserve + frLiquid;
    pdBalance.peglevel = peglevelNew;
    pdBalance.nReserve = nReserve;
    pdBalance.nLiquid = nLiquid;

    if (nValue != pdBalance.fractions.Total()) {
        std::stringstream ss;
        ss << "Balance mimatch after update " << pdBalance.fractions.Total()
           << " vs " << nValue;
        sErr = ss.str();
        return false;
    }

    // match total
    if ((pdBalance.nReserve+pdBalance.nLiquid) != pdBalance.fractions.Total()) {
        std::stringstream ss;
        ss << "Balance mimatch liquid+reserve after update "
           << pdBalance.nLiquid << "+" << pdBalance.nReserve
           << " vs " << pdBalance.fractions.Total();
        sErr = ss.str();
        return false;
    }

    // validate liquid/reserve match peglevel
    if (fPartial) {
        int nSupplyEffective = peglevelNew.nSupply + peglevelNew.nShift +1;
        int64_t nLiquidWithoutPartial = pdBalance.fractions.High(nSupplyEffective);
        int64_t nReserveWithoutPartial = pdBalance.fractions.Low(nSupplyEffective-1);
        if (pdBalance.nLiquid < nLiquidWithoutPartial) {
            std::stringstream ss;
            ss << "Balance liquid less than without partial after update "
               << pdBalance.nLiquid
               << " vs "
               << nLiquidWithoutPartial;
            sErr = ss.str();
            return false;
        }
        if (pdBalance.nReserve < nReserveWithoutPartial) {
            std::stringstream ss;
            ss << "Balance reserve less than without partial after update "
               << pdBalance.nReserve
               << " vs "
               << nReserveWithoutPartial;
            sErr = ss.str();
            return false;
        }
    }
    else {
        int nSupplyEffective = peglevelNew.nSupply + peglevelNew.nShift;
        int64_t nLiquidCalc = pdBalance.fractions.High(nSupplyEffective);
        int64_t nReserveCalc = pdBalance.fractions.Low(nSupplyEffective);
        if (pdBalance.nLiquid != nLiquidCalc) {
            std::stringstream ss;
            ss << "Balance liquid mismatch calculated after update "
               << pdBalance.nLiquid
               << " vs "
               << nLiquidCalc;
            sErr = ss.str();
            return false;
        }
        if (pdBalance.nReserve != nReserveCalc) {
            std::stringstream ss;
            ss << "Balance reserve mismatch calculated after update "
               << pdBalance.nReserve
               << " vs "
               << nReserveCalc;
            sErr = ss.str();
            return false;
        }
    }

    int64_t nPegPoolValue = pdPegPool.fractions.Total();
    pdPegPool.nLiquid = nPegPoolValue - pdPegPool.nReserve;
    pdPegPool.nId++;
    pdBalance.nId = pdPegPool.nId;

    return true;
}

bool movecoins(
        int64_t             nMoveAmount,
        CPegData &          pdSrc,
        CPegData &          pdDst,
        const CPegLevel &   peglevel,
        bool                fCrossCycles,

        std::string &   sErr)
{
    if (!fCrossCycles && peglevel != pdSrc.peglevel) {
        std::stringstream ss;
        ss << "Outdated 'src' of cycle of " << pdSrc.peglevel.nCycle
           << ", current " << peglevel.nCycle;
        sErr = ss.str();
        return false;
    }

    if (nMoveAmount <0) {
        std::stringstream ss;
        ss << "Requested to move negative " << nMoveAmount;
        sErr = ss.str();
        return false;
    }

    int64_t nSrcValue = pdSrc.fractions.Total();
    if (nSrcValue < nMoveAmount) {
        std::stringstream ss;
        ss << "Not enough amount " << nSrcValue
           << " on 'src' to move " << nMoveAmount;
        sErr = ss.str();
        return false;
    }

    if (!fCrossCycles && peglevel != pdDst.peglevel) {
        std::stringstream ss;
        ss << "Outdated 'dst' of cycle of " << pdDst.peglevel.nCycle
           << ", current " << peglevel.nCycle;
        sErr = ss.str();
        return false;
    }

    int64_t nIn = pdSrc.fractions.Total() + pdDst.fractions.Total();

    CFractions frAmount = pdSrc.fractions;
    CFractions frMove = frAmount.RatioPart(nMoveAmount);

    pdSrc.fractions -= frMove;
    pdDst.fractions += frMove;

    int64_t nOut = pdSrc.fractions.Total() + pdDst.fractions.Total();

    if (nIn != nOut) {
        std::stringstream ss;
        ss << "Mismatch in and out values " << nIn
           << " vs " << nOut;
        sErr = ss.str();
        return false;
    }

    // std calc values
    pdSrc.nLiquid = pdSrc.fractions.High(peglevel);
    pdSrc.nReserve = pdSrc.fractions.Low(peglevel);
    pdDst.nLiquid = pdDst.fractions.High(peglevel);
    pdDst.nReserve = pdDst.fractions.Low(peglevel);

    if (pdSrc.fractions.Total() != (pdSrc.nLiquid+pdSrc.nReserve)) {
        std::stringstream ss;
        ss << "Mismatch total "
           << pdSrc.fractions.Total()
           << " vs liquid/reserve 'src' values "
           << " L:" << pdSrc.nLiquid
           << " R:" << pdSrc.nReserve;
        sErr = ss.str();
        return false;
    }

    if (pdDst.fractions.Total() != (pdDst.nLiquid+pdDst.nReserve)) {
        std::stringstream ss;
        ss << "Mismatch total "
           << pdDst.fractions.Total()
           << " vs liquid/reserve 'src' values "
           << " L:" << pdDst.nLiquid
           << " R:" << pdDst.nReserve;
        sErr = ss.str();
        return false;
    }

    if (pdSrc.peglevel != peglevel) {
        pdSrc.peglevel = peglevel;
    }
    if (pdDst.peglevel != peglevel) {
        pdDst.peglevel = peglevel;
    }

    return true;
}

bool moveliquid(
        int64_t             nMoveAmount,
        CPegData &          pdSrc,
        CPegData &          pdDst,
        const CPegLevel &   peglevel,

        std::string &   sErr)
{
    int nSupplyEffective = peglevel.nSupply + peglevel.nShift;
    if (nSupplyEffective <0 &&
        nSupplyEffective >=PEG_SIZE) {
        std::stringstream ss;
        ss << "Supply index out of bounds " << nSupplyEffective;
        sErr = ss.str();
        return false;
    }

    if (peglevel != pdSrc.peglevel) {
        std::stringstream ss;
        ss << "Outdated 'src' of cycle of " << pdSrc.peglevel.nCycle
           << ", current " << peglevel.nCycle;
        sErr = ss.str();
        return false;
    }

    if (nMoveAmount <0) {
        std::stringstream ss;
        ss << "Requested to move negative " << nMoveAmount;
        sErr = ss.str();
        return false;
    }

    if (pdSrc.nLiquid < nMoveAmount) {
        std::stringstream ss;
        ss << "Not enough liquid " << pdSrc.nLiquid
           << " on 'src' to move " << nMoveAmount;
        sErr = ss.str();
        return false;
    }

    if (peglevel != pdDst.peglevel) {
        std::stringstream ss;
        ss << "Outdated 'dst' of cycle of " << pdDst.peglevel.nCycle
           << ", current " << peglevel.nCycle;
        sErr = ss.str();
        return false;
    }

    if (pdSrc.nId != 0 && pdDst.nId != 0 && pdSrc.nId == pdDst.nId) {
        std::stringstream ss;
        ss << "Detected move between same user src id:" << pdSrc.nId
           << " vs dst id:" << pdDst.nId;
        sErr = ss.str();
        return false;
    }

    int64_t nIn = pdSrc.fractions.Total() + pdDst.fractions.Total();

    bool fPartial = peglevel.nShiftLastPart >0 && peglevel.nShiftLastTotal >0;
    if (fPartial) {
        nSupplyEffective++;
    }

    CFractions frLiquid = pdSrc.fractions.HighPart(nSupplyEffective, nullptr);

    if (fPartial) {
        int64_t nPartialLiquid = pdSrc.nLiquid - frLiquid.Total();
        if (nPartialLiquid < 0) {
            std::stringstream ss;
            ss << "Mismatch on nPartialLiquid " << nPartialLiquid;
            sErr = ss.str();
            return false;
        }

        frLiquid.f[nSupplyEffective-1] = nPartialLiquid;
    }

    if (frLiquid.Total() < nMoveAmount) {
        std::stringstream ss;
        ss << "Not enough liquid(1) " << frLiquid.Total()
           << " on 'src' to move " << nMoveAmount;
        sErr = ss.str();
        return false;
    }

    CFractions frMove = frLiquid.RatioPart(nMoveAmount);

    pdSrc.fractions -= frMove;
    pdDst.fractions += frMove;

    pdSrc.nLiquid -= nMoveAmount;
    pdDst.nLiquid += nMoveAmount;

    int64_t nOut = pdSrc.fractions.Total() + pdDst.fractions.Total();

    if (nIn != nOut) {
        std::stringstream ss;
        ss << "Mismatch in and out values " << nIn
           << " vs " << nOut;
        sErr = ss.str();
        return false;
    }

    if (!pdSrc.fractions.IsPositive()) {
        sErr = "Negative detected in 'src";
        return false;
    }

    if (pdSrc.fractions.Total() != (pdSrc.nLiquid+pdSrc.nReserve)) {
        std::stringstream ss;
        ss << "Mismatch total "
           << pdSrc.fractions.Total()
           << " vs liquid/reserve 'src' values "
           << " L:" << pdSrc.nLiquid
           << " R:" << pdSrc.nReserve;
        sErr = ss.str();
        return false;
    }

    if (pdDst.fractions.Total() != (pdDst.nLiquid+pdDst.nReserve)) {
        std::stringstream ss;
        ss << "Mismatch total "
           << pdDst.fractions.Total()
           << " vs liquid/reserve 'src' values "
           << " L:" << pdDst.nLiquid
           << " R:" << pdDst.nReserve;
        sErr = ss.str();
        return false;
    }

    return true;
}

bool movereserve(
        int64_t             nMoveAmount,
        CPegData &          pdSrc,
        CPegData &          pdDst,
        const CPegLevel &   peglevel,

        std::string &   sErr)
{
    int nSupplyEffective = peglevel.nSupply + peglevel.nShift;
    if (nSupplyEffective <0 &&
        nSupplyEffective >=PEG_SIZE) {
        std::stringstream ss;
        ss << "Supply index out of bounds " << nSupplyEffective;
        sErr = ss.str();
        return false;
    }

    if (peglevel != pdSrc.peglevel) {
        std::stringstream ss;
        ss << "Outdated 'src' of cycle of " << pdSrc.peglevel.nCycle
           << ", current " << peglevel.nCycle;
        sErr = ss.str();
        return false;
    }

    if (nMoveAmount <0) {
        std::stringstream ss;
        ss << "Requested to move negative " << nMoveAmount;
        sErr = ss.str();
        return false;
    }

    if (pdSrc.nReserve < nMoveAmount) {
        std::stringstream ss;
        ss << "Not enough reserve " << pdSrc.nReserve
           << " on 'src' to move " << nMoveAmount;
        sErr = ss.str();
        return false;
    }

    if (peglevel != pdDst.peglevel) {
        std::stringstream ss;
        ss << "Outdated 'dst' of cycle of " << pdDst.peglevel.nCycle
           << ", current " << peglevel.nCycle;
        sErr = ss.str();
        return false;
    }

    if (pdSrc.nId != 0 && pdDst.nId != 0 && pdSrc.nId == pdDst.nId) {
        std::stringstream ss;
        ss << "Detected move between same user src id:" << pdSrc.nId
           << " vs dst id:" << pdDst.nId;
        sErr = ss.str();
        return false;
    }

    int64_t nIn = pdSrc.fractions.Total() + pdDst.fractions.Total();

    CFractions frReserve = pdSrc.fractions.LowPart(nSupplyEffective, nullptr);

    bool fPartial = peglevel.nShiftLastPart >0 && peglevel.nShiftLastTotal >0;
    if (fPartial) {
        int64_t nPartialReserve = pdSrc.nReserve - frReserve.Total();
        if (nPartialReserve < 0) {
            std::stringstream ss;
            ss << "Mismatch on nPartialReserve " << nPartialReserve;
            sErr = ss.str();
            return false;
        }

        frReserve.f[nSupplyEffective] = nPartialReserve;
    }

    CFractions frMove = frReserve.RatioPart(nMoveAmount);

    pdSrc.fractions -= frMove;
    pdDst.fractions += frMove;

    pdSrc.nReserve -= nMoveAmount;
    pdDst.nReserve += nMoveAmount;

    int64_t nOut = pdSrc.fractions.Total() + pdDst.fractions.Total();

    if (nIn != nOut) {
        std::stringstream ss;
        ss << "Mismatch in and out values " << nIn
           << " vs " << nOut;
        sErr = ss.str();
        return false;
    }

    if (!pdSrc.fractions.IsPositive()) {
        sErr = "Negative detected in 'src";
        return false;
    }

    if (pdSrc.fractions.Total() != (pdSrc.nLiquid+pdSrc.nReserve)) {
        std::stringstream ss;
        ss << "Mismatch total "
           << pdSrc.fractions.Total()
           << " vs liquid/reserve 'src' values "
           << " L:" << pdSrc.nLiquid
           << " R:" << pdSrc.nReserve;
        sErr = ss.str();
        return false;
    }

    if (pdDst.fractions.Total() != (pdDst.nLiquid+pdDst.nReserve)) {
        std::stringstream ss;
        ss << "Mismatch total "
           << pdDst.fractions.Total()
           << " vs liquid/reserve 'src' values "
           << " L:" << pdDst.nLiquid
           << " R:" << pdDst.nReserve;
        sErr = ss.str();
        return false;
    }

    return true;
}



class CCoinToUse
{
public:
    uint256     txhash;
    uint64_t    i;
    int64_t     nValue;
    int64_t     nAvailableValue;
    CScript     scriptPubKey;
    string      privkeybip32;
    int         nCycle;
    CFractions  fractions;

    CCoinToUse() : i(0),nValue(0),nAvailableValue(0),nCycle(0) {}

    friend bool operator<(const CCoinToUse &a, const CCoinToUse &b) {
        if (a.txhash < b.txhash) return true;
        if (a.txhash == b.txhash && a.i < b.i) return true;
        if (a.txhash == b.txhash && a.i == b.i && a.nValue < b.nValue) return true;
        if (a.txhash == b.txhash && a.i == b.i && a.nValue == b.nValue && a.nAvailableValue < b.nAvailableValue) return true;
        if (a.txhash == b.txhash && a.i == b.i && a.nValue == b.nValue && a.nAvailableValue == b.nAvailableValue && a.scriptPubKey < b.scriptPubKey) return true;
        if (a.txhash == b.txhash && a.i == b.i && a.nValue == b.nValue && a.nAvailableValue == b.nAvailableValue && a.scriptPubKey == b.scriptPubKey && a.nCycle < b.nCycle) return true;
        return false;
    }

    IMPLEMENT_SERIALIZE
    (
        READWRITE(txhash);
        READWRITE(i);
        READWRITE(nValue);
        READWRITE(scriptPubKey);
        READWRITE(nCycle);
    )
};

static bool sortByAddress(const CCoinToUse &lhs, const CCoinToUse &rhs) {
    CScript lhs_script = lhs.scriptPubKey;
    CScript rhs_script = rhs.scriptPubKey;

    CTxDestination lhs_dst;
    CTxDestination rhs_dst;
    bool lhs_ok1 = ExtractDestination(lhs_script, lhs_dst);
    bool rhs_ok1 = ExtractDestination(rhs_script, rhs_dst);

    if (!lhs_ok1 || !rhs_ok1) {
        if (lhs_ok1 == rhs_ok1)
            return lhs_script < rhs_script;
        return lhs_ok1 < rhs_ok1;
    }

    string lhs_addr = CBitcoinAddress(lhs_dst).ToString();
    string rhs_addr = CBitcoinAddress(rhs_dst).ToString();

    return lhs_addr < rhs_addr;
}

static bool sortByDestination(const CTxDestination &lhs, const CTxDestination &rhs) {
    string lhs_addr = CBitcoinAddress(lhs).ToString();
    string rhs_addr = CBitcoinAddress(rhs).ToString();
    return lhs_addr < rhs_addr;
}

typedef std::map<uint320, CTxOut> MapPrevOut;

static bool computeTxPegForNextCycle(const CTransaction & rawTx,
                                     const CPegLevel & peglevel_net,
                                     map<uint320,CCoinToUse> & mapAllOutputs,
                                     map<int, CFractions> & mapTxOutputFractions,
                                     CFractions & feesFractions,
                                     std::string& sFailCause)
{
    MapPrevOut mapInputs;
    MapFractions mapInputsFractions;
    MapFractions mapOutputFractions;

    size_t n_vin = rawTx.vin.size();

    for (unsigned int i = 0; i < n_vin; i++)
    {
        const COutPoint & prevout = rawTx.vin[i].prevout;
        auto fkey = uint320(prevout.hash, prevout.n);

        if (!mapAllOutputs.count(fkey)) {
            return false;
        }

        const CCoinToUse& coin = mapAllOutputs[fkey];
        CTxOut out(coin.nValue, coin.scriptPubKey);
        mapInputs[fkey] = out;
        mapInputsFractions[fkey] = coin.fractions;
    }

    bool peg_ok = CalculateStandardFractions(rawTx,
                                             peglevel_net.nSupplyNext,
                                             rawTx.nTime,
                                             mapInputs,
                                             mapInputsFractions,
                                             mapOutputFractions,
                                             feesFractions,
                                             sFailCause);
    if (!peg_ok) {
        return false;
    }

    size_t n_out = rawTx.vout.size();
    for(size_t i=0; i< n_out; i++) {
        auto fkey = uint320(rawTx.GetHash(), i);
        mapTxOutputFractions[i] = mapOutputFractions[fkey];
    }
    return true;
}

static void loadMaintenance(const string & data, CFractions & frMaintenance) {
    vector<string> vArgs;
    vector<string> vAMArgs;
    boost::split(vArgs, data, boost::is_any_of(","));
    for(string sArg : vArgs) {
        string sAMPrefix = "AM:";
        if (boost::starts_with(sArg, sAMPrefix)) {
            boost::split(vAMArgs, sArg, boost::is_any_of(":"));
            if (vAMArgs.size() <2) continue;
            CPegData pd(vAMArgs[1]);
            if (pd.IsValid()) {
                frMaintenance = pd.fractions;
            }
        }
    }
}

static void saveMaintenance(string & data,
                            const CFractions & fractions,
                            const CPegLevel & peglevel) {
    CPegData pd;
    pd.peglevel = peglevel;
    pd.fractions = fractions;
    pd.nLiquid = pd.fractions.High(peglevel);
    pd.nReserve = pd.fractions.Low(peglevel);

    vector<string> vArgs;
    vector<string> vAMArgs;
    boost::split(vArgs, data, boost::is_any_of(","));
    for(size_t i=0; i< vArgs.size(); i++) {
        string sAMPrefix = "AM:";
        if (boost::starts_with(vArgs[i], sAMPrefix)) {
            boost::split(vAMArgs, vArgs[i], boost::is_any_of(":"));
            if (vAMArgs.size() <2) continue;
            vAMArgs[1] = pd.ToString();
            vArgs[i] = boost::algorithm::join(vAMArgs, ":");
            data = boost::algorithm::join(vArgs, ",");
            return;
        }
    }

    if (!data.empty()) {
        data += ",";
    }
    data += "AM:"+pd.ToString();
}

bool prepareliquidwithdraw(
        const std::vector<
            std::tuple<
                std::string,
                CPegData,
                std::string>> & txins,
        CPegData &              pdBalance,
        CPegData &              pdExchange,
        CPegData &              pdPegShift,
        int64_t                 nAmountWithFee,
        std::string             sAddress,
        const CPegLevel &       peglevel_exchange,

        CPegData &              pdRequested,
        CPegData &              pdProcessed,
        std::string &           withdrawIdXch,
        std::string &           withdrawTxout,
        std::string &           rawtxstr,
        std::vector<
            std::tuple<
                std::string,
                CPegData,
                std::string>> &    txouts,

        std::string &   sErr) {

    CBitcoinAddress address(sAddress);
    if (!address.IsValid()) {
        std::stringstream ss;
        ss << "Invalid BitBay address " << sAddress;
        sErr = ss.str();
        return false;
    }

    if (txins.size() > 30) {
        sErr = "Too much inputs for withdraw (max=30 liquid)";
        return false;
    }

    CFractions frMaintenance(0, CFractions::STD);
    loadMaintenance(pdPegShift.fractions.sReturnAddr, frMaintenance);

    if (nAmountWithFee <0) {
        std::stringstream ss;
        ss << "Requested to withdraw negative " << nAmountWithFee;
        sErr = ss.str();
        return false;
    }
    if (nAmountWithFee <1000000+2) {
        std::stringstream ss;
        ss << "Requested to withdraw less than 1M fee " << nAmountWithFee;
        sErr = ss.str();
        return false;
    }

    if (!peglevel_exchange.IsValid()) {
        sErr = "Not valid peglevel";
        return false;
    }

    // network peglevel (exchange minus buffer)
    CPegLevel peglevel_net(peglevel_exchange.nCycle,
                           peglevel_exchange.nCyclePrev,
                           0 /*buffer=0, network level*/,
                           peglevel_exchange.nSupply - peglevel_exchange.nBuffer,
                           peglevel_exchange.nSupplyNext - peglevel_exchange.nBuffer,
                           peglevel_exchange.nSupplyNextNext - peglevel_exchange.nBuffer);

    if (!pdBalance.IsValid()) {
        sErr = "Not valid 'balance' pegdata";
        return false;
    }
    if (!pdExchange.IsValid()) {
        sErr = "Not valid 'exchange' pegdata";
        return false;
    }
    if (!pdPegShift.IsValid()) {
        sErr = "Not valid 'pegshift' pegdata";
        return false;
    }

    if (pdBalance.peglevel.nCycle != peglevel_exchange.nCycle) {
        sErr = "Balance has other cycle than peglevel";
        return false;
    }

    if (nAmountWithFee > pdBalance.nLiquid) {
        std::stringstream ss;
        ss << "Not enough liquid " << pdBalance.nLiquid
           << " on 'balance' to withdraw " << nAmountWithFee;
        sErr = ss.str();
        return false;
    }

    int nSupplyEffective = peglevel_exchange.nSupply + peglevel_exchange.nShift;
    bool fPartial = peglevel_exchange.nShiftLastPart >0 && peglevel_exchange.nShiftLastTotal >0;
    if (fPartial) {
        nSupplyEffective++;
    }

    CFractions frBalanceLiquid = pdBalance.fractions.HighPart(nSupplyEffective, nullptr);

    if (fPartial) {
        int64_t nPartialLiquid = pdBalance.nLiquid - frBalanceLiquid.Total();
        if (nPartialLiquid < 0) {
            std::stringstream ss;
            ss << "Mismatch on nPartialLiquid " << nPartialLiquid;
            sErr = ss.str();
            return false;
        }

        frBalanceLiquid.f[nSupplyEffective-1] = nPartialLiquid;
    }

    if (frBalanceLiquid.Total() < nAmountWithFee) {
        std::stringstream ss;
        ss << "Not enough liquid(1) " << frBalanceLiquid.Total()
           << "  on 'balance' to withdraw " << nAmountWithFee;
        sErr = ss.str();
        return false;
    }

    CFractions frAmount = frBalanceLiquid.RatioPart(nAmountWithFee);
    CFractions frRequested = frAmount;

    vector<CCoinToUse> vCoins;
    int64_t nLeftAmount = nAmountWithFee;

    map<uint320,CCoinToUse> mapAllOutputs;

    CBasicKeyStore keyStore;

    int i =0;
    for(const std::tuple<string,CPegData,string> & txin : txins) {
        string txhash_nout = std::get<0>(txin);
        const CPegData & pdTxin = std::get<1>(txin);
        string privkeybip32 = std::get<2>(txin);

        if (privkeybip32.empty()) {
            std::stringstream ss;
            ss << "Txout bip32 key is empty, idx=" << i;
            sErr = ss.str();
            return false;
        }

        vector<string> vTxoutArgs;
        boost::split(vTxoutArgs, txhash_nout, boost::is_any_of(":"));

        if (vTxoutArgs.size() != 2) {
            std::stringstream ss;
            ss << "Txout is not recognized, format txhash:nout, idx=" << i;
            sErr = ss.str();
            return false;
        }
        string sTxid = vTxoutArgs[0];
        char * pEnd = nullptr;
        long nout = strtol(vTxoutArgs[1].c_str(), &pEnd, 0);
        if (pEnd == vTxoutArgs[1].c_str()) {
            std::stringstream ss;
            ss << "Txout is not recognized, format txhash:nout, idx=" << i;
            sErr = ss.str();
            return false;
        }
        uint256 txhash;
        txhash.SetHex(sTxid);

        auto fkey = uint320(txhash, nout);
        int64_t nAvailableLiquid = pdTxin.fractions.High(peglevel_net.nSupplyNext);

        CCoinToUse coin;
        coin.i = nout;
        coin.txhash = txhash;
        coin.nValue = pdTxin.fractions.Total();
        coin.nAvailableValue = nAvailableLiquid;
        vector<unsigned char> pkData(ParseHex(pdTxin.fractions.sReturnAddr));
        coin.scriptPubKey = CScript(pkData.begin(), pkData.end());
        coin.privkeybip32 = privkeybip32;
        coin.nCycle = peglevel_exchange.nCycle;
        coin.fractions = pdTxin.fractions;
        vCoins.push_back(coin);
        mapAllOutputs[fkey] = coin;

        nLeftAmount -= nAvailableLiquid;

        CBitcoinExtKey b58keyDecodeCheck(privkeybip32);
        CExtKey privKey = b58keyDecodeCheck.GetKey();
        if (!privKey.key.IsValid()) {
            std::stringstream ss;
            ss << "Failed to add key/pubkey, private key is invalid, idx=" << i;
            sErr = ss.str();
            return false;
        }
        CExtPubKey pubKey = privKey.Neuter();
        if (!pubKey.pubkey.IsValid()) {
            std::stringstream ss;
            ss << "Failed to add key/pubkey, public key is invalid, idx=" << i;
            sErr = ss.str();
            return false;
        }

        if (!keyStore.AddKeyPubKey(privKey.key, pubKey.pubkey)) {
            std::stringstream ss;
            ss << "Failed to add key/pubkey, idx=" << i;
            sErr = ss.str();
            return false;
        }

        i++;
    }

    // check enough coins in inputs
    if (nLeftAmount > 0) {
        std::stringstream ss;
        ss << "Not enough liquid in inputs to next cycle withdraw "
           << nAmountWithFee;
        sErr = ss.str();
        return false;
    }

    int64_t nFeeRet = 1000000 /*common fee deducted from user amount, 1M*/;
    int64_t nAmount = nAmountWithFee - nFeeRet;

    vector<pair<CScript, int64_t> > vecSend;
    CScript scriptPubKey;
    scriptPubKey.SetDestination(address.Get());
    vecSend.push_back(make_pair(scriptPubKey, nAmount));

    int64_t nValue = 0;
    for(const pair<CScript, int64_t>& s : vecSend) {
        if (nValue < 0)
            return false;
        nValue += s.second;
    }

    size_t nNumInputs = 1;

    CTransaction rawTx;

    nNumInputs = vCoins.size();
    if (!nNumInputs) return false;

    // Inputs to be sorted by address
    sort(vCoins.begin(), vCoins.end(), sortByAddress);

    // Collect input addresses
    // Prepare maps for input,available,take
    set<CTxDestination> setInputAddresses;
    vector<CTxDestination> vInputAddresses;
    map<CTxDestination, string> mapBip32Keys;
    map<CTxDestination, int64_t> mapAvailableValuesAt;
    map<CTxDestination, int64_t> mapInputValuesAt;
    map<CTxDestination, int64_t> mapTakeValuesAt;
    for(const CCoinToUse& coin : vCoins) {
        CTxDestination address;
        if(!ExtractDestination(coin.scriptPubKey, address))
            continue;

        setInputAddresses.insert(address); // sorted due to vCoins
        mapBip32Keys[address] = coin.privkeybip32;
        mapAvailableValuesAt[address] = 0;
        mapInputValuesAt[address] = 0;
        mapTakeValuesAt[address] = 0;
    }
    // Get sorted list of input addresses
    for(const CTxDestination& address : setInputAddresses) {
        vInputAddresses.push_back(address);
    }
    sort(vInputAddresses.begin(), vInputAddresses.end(), sortByDestination);
    // Input and available values can be filled in
    for(const CCoinToUse& coin : vCoins) {
        CTxDestination address;
        if(!ExtractDestination(coin.scriptPubKey, address))
            continue;
        int64_t& nValueAvailableAt = mapAvailableValuesAt[address];
        nValueAvailableAt += coin.nAvailableValue;
        int64_t& nValueInputAt = mapInputValuesAt[address];
        nValueInputAt += coin.nValue;
    }

    // vouts to the payees
    for(const pair<CScript, int64_t>& s : vecSend) {
        rawTx.vout.push_back(CTxOut(s.second, s.first));
    }

    // Available values - liquidity
    // Compute values to take from each address (liquidity is common)
    int64_t nValueLeft = nValue;
    for(const CCoinToUse& coin : vCoins) {
        CTxDestination address;
        if(!ExtractDestination(coin.scriptPubKey, address))
            continue;
        int64_t nValueAvailable = coin.nAvailableValue;
        int64_t nValueTake = nValueAvailable;
        if (nValueTake > nValueLeft) {
            nValueTake = nValueLeft;
        }
        int64_t& nValueTakeAt = mapTakeValuesAt[address];
        nValueTakeAt += nValueTake;
        nValueLeft -= nValueTake;
    }

    // Calculate change (minus fee, notary and part taken from change)
    int64_t nTakeFromChangeLeft = nFeeRet+1;
    map<CTxDestination, int> mapChangeOutputs;
    for (const CTxDestination& address : vInputAddresses) {
        CScript scriptPubKey;
        scriptPubKey.SetDestination(address);
        int64_t& nValueTakeAt = mapTakeValuesAt[address];
        int64_t nValueInput = mapInputValuesAt[address];
        int64_t nValueChange = nValueInput - nValueTakeAt;
        if (nValueChange > nTakeFromChangeLeft) {
            nValueChange -= nTakeFromChangeLeft;
            nTakeFromChangeLeft = 0;
        }
        if (nValueChange <= nTakeFromChangeLeft) {
            nTakeFromChangeLeft -= nValueChange;
            nValueChange = 0;
        }
        if (nValueChange == 0) continue;
        nValueTakeAt += nValueChange;
        mapChangeOutputs[address] = rawTx.vout.size();
        rawTx.vout.push_back(CTxOut(nValueChange, scriptPubKey));
    }

    // Fill vin
    for(const CCoinToUse& coin : vCoins) {
        rawTx.vin.push_back(CTxIn(coin.txhash,coin.i));
    }

    // notation for exchange control
    {
        string sNotary = "XCH:0:";
        CDataStream ss(SER_GETHASH, 0);
        size_t n_inp = rawTx.vin.size();
        for(size_t j=0; j< n_inp; j++) {
            ss << rawTx.vin[j].prevout.hash;
            ss << rawTx.vin[j].prevout.n;
        }
        ss << string("L");
        ss << sAddress;
        ss << nAmount;
        withdrawIdXch = Hash(ss.begin(), ss.end()).GetHex();
        sNotary += withdrawIdXch;
        CScript scriptPubKey;
        scriptPubKey.push_back(OP_RETURN);
        unsigned char len_bytes = sNotary.size();
        scriptPubKey.push_back(len_bytes);
        for (size_t j=0; j< sNotary.size(); j++) {
            scriptPubKey.push_back(sNotary[j]);
        }
        rawTx.vout.push_back(CTxOut(1, scriptPubKey));
    }

    // locktime to next interval
    rawTx.nLockTime = (peglevel_net.nCycle+1) * Params().PegInterval(Params().nPegIntervalProbeHeight) -1;

    // Calculate peg to know 'user' fee
    CFractions feesFractionsCommon(0, CFractions::STD);
    map<int, CFractions> mapTxOutputFractionsSkip;
    if (!computeTxPegForNextCycle(rawTx, peglevel_net, mapAllOutputs,
                                  mapTxOutputFractionsSkip, feesFractionsCommon,
                                  sErr)) {
        return false;
    }

    // for liquid just first output
    if (!mapTxOutputFractionsSkip.count(0)) {
        sErr = "No withdraw fractions";
        return false;
    }

    // Now all inputs and outputs are know, calculate network fee
    int64_t nFeeNetwork = 200000 + 10000 * (rawTx.vin.size() + rawTx.vout.size());
    int64_t nFeeMaintenance = nFeeRet - nFeeNetwork;
    int64_t nFeeMaintenanceLeft = nFeeMaintenance;
    // Maintenance fee recorded in provided_outputs
    // It is returned - distributed over 'change' outputs
    // First if we can use existing change
    for (const CTxDestination& address : vInputAddresses) {
        if (!mapChangeOutputs.count(address)) continue;
        int nOut = mapChangeOutputs[address];
        int64_t& nValueTakeAt = mapTakeValuesAt[address];
        int64_t nValueInput = mapInputValuesAt[address];
        int64_t nValueForFee = nValueInput - nValueTakeAt;
        if (nValueForFee > nFeeMaintenanceLeft) {
            rawTx.vout[nOut].nValue += nFeeMaintenanceLeft;
            nFeeMaintenanceLeft = 0;
        }
        if (nValueForFee <= nFeeMaintenanceLeft) {
            nFeeMaintenanceLeft -= nValueForFee;
            rawTx.vout[nOut].nValue += nValueForFee;
        }
    }
    // Second if it is still left we need to add change outputs for maintenance fee
    for (const CTxDestination& address : vInputAddresses) {
        if (mapChangeOutputs.count(address)) continue;
        int64_t nValueTakeAt = mapTakeValuesAt[address];
        int64_t nValueInput = mapInputValuesAt[address];
        int64_t nValueForFee = nValueInput - nValueTakeAt;
        if (nValueForFee > nFeeMaintenanceLeft) {
            nValueForFee = nFeeMaintenanceLeft;
            nFeeMaintenanceLeft = 0;
        }
        if (nValueForFee <= nFeeMaintenanceLeft) {
            nFeeMaintenanceLeft -= nValueForFee;
        }
        if (nValueForFee == 0) continue;
        CScript scriptPubKey;
        scriptPubKey.SetDestination(address);
        rawTx.vout.push_back(CTxOut(nValueForFee, scriptPubKey));
    }
    // Can you for maintenance (is not all nFeeMaintenanceLeft consumed):
    nFeeMaintenance -= nFeeMaintenanceLeft;

    // Recalculate peg to update mapTxOutputFractions
    CFractions feesFractionsNet(0, CFractions::STD);
    map<int, CFractions> mapTxOutputFractions;
    if (!computeTxPegForNextCycle(rawTx, peglevel_net, mapAllOutputs,
                                  mapTxOutputFractions, feesFractionsNet,
                                  sErr)) {
        return false;
    }
    CFractions frFeesMaintenance = feesFractionsCommon - feesFractionsNet;
    frMaintenance += frFeesMaintenance;

    // for liquid just first output
    if (!mapTxOutputFractions.count(0)) {
        sErr = "No withdraw fractions (2)";
        return false;
    }

    // signing the transaction to get it ready for broadcast
    int nIn = 0;
    for(const CCoinToUse& coin : vCoins) {
        if (!SignSignature(keyStore, coin.scriptPubKey, rawTx, nIn++)) {
            std::stringstream ss;
            ss << "Fail on signing input " << nIn-1;
            sErr = ss.str();
            return false;
        }
    }
    // for liquid just first output
    CFractions frProcessed = mapTxOutputFractions[0] + feesFractionsCommon;

    if (frRequested.Total() != nAmountWithFee) {
        std::stringstream ss;
        ss << "Mismatch requested and amount_with_fee " << frRequested.Total()
           << " " << nAmountWithFee;
        sErr = ss.str();
        return false;
    }
    if (frProcessed.Total() != nAmountWithFee) {
        std::stringstream ss;
        ss << "Mismatch processed and amount_with_fee " << frProcessed.Total()
           << " " << nAmountWithFee;
        sErr = ss.str();
        return false;
    }

    // save txouts
    string sTxhash = rawTx.GetHash().GetHex();
    // skip 0 (withdraw) and last (xch id)
    for (size_t i=1; i< rawTx.vout.size()-1; i++) {
        string txout = sTxhash+":"+std::to_string(i);
        CPegData pdTxout;
        pdTxout.fractions = mapTxOutputFractions[i];
        pdTxout.fractions.sReturnAddr = HexStr(
            rawTx.vout[i].scriptPubKey.begin(),
            rawTx.vout[i].scriptPubKey.end()
        );
        pdTxout.peglevel = peglevel_exchange;
        pdTxout.nLiquid = pdTxout.fractions.High(peglevel_exchange);
        pdTxout.nReserve = pdTxout.fractions.Low(peglevel_exchange);

        CTxDestination address;
        if(!ExtractDestination(rawTx.vout[i].scriptPubKey, address))
            continue;
        string privkeybip32 = mapBip32Keys[address];
        if (privkeybip32.empty())
            continue;

        txouts.push_back(std::make_tuple(txout, pdTxout, privkeybip32));
    }

    pdBalance.fractions -= frRequested;
    pdExchange.fractions -= frRequested;
    pdPegShift.fractions += (frRequested - frProcessed);
    saveMaintenance(pdPegShift.fractions.sReturnAddr,
                    frMaintenance,
                    peglevel_net);

    pdBalance.nLiquid -= nAmountWithFee;

    // consume liquid part of pegshift by balance
    // as computation were completed by pegnext it may use fractions
    // of current reserves - at current supply not to consume these fractions
//    consumeliquidpegshift(pdBalance.fractions,
//                          pdExchange.fractions,
//                          pdPegShift.fractions,
//                          peglevel_exchange);

    pdExchange.peglevel = peglevel_exchange;
    pdExchange.nLiquid = pdExchange.fractions.High(peglevel_exchange);
    pdExchange.nReserve = pdExchange.fractions.Low(peglevel_exchange);

    pdProcessed = CPegData();
    pdProcessed.fractions = frProcessed;
    pdProcessed.peglevel = peglevel_exchange;
    pdProcessed.nLiquid = pdProcessed.fractions.High(peglevel_exchange);
    pdProcessed.nReserve = pdProcessed.fractions.Low(peglevel_exchange);

    pdRequested = CPegData();
    pdRequested.fractions = frRequested;
    pdRequested.peglevel = peglevel_exchange;
    pdRequested.nLiquid = pdRequested.fractions.High(peglevel_exchange);
    pdRequested.nReserve = pdRequested.fractions.Low(peglevel_exchange);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << rawTx;

    rawtxstr = HexStr(ss.begin(), ss.end());
    withdrawTxout = sTxhash+":0"; // first

    return true;
}

bool preparereservewithdraw(
        const std::vector<
            std::tuple<
                std::string,
                CPegData,
                std::string>> & txins,
        CPegData &              pdBalance,
        CPegData &              pdExchange,
        CPegData &              pdPegShift,
        int64_t                 nAmountWithFee,
        std::string             sAddress,
        const CPegLevel &       peglevel_exchange,

        CPegData &              pdRequested,
        CPegData &              pdProcessed,
        std::string &           withdrawIdXch,
        std::string &           withdrawTxout,
        std::string &           rawtxstr,
        std::vector<
            std::tuple<
                std::string,
                CPegData,
                std::string>> & txouts,

        std::string &   sErr) {

    CBitcoinAddress address(sAddress);
    if (!address.IsValid()) {
        std::stringstream ss;
        ss << "Invalid BitBay address " << sAddress;
        sErr = ss.str();
        return false;
    }

    if (txins.size() > 20) {
        sErr = "Too much inputs for withdraw (max=20 reserve)";
        return false;
    }

    CFractions frMaintenance(0, CFractions::STD);
    loadMaintenance(pdPegShift.fractions.sReturnAddr, frMaintenance);

    if (nAmountWithFee <0) {
        std::stringstream ss;
        ss << "Requested to withdraw negative " << nAmountWithFee;
        sErr = ss.str();
        return false;
    }
    if (nAmountWithFee <1000000+2) {
        std::stringstream ss;
        ss << "Requested to withdraw less than 1M fee " << nAmountWithFee;
        sErr = ss.str();
        return false;
    }

    if (!peglevel_exchange.IsValid()) {
        sErr = "Not valid peglevel";
        return false;
    }

    // network peglevel (exchange minus buffer)
    CPegLevel peglevel_net(peglevel_exchange.nCycle,
                           peglevel_exchange.nCyclePrev,
                           0 /*buffer=0, network level*/,
                           peglevel_exchange.nSupply - peglevel_exchange.nBuffer,
                           peglevel_exchange.nSupplyNext - peglevel_exchange.nBuffer,
                           peglevel_exchange.nSupplyNextNext - peglevel_exchange.nBuffer);

    if (!pdBalance.IsValid()) {
        sErr = "Not valid 'balance' pegdata";
        return false;
    }
    if (!pdExchange.IsValid()) {
        sErr = "Not valid 'exchange' pegdata";
        return false;
    }
    if (!pdPegShift.IsValid()) {
        sErr = "Not valid 'pegshift' pegdata";
        return false;
    }

    if (pdBalance.peglevel.nCycle != peglevel_exchange.nCycle) {
        sErr = "Balance has other cycle than peglevel";
        return false;
    }

    if (nAmountWithFee > pdBalance.nReserve) {
        std::stringstream ss;
        ss << "Not enough reserve " << pdBalance.nReserve
           << " on 'balance' to withdraw " << nAmountWithFee;
        sErr = ss.str();
        return false;
    }

    int nSupplyEffective = peglevel_exchange.nSupply + peglevel_exchange.nShift;
    bool fPartial = peglevel_exchange.nShiftLastPart >0 && peglevel_exchange.nShiftLastTotal >0;

    CFractions frBalanceReserve = pdBalance.fractions.LowPart(nSupplyEffective, nullptr);

    if (fPartial) {
        int64_t nPartialReserve = pdBalance.nReserve - frBalanceReserve.Total();
        if (nPartialReserve < 0) {
            std::stringstream ss;
            ss << "Mismatch on nPartialReserve " << nPartialReserve;
            sErr = ss.str();
            return false;
        }

        frBalanceReserve.f[nSupplyEffective] = nPartialReserve;
    }

    if (frBalanceReserve.Total() < nAmountWithFee) {
        std::stringstream ss;
        ss << "Not enough reserve(1) " << frBalanceReserve.Total()
           << "  on 'balance' to withdraw " << nAmountWithFee;
        sErr = ss.str();
        return false;
    }

    CFractions frAmount = frBalanceReserve.RatioPart(nAmountWithFee);
    CFractions frRequested = frAmount;

    vector<CCoinToUse> vCoins;
    int64_t nLeftAmount = nAmountWithFee;

    map<uint320,CCoinToUse> mapAllOutputs;

    CBasicKeyStore keyStore;

    int i =0;
    for(const std::tuple<string,CPegData,string> & txin : txins) {
        string txhash_nout = std::get<0>(txin);
        const CPegData & pdTxin = std::get<1>(txin);
        string privkeybip32 = std::get<2>(txin);

        if (privkeybip32.empty()) {
            std::stringstream ss;
            ss << "Txout bip32 key is empty, idx=" << i;
            sErr = ss.str();
            return false;
        }

        vector<string> vTxoutArgs;
        boost::split(vTxoutArgs, txhash_nout, boost::is_any_of(":"));

        if (vTxoutArgs.size() != 2) {
            std::stringstream ss;
            ss << "Txout is not recognized, format txhash:nout, idx=" << i;
            sErr = ss.str();
            return false;
        }
        string sTxid = vTxoutArgs[0];
        char * pEnd = nullptr;
        long nout = strtol(vTxoutArgs[1].c_str(), &pEnd, 0);
        if (pEnd == vTxoutArgs[1].c_str()) {
            std::stringstream ss;
            ss << "Txout is not recognized, format txhash:nout, idx=" << i;
            sErr = ss.str();
            return false;
        }
        uint256 txhash;
        txhash.SetHex(sTxid);

        auto fkey = uint320(txhash, nout);
        int64_t nAvailableReserve = pdTxin.fractions.Low(peglevel_exchange.nSupplyNext);

        CCoinToUse coin;
        coin.i = nout;
        coin.txhash = txhash;
        coin.nValue = pdTxin.fractions.Total();
        coin.nAvailableValue = nAvailableReserve;
        vector<unsigned char> pkData(ParseHex(pdTxin.fractions.sReturnAddr));
        coin.scriptPubKey = CScript(pkData.begin(), pkData.end());
        coin.privkeybip32 = privkeybip32;
        coin.nCycle = peglevel_exchange.nCycle;
        coin.fractions = pdTxin.fractions;
        vCoins.push_back(coin);
        mapAllOutputs[fkey] = coin;

        // for withdrawing can use liquid coins too
        nLeftAmount -= coin.nValue;

        CBitcoinExtKey b58keyDecodeCheck(privkeybip32);
        CExtKey privKey = b58keyDecodeCheck.GetKey();
        if (!privKey.key.IsValid()) {
            std::stringstream ss;
            ss << "Failed to add key/pubkey, private key is invalid, idx=" << i;
            sErr = ss.str();
            return false;
        }
        CExtPubKey pubKey = privKey.Neuter();
        if (!pubKey.pubkey.IsValid()) {
            std::stringstream ss;
            ss << "Failed to add key/pubkey, public key is invalid, idx=" << i;
            sErr = ss.str();
            return false;
        }

        if (!keyStore.AddKeyPubKey(privKey.key, pubKey.pubkey)) {
            std::stringstream ss;
            ss << "Failed to add key/pubkey, idx=" << i;
            sErr = ss.str();
            return false;
        }

        i++;
    }

    // check enough coins in inputs
    if (nLeftAmount > 0) {
        std::stringstream ss;
        ss << "Not enough reserve in inputs to next cycle withdraw "
           << nAmountWithFee;
        sErr = ss.str();
        return false;
    }

    int64_t nFeeRet = 1000000 /*common fee deducted from user amount, 1M*/;
    int64_t nAmount = nAmountWithFee - nFeeRet;

    vector<pair<CScript, int64_t> > vecSend;
    CScript scriptPubKey;
    scriptPubKey.SetDestination(address.Get());
    vecSend.push_back(make_pair(scriptPubKey, nAmount));

    int64_t nValue = 0;
    for(const pair<CScript, int64_t>& s : vecSend) {
        if (nValue < 0)
            return false;
        nValue += s.second;
    }

    size_t nNumInputs = 1;

    CTransaction rawTx;

    nNumInputs = vCoins.size();
    if (!nNumInputs) return false;

    // Inputs to be sorted by address
    sort(vCoins.begin(), vCoins.end(), sortByAddress);

    // Collect input addresses
    // Prepare maps for input,available,take
    set<CTxDestination> setInputAddresses;
    vector<CTxDestination> vInputAddresses;
    map<CTxDestination, string> mapBip32Keys;
    map<CTxDestination, int64_t> mapAvailableValuesAt;
    map<CTxDestination, int64_t> mapInputValuesAt;
    map<CTxDestination, int64_t> mapTakeValuesAt;
    int64_t nValueToTakeFromChange = 0;
    for(const CCoinToUse& coin : vCoins) {
        CTxDestination address;
        if(!ExtractDestination(coin.scriptPubKey, address))
            continue;

        setInputAddresses.insert(address); // sorted due to vCoins
        mapBip32Keys[address] = coin.privkeybip32;
        mapAvailableValuesAt[address] = 0;
        mapInputValuesAt[address] = 0;
        mapTakeValuesAt[address] = 0;
    }
    // Get sorted list of input addresses
    for(const CTxDestination& address : setInputAddresses) {
        vInputAddresses.push_back(address);
    }
    sort(vInputAddresses.begin(), vInputAddresses.end(), sortByDestination);
    // Input and available values can be filled in
    for(const CCoinToUse& coin : vCoins) {
        CTxDestination address;
        if(!ExtractDestination(coin.scriptPubKey, address))
            continue;
        int64_t& nValueAvailableAt = mapAvailableValuesAt[address];
        nValueAvailableAt += coin.nAvailableValue;
        int64_t& nValueInputAt = mapInputValuesAt[address];
        nValueInputAt += coin.nValue;
    }

    // Notations for frozen **F**
    {
        // prepare indexes to freeze
        size_t nCoins = vCoins.size();
        size_t nPayees = vecSend.size();
        string out_indexes;
        if (nPayees == 1) { // trick to have triple to use sort
            auto out_index = std::to_string(0+nCoins);
            out_indexes = out_index+":"+out_index+":"+out_index;
        }
        else if (nPayees == 2) { // trick to have triple to use sort
            auto out_index1 = std::to_string(0+nCoins);
            auto out_index2 = std::to_string(1+nCoins);
            out_indexes = out_index1+":"+out_index1+":"+out_index2+":"+out_index2;
        }
        else {
            for(size_t i=0; i<nPayees; i++) {
                if (!out_indexes.empty())
                    out_indexes += ":";
                out_indexes += std::to_string(i+nCoins);
            }
        }
        // Fill vout with freezing instructions
        for(size_t i=0; i<nCoins; i++) {
            CScript scriptPubKey;
            scriptPubKey.push_back(OP_RETURN);
            unsigned char len_bytes = out_indexes.size();
            scriptPubKey.push_back(len_bytes+5);
            scriptPubKey.push_back('*');
            scriptPubKey.push_back('*');
            scriptPubKey.push_back('F');
            scriptPubKey.push_back('*');
            scriptPubKey.push_back('*');
            for (size_t j=0; j< out_indexes.size(); j++) {
                scriptPubKey.push_back(out_indexes[j]);
            }
            rawTx.vout.push_back(CTxOut(PEG_MAKETX_FREEZE_VALUE, scriptPubKey));
        }
        // Value for notary is first taken from reserves sorted by address
        int64_t nValueLeft = nCoins*PEG_MAKETX_FREEZE_VALUE;
        // take reserves in defined order
        for(const CTxDestination& address : vInputAddresses) {
            int64_t nValueAvailableAt = mapAvailableValuesAt[address];
            int64_t& nValueTakeAt = mapTakeValuesAt[address];
            int64_t nValueLeftAt = nValueAvailableAt-nValueTakeAt;
            if (nValueAvailableAt ==0) continue;
            int64_t nValueToTake = nValueLeft;
            if (nValueToTake > nValueLeftAt)
                nValueToTake = nValueLeftAt;

            nValueTakeAt += nValueToTake;
            nValueLeft -= nValueToTake;

            if (nValueLeft == 0) break;
        }
        // if nValueLeft is left - need to be taken from change (liquidity)
        nValueToTakeFromChange += nValueLeft;
    }

    // vouts to the payees
    for(const pair<CScript, int64_t>& s : vecSend) {
        rawTx.vout.push_back(CTxOut(s.second, s.first));
    }

    // Available values - reserves per address
    // vecSend - outputs to be frozen reserve parts

    // Prepare order of inputs
    // For **F** the first is referenced (last input) then others are sorted
    vector<CTxDestination> vAddressesForFrozen;
    CTxDestination addressFrozenRef = vInputAddresses.back();
    vAddressesForFrozen.push_back(addressFrozenRef);
    for(const CTxDestination & address : vInputAddresses) {
        if (address == addressFrozenRef) continue;
        vAddressesForFrozen.push_back(address);
    }

    // Follow outputs and compute taken values
    for(const pair<CScript, int64_t>& s : vecSend) {
        int64_t nValueLeft = s.second;
        // take reserves in defined order
        for(const CTxDestination& address : vAddressesForFrozen) {
            int64_t nValueAvailableAt = mapAvailableValuesAt[address];
            int64_t& nValueTakeAt = mapTakeValuesAt[address];
            int64_t nValueLeftAt = nValueAvailableAt-nValueTakeAt;
            if (nValueAvailableAt ==0) continue;
            int64_t nValueToTake = nValueLeft;
            if (nValueToTake > nValueLeftAt)
                nValueToTake = nValueLeftAt;

            nValueTakeAt += nValueToTake;
            nValueLeft -= nValueToTake;

            if (nValueLeft == 0) break;
        }
        // if nValueLeft is left then is taken from change (liquidity)
        nValueToTakeFromChange += nValueLeft;
    }

    // Calculate change (minus fee, notary and part taken from change)
    int64_t nTakeFromChangeLeft = nValueToTakeFromChange + nFeeRet +1;
    map<CTxDestination, int> mapChangeOutputs;
    for (const CTxDestination& address : vInputAddresses) {
        CScript scriptPubKey;
        scriptPubKey.SetDestination(address);
        int64_t& nValueTakeAt = mapTakeValuesAt[address];
        int64_t nValueInput = mapInputValuesAt[address];
        int64_t nValueChange = nValueInput - nValueTakeAt;
        if (nValueChange > nTakeFromChangeLeft) {
            nValueChange -= nTakeFromChangeLeft;
            nTakeFromChangeLeft = 0;
        }
        if (nValueChange <= nTakeFromChangeLeft) {
            nTakeFromChangeLeft -= nValueChange;
            nValueChange = 0;
        }
        if (nValueChange == 0) continue;
        nValueTakeAt += nValueChange;
        mapChangeOutputs[address] = rawTx.vout.size();
        rawTx.vout.push_back(CTxOut(nValueChange, scriptPubKey));
    }

    // Fill vin
    for(const CCoinToUse& coin : vCoins) {
        rawTx.vin.push_back(CTxIn(coin.txhash,coin.i));
    }

    // notation for exchange control
    {
        string sNotary = "XCH:";
        sNotary += std::to_string(rawTx.vin.size()) + ":";
        CDataStream ss(SER_GETHASH, 0);
        size_t n_inp = rawTx.vin.size();
        for(size_t j=0; j< n_inp; j++) {
            ss << rawTx.vin[j].prevout.hash;
            ss << rawTx.vin[j].prevout.n;
        }
        ss << string("R");
        ss << sAddress;
        ss << nAmount;
        withdrawIdXch = Hash(ss.begin(), ss.end()).GetHex();
        sNotary += withdrawIdXch;
        CScript scriptPubKey;
        scriptPubKey.push_back(OP_RETURN);
        unsigned char len_bytes = sNotary.size();
        scriptPubKey.push_back(len_bytes);
        for (size_t j=0; j< sNotary.size(); j++) {
            scriptPubKey.push_back(sNotary[j]);
        }
        rawTx.vout.push_back(CTxOut(1, scriptPubKey));
    }

    // locktime to next interval
    rawTx.nLockTime = (peglevel_net.nCycle+1) * Params().PegInterval(Params().nPegIntervalProbeHeight) -1;

    // Calculate peg to know 'user' fee
    CFractions feesFractionsCommon(0, CFractions::STD);
    map<int, CFractions> mapTxOutputFractionsSkip;
    if (!computeTxPegForNextCycle(rawTx, peglevel_net, mapAllOutputs,
                                  mapTxOutputFractionsSkip, feesFractionsCommon,
                                  sErr)) {
        return false;
    }

    // first out after F notations (same num as size inputs)
    if (!mapTxOutputFractionsSkip.count(rawTx.vin.size())) {
        sErr = "No withdraw fractions";
        return false;
    }

    // Now all inputs and outputs are know, calculate network fee
    int64_t nFeeNetwork = 200000 + 10000 * (rawTx.vin.size() + rawTx.vout.size());
    int64_t nFeeMaintenance = nFeeRet - nFeeNetwork;
    int64_t nFeeMaintenanceLeft = nFeeMaintenance;
    // Maintenance fee recorded in provided_outputs
    // It is returned - distributed over 'change' outputs
    // First if we can use existing change
    for (const CTxDestination& address : vInputAddresses) {
        if (!mapChangeOutputs.count(address)) continue;
        int nOut = mapChangeOutputs[address];
        int64_t& nValueTakeAt = mapTakeValuesAt[address];
        int64_t nValueInput = mapInputValuesAt[address];
        int64_t nValueForFee = nValueInput - nValueTakeAt;
        if (nValueForFee > nFeeMaintenanceLeft) {
            rawTx.vout[nOut].nValue += nFeeMaintenanceLeft;
            nFeeMaintenanceLeft = 0;
        }
        if (nValueForFee <= nFeeMaintenanceLeft) {
            nFeeMaintenanceLeft -= nValueForFee;
            rawTx.vout[nOut].nValue += nValueForFee;
        }
    }
    // Second if it is still left we need to add change outputs for maintenance fee
    for (const CTxDestination& address : vInputAddresses) {
        if (mapChangeOutputs.count(address)) continue;
        int64_t nValueTakeAt = mapTakeValuesAt[address];
        int64_t nValueInput = mapInputValuesAt[address];
        int64_t nValueForFee = nValueInput - nValueTakeAt;
        if (nValueForFee > nFeeMaintenanceLeft) {
            nValueForFee = nFeeMaintenanceLeft;
            nFeeMaintenanceLeft = 0;
        }
        if (nValueForFee <= nFeeMaintenanceLeft) {
            nFeeMaintenanceLeft -= nValueForFee;
        }
        if (nValueForFee == 0) continue;
        CScript scriptPubKey;
        scriptPubKey.SetDestination(address);
        rawTx.vout.push_back(CTxOut(nValueForFee, scriptPubKey));
    }
    // Can you for maintenance (is not all nFeeMaintenanceLeft consumed):
    nFeeMaintenance -= nFeeMaintenanceLeft;

    // Recalculate peg to update mapTxOutputFractions
    CFractions feesFractionsNet(0, CFractions::STD);
    map<int, CFractions> mapTxOutputFractions;
    if (!computeTxPegForNextCycle(rawTx, peglevel_net, mapAllOutputs,
                                  mapTxOutputFractions, feesFractionsNet,
                                  sErr)) {
        return false;
    }
    CFractions frFeesMaintenance = feesFractionsCommon - feesFractionsNet;
    frMaintenance += frFeesMaintenance;

    // first out after F notations (same num as size inputs)
    if (!mapTxOutputFractions.count(rawTx.vin.size())) {
        sErr = "No withdraw fractions (2)";
        return false;
    }

    // signing the transaction to get it ready for broadcast
    int nIn = 0;
    for(const CCoinToUse& coin : vCoins) {
        if (!SignSignature(keyStore, coin.scriptPubKey, rawTx, nIn++)) {
            std::stringstream ss;
            ss << "Fail on signing input " << nIn-1;
            sErr = ss.str();
            return false;
        }
    }

    // first out after F notations (same num as size inputs)
    CFractions frProcessed = mapTxOutputFractions[rawTx.vin.size()] + feesFractionsCommon;

    if (frRequested.Total() != nAmountWithFee) {
        std::stringstream ss;
        ss << "Mismatch requested and amount_with_fee " << frRequested.Total()
           << " " << nAmountWithFee;
        sErr = ss.str();
        return false;
    }
    if (frProcessed.Total() != nAmountWithFee) {
        std::stringstream ss;
        ss << "Mismatch processed and amount_with_fee " << frProcessed.Total()
           << " " << nAmountWithFee;
        sErr = ss.str();
        return false;
    }

    // save txouts
    string sTxhash = rawTx.GetHash().GetHex();
    // skip F and first (withdraw) and last (xch id)
    for (size_t i=rawTx.vin.size()+1; i< rawTx.vout.size()-1; i++) {
        string txout = sTxhash+":"+std::to_string(i);
        CPegData pdTxout;
        pdTxout.fractions = mapTxOutputFractions[i];
        pdTxout.fractions.sReturnAddr = HexStr(
            rawTx.vout[i].scriptPubKey.begin(),
            rawTx.vout[i].scriptPubKey.end()
        );
        pdTxout.peglevel = peglevel_exchange;
        pdTxout.nLiquid = pdTxout.fractions.High(peglevel_exchange);
        pdTxout.nReserve = pdTxout.fractions.Low(peglevel_exchange);

        CTxDestination address;
        if(!ExtractDestination(rawTx.vout[i].scriptPubKey, address))
            continue;
        string privkeybip32 = mapBip32Keys[address];
        if (privkeybip32.empty())
            continue;

        txouts.push_back(std::make_tuple(txout, pdTxout, privkeybip32));
    }

    pdBalance.fractions -= frRequested;
    pdExchange.fractions -= frRequested;
    pdPegShift.fractions += (frRequested - frProcessed);
    saveMaintenance(pdPegShift.fractions.sReturnAddr,
                    frMaintenance,
                    peglevel_net);

    pdBalance.nReserve -= nAmountWithFee;

    // consume reserve part of pegshift by balance
    // as computation were completed by pegnext it may use fractions
    // of current liquid - at current supply not to consume these fractions
//    consumereservepegshift(pdBalance.fractions,
//                           pdExchange.fractions,
//                           pdPegShift.fractions,
//                           peglevel_exchange);

    pdExchange.peglevel = peglevel_exchange;
    pdExchange.nLiquid = pdExchange.fractions.High(peglevel_exchange);
    pdExchange.nReserve = pdExchange.fractions.Low(peglevel_exchange);

    pdProcessed = CPegData();
    pdProcessed.fractions = frProcessed;
    pdProcessed.peglevel = peglevel_exchange;
    pdProcessed.nLiquid = pdProcessed.fractions.High(peglevel_exchange);
    pdProcessed.nReserve = pdProcessed.fractions.Low(peglevel_exchange);

    pdRequested = CPegData();
    pdRequested.fractions = frRequested;
    pdRequested.peglevel = peglevel_exchange;
    pdRequested.nLiquid = pdRequested.fractions.High(peglevel_exchange);
    pdRequested.nReserve = pdRequested.fractions.Low(peglevel_exchange);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << rawTx;

    rawtxstr = HexStr(ss.begin(), ss.end());
    withdrawTxout = sTxhash+":"+std::to_string(rawTx.vin.size());

    return true;
}


} // namespace
