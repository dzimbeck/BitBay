// Copyright (c) 2019 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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
using namespace json_spirit;

void printpegshift(const CFractions & frPegShift,
                   const CPegLevel & peglevel,
                   Object & result);

void printpegbalance(const CPegData & pegdata,
                     Object & result,
                     string prefix);

string scripttoaddress(const CScript& scriptPubKey,
                       bool* ptrIsNotary,
                       string* ptrNotary);

static void consumepegshift(CFractions & frBalance, 
                            CFractions & frExchange, 
                            CFractions & frPegShift,
                            const CFractions & frPegShiftInput) {
    int64_t nPegShiftPositive = 0;
    int64_t nPegShiftNegative = 0;
    CFractions frPegShiftPositive = frPegShiftInput.Positive(&nPegShiftPositive);
    CFractions frPegShiftNegative = frPegShiftInput.Negative(&nPegShiftNegative);
    CFractions frPegShiftNegativeConsume = frPegShiftNegative & (-frBalance);
    int64_t nPegShiftNegativeConsume = frPegShiftNegativeConsume.Total();
    int64_t nPegShiftPositiveConsume = frPegShiftPositive.Total();
    if ((-nPegShiftNegativeConsume) > nPegShiftPositiveConsume) {
        CFractions frToPositive = -frPegShiftNegativeConsume; 
        frToPositive = frToPositive.RatioPart(nPegShiftPositiveConsume);
        frPegShiftNegativeConsume = -frToPositive;
        nPegShiftNegativeConsume = frPegShiftNegativeConsume.Total();
    }
    nPegShiftPositiveConsume = -nPegShiftNegativeConsume;
    CFractions frPegShiftPositiveConsume = frPegShiftPositive.RatioPart(nPegShiftPositiveConsume);
    CFractions frPegShiftConsume = frPegShiftNegativeConsume + frPegShiftPositiveConsume;
    
    frBalance += frPegShiftConsume;
    frExchange += frPegShiftConsume;
    frPegShift -= frPegShiftConsume;
}

static void consumereservepegshift(CFractions & frBalance, 
                                   CFractions & frExchange, 
                                   CFractions & frPegShift,
                                   const CPegLevel & peglevel_exchange)
{
    int nSupplyEffective = peglevel_exchange.nSupply + peglevel_exchange.nShift;
    
    CFractions frPegShiftReserve = frPegShift.LowPart(nSupplyEffective, nullptr);
    consumepegshift(frBalance, frExchange, frPegShift, frPegShiftReserve);

    if (frPegShift.Positive(nullptr).Total() != -frPegShift.Negative(nullptr).Total()) {
        throw JSONRPCError(RPC_MISC_ERROR,
                           strprintf("Mismatch pegshift parts (%d - %d)",
                                     frPegShift.Positive(nullptr).Total(),
                                     frPegShift.Negative(nullptr).Total()));
    }
}

static void consumeliquidpegshift(CFractions & frBalance, 
                                  CFractions & frExchange, 
                                  CFractions & frPegShift,
                                  const CPegLevel & peglevel_exchange)
{
    int nSupplyEffective = peglevel_exchange.nSupply + peglevel_exchange.nShift;
    bool fPartial = peglevel_exchange.nShiftLastPart >0 && peglevel_exchange.nShiftLastTotal >0;
    if (fPartial) {
        nSupplyEffective++;
    }
    
    CFractions frPegShiftLiquid = frPegShift.HighPart(nSupplyEffective, nullptr);
    consumepegshift(frBalance, frExchange, frPegShift, frPegShiftLiquid);

    if (frPegShift.Positive(nullptr).Total() != -frPegShift.Negative(nullptr).Total()) {
        throw JSONRPCError(RPC_MISC_ERROR,
                           strprintf("Mismatch pegshift parts (%d - %d)",
                                     frPegShift.Positive(nullptr).Total(),
                                     frPegShift.Negative(nullptr).Total()));
    }
}

class CCoinToUse
{
public:
    uint256     txhash;
    uint64_t    i;
    int64_t     nValue;
    int64_t     nAvailableValue;
    CScript     scriptPubKey;
    int         nCycle;

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

static void cleanupConsumed(const set<uint320> & setConsumedInputs,
                            const set<uint320> & setAllOutputs,
                            string & sConsumedInputs)
{
    set<uint320> setConsumedInputsNew;
    std::set_intersection(setConsumedInputs.begin(), setConsumedInputs.end(),
                          setAllOutputs.begin(), setAllOutputs.end(),
                          std::inserter(setConsumedInputsNew,setConsumedInputsNew.begin()));
    sConsumedInputs.clear();
    for(const uint320& fkey : setConsumedInputsNew) {
        if (!sConsumedInputs.empty()) sConsumedInputs += ",";
        sConsumedInputs += fkey.GetHex();
    }
}

static void cleanupProvided(const set<uint320> & setWalletOutputs,
                            map<uint320,CCoinToUse> & mapProvidedOutputs)
{
    map<uint320,CCoinToUse> mapProvidedOutputsNew;
    for(const pair<uint320,CCoinToUse> & item : mapProvidedOutputs) {
        if (setWalletOutputs.count(item.first)) continue;
        mapProvidedOutputsNew.insert(item);
    }
    mapProvidedOutputs = mapProvidedOutputsNew;
}

static void getAvailableCoins(const set<uint320> & setConsumedInputs,
                              int nCycleNow,
                              set<uint320> & setAllOutputs,
                              set<uint320> & setWalletOutputs,
                              map<uint320,CCoinToUse> & mapAllOutputs)
{
    vector<COutput> vecCoins;
    pwalletMain->AvailableCoins(vecCoins, false, true, NULL);
    for(const COutput& coin : vecCoins)
    {
        auto txhash = coin.tx->GetHash();
        auto fkey = uint320(txhash, coin.i);
        setAllOutputs.insert(fkey);
        setWalletOutputs.insert(fkey);
        if (setConsumedInputs.count(fkey)) continue; // already used
        CCoinToUse & out = mapAllOutputs[fkey];
        out.i = coin.i;
        out.txhash = txhash;
        out.nValue = coin.tx->vout[coin.i].nValue;
        out.scriptPubKey = coin.tx->vout[coin.i].scriptPubKey;
        out.nCycle = nCycleNow;
    }
}

static void parseConsumedAndProvided(const string & sConsumedInputs,
                                     const string & sProvidedOutputs,
                                     int nCycleNow,
                                     int64_t & nMaintenance,
                                     CFractions & frMaintenance,
                                     set<uint320> & setAllOutputs,
                                     set<uint320> & setConsumedInputs,
                                     map<uint320,CCoinToUse> & mapProvidedOutputs)
{
    nMaintenance = 0;
    vector<string> vConsumedInputsArgs;
    vector<string> vProvidedOutputsArgs;
    vector<string> vMaintenanceArgs;
    
    boost::split(vConsumedInputsArgs, sConsumedInputs, boost::is_any_of(","));
    boost::split(vProvidedOutputsArgs, sProvidedOutputs, boost::is_any_of(","));
    for(string sConsumedInput : vConsumedInputsArgs) {
        setConsumedInputs.insert(uint320(sConsumedInput));
    }
    
    for(string sProvidedOutput : vProvidedOutputsArgs) {
        string sAccountMaintenancePrefix = "accountmaintenance:";
        if (boost::starts_with(sProvidedOutput, sAccountMaintenancePrefix)) {
            boost::split(vMaintenanceArgs, sProvidedOutput, boost::is_any_of(":"));
            if (vMaintenanceArgs.size() <3) continue;
            auto sValueMaintenance = vMaintenanceArgs[1];
            char * pEnd = nullptr;
            int64_t nConv = strtoll(sValueMaintenance.c_str(), &pEnd, 0);
            bool fOk = !(pEnd == sValueMaintenance.c_str()) && nConv >= 0;
            if (fOk) {
                CPegData pdLoad(vMaintenanceArgs[2]);
                if (pdLoad.IsValid()) {
                    nMaintenance = nConv;
                    frMaintenance = pdLoad.fractions;
                }
            }
            continue;
        }
        vector<unsigned char> outData(ParseHex(sProvidedOutput));
        CDataStream ssData(outData, SER_NETWORK, PROTOCOL_VERSION);
        CCoinToUse out;
        try { ssData >> out; }
        catch (std::exception &) { continue; }
        if (out.nCycle != nCycleNow) { continue; }
        auto fkey = uint320(out.txhash, out.i);
        if (setConsumedInputs.count(fkey)) { continue; }
        mapProvidedOutputs[fkey] = out;
        setAllOutputs.insert(fkey);
    }
}

static void computeTxPegForNextCycle(const CTransaction & rawTx,
                                     const CPegLevel & peglevel_net,
                                     CTxDB & txdb,
                                     CPegDB & pegdb,
                                     map<uint320,CCoinToUse> & mapAllOutputs,
                                     map<int, CFractions> & mapTxOutputFractions,
                                     CFractions & feesFractions)
{
    MapPrevOut mapInputs;
    MapPrevTx mapTxInputs;
    MapFractions mapInputsFractions;
    MapFractions mapOutputFractions;
    string sPegFailCause;

    size_t n_vin = rawTx.vin.size();

    for (unsigned int i = 0; i < n_vin; i++)
    {
        const COutPoint & prevout = rawTx.vin[i].prevout;
        auto fkey = uint320(prevout.hash, prevout.n);

        if (mapAllOutputs.count(fkey)) {
            const CCoinToUse& coin = mapAllOutputs[fkey];
            CTxOut out(coin.nValue, coin.scriptPubKey);
            mapInputs[fkey] = out;
        }
        else {
            // Read txindex
            CTxIndex& txindex = mapTxInputs[prevout.hash].first;
            if (!txdb.ReadTxIndex(prevout.hash, txindex)) {
                continue;
            }
            // Read txPrev
            CTransaction& txPrev = mapTxInputs[prevout.hash].second;
            if (!txPrev.ReadFromDisk(txindex.pos)) {
                continue;
            }

            if (prevout.n >= txPrev.vout.size()) {
                continue;
            }

            mapInputs[fkey] = txPrev.vout[prevout.n];
        }

        CFractions& fractions = mapInputsFractions[fkey];
        fractions = CFractions(mapInputs[fkey].nValue, CFractions::VALUE);
        pegdb.ReadFractions(fkey, fractions);
    }

    bool peg_ok = CalculateStandardFractions(rawTx,
                                             peglevel_net.nSupplyNext,
                                             pindexBest->nTime,
                                             mapInputs,
                                             mapInputsFractions,
                                             mapOutputFractions,
                                             feesFractions,
                                             sPegFailCause);
    if (!peg_ok) {
        throw JSONRPCError(RPC_MISC_ERROR,
                           strprintf("Fail on calculations of tx fractions (cause=%s)",
                                     sPegFailCause.c_str()));
    }

    size_t n_out = rawTx.vout.size();
    for(size_t i=0; i< n_out; i++) {
        auto fkey = uint320(rawTx.GetHash(), i);
        mapTxOutputFractions[i] = mapOutputFractions[fkey];
    }
}

static void prepareConsumedProvided(map<uint320,CCoinToUse> & mapProvidedOutputs,
                                    const CTransaction & rawTx,
                                    string sAddress,
                                    int nCycleNow,
                                    int64_t nMaintenance,
                                    const CFractions & frMaintenance,
                                    const CPegLevel & peglevel,
                                    string & sConsumedInputs,
                                    string & sProvidedOutputs)
{
    for (size_t i=0; i< rawTx.vin.size(); i++) {
        const COutPoint & prevout = rawTx.vin[i].prevout;
        auto fkey = uint320(prevout.hash, prevout.n);
        if (mapProvidedOutputs.count(fkey)) mapProvidedOutputs.erase(fkey);
        if (!sConsumedInputs.empty()) sConsumedInputs += ",";
        sConsumedInputs += fkey.GetHex();
    }

    sProvidedOutputs.clear();
    for(const pair<uint320,CCoinToUse> & item : mapProvidedOutputs) {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << item.second;
        if (!sProvidedOutputs.empty()) sProvidedOutputs += ",";
        sProvidedOutputs += HexStr(ss.begin(), ss.end());
    }

    size_t n_out = rawTx.vout.size();
    for (size_t i=0; i< n_out; i++) {
        string sNotary;
        bool fNotary = false;
        string sToAddress = scripttoaddress(rawTx.vout[i].scriptPubKey, &fNotary, &sNotary);
        if (fNotary) continue;
        if (sToAddress == sAddress) continue;

        CCoinToUse out;
        out.i = i;
        out.txhash = rawTx.GetHash();
        out.nValue = rawTx.vout[i].nValue;
        out.scriptPubKey = rawTx.vout[i].scriptPubKey;
        out.nCycle = nCycleNow;
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << out;
        if (!sProvidedOutputs.empty()) sProvidedOutputs += ",";
        sProvidedOutputs += HexStr(ss.begin(), ss.end());
    }
    if (!sProvidedOutputs.empty()) sProvidedOutputs += ",";
    
    CPegData pdSave;
    pdSave.fractions = frMaintenance;
    pdSave.peglevel = peglevel;
    pdSave.nReserve = frMaintenance.Low(peglevel);
    pdSave.nLiquid = frMaintenance.High(peglevel);
    
    sProvidedOutputs += "accountmaintenance:"
            +std::to_string(nMaintenance)
            +":"
            +pdSave.ToString();
}

Value prepareliquidwithdraw(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 8)
        throw runtime_error(
            "prepareliquidwithdraw "
                "<balance_pegdata_base64> "
                "<exchange_pegdata_base64> "
                "<pegshift_pegdata_base64> "
                "<amount_with_fee> "
                "<address> "
                "<peglevel_hex> "
                "<consumed_inputs> "
                "<provided_outputs>\n"
            );
    
    string balance_pegdata64 = params[0].get_str();
    string exchange_pegdata64 = params[1].get_str();
    string pegshift_pegdata64 = params[2].get_str();
    int64_t nAmountWithFee = params[3].get_int64();
    string sAddress = params[4].get_str();

    CBitcoinAddress address(sAddress);
    if (!address.IsValid()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid BitBay address");
    }
    
    if (nAmountWithFee <0) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Requested to withdraw negative %d",
                                                     nAmountWithFee));
    }
    if (nAmountWithFee <1000000+2) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Requested to withdraw less than 1M fee %d",
                                                     nAmountWithFee));
    }
    
    string peglevel_hex = params[5].get_str();
    
    // exchange peglevel
    CPegLevel peglevel_exchange(peglevel_hex);
    if (!peglevel_exchange.IsValid()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Can not unpack peglevel");
    }

    int nSupplyNow = pindexBest ? pindexBest->nPegSupplyIndex : 0;
    int nSupplyNext = pindexBest ? pindexBest->GetNextIntervalPegSupplyIndex() : 0;
    int nSupplyNextNext = pindexBest ? pindexBest->GetNextNextIntervalPegSupplyIndex() : 0;
    
    int nPegInterval = Params().PegInterval(nBestHeight);
    int nCycleNow = nBestHeight / nPegInterval;
    
    // network peglevel
    CPegLevel peglevel_net(nCycleNow,
                           nCycleNow-1,
                           0 /*buffer=0, network level*/,
                           nSupplyNow,
                           nSupplyNext,
                           nSupplyNextNext);
    
    CPegData pdBalance(balance_pegdata64);
    if (!pdBalance.IsValid()) {
        string err = "Can not unpack 'balance' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }

    CPegData pdExchange(exchange_pegdata64);
    if (!pdExchange.IsValid()) {
        string err = "Can not unpack 'exchange' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }

    CPegData pdPegShift(pegshift_pegdata64);
    if (!pdPegShift.IsValid()) {
        string err = "Can not unpack 'pegshift' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }
    
    if (!balance_pegdata64.empty() && pdBalance.peglevel.nCycle != peglevel_exchange.nCycle) {
        throw JSONRPCError(RPC_MISC_ERROR, "Balance has other cycle than peglevel");
    }

    if (nAmountWithFee > pdBalance.nLiquid) {
        throw JSONRPCError(RPC_MISC_ERROR, 
                           strprintf("Not enough liquid %d on 'balance' to withdraw %d",
                                     pdBalance.nLiquid,
                                     nAmountWithFee));
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
            throw JSONRPCError(RPC_MISC_ERROR, 
                               strprintf("Mismatch on nPartialLiquid %d",
                                         nPartialLiquid));
        }
        
        frBalanceLiquid.f[nSupplyEffective-1] = nPartialLiquid;
    }
    
    if (frBalanceLiquid.Total() < nAmountWithFee) {
        throw JSONRPCError(RPC_MISC_ERROR, 
                           strprintf("Not enough liquid(1) %d  on 'src' to move %d",
                                     frBalanceLiquid.Total(),
                                     nAmountWithFee));
    }
    
    CFractions frAmount = frBalanceLiquid.RatioPart(nAmountWithFee);
    CFractions frRequested = frAmount;

    // inputs, outputs
    string sConsumedInputs = params[6].get_str();
    string sProvidedOutputs = params[7].get_str();

    int64_t nMaintenance = 0;
    CFractions frMaintenance(0, CFractions::VALUE);
    set<uint320> setAllOutputs;
    set<uint320> setConsumedInputs;
    map<uint320,CCoinToUse> mapProvidedOutputs;
    parseConsumedAndProvided(sConsumedInputs, sProvidedOutputs, nCycleNow,
                             nMaintenance, frMaintenance,
                             setAllOutputs, setConsumedInputs, mapProvidedOutputs);
    
    if (!pindexBest) {
        throw JSONRPCError(RPC_MISC_ERROR, "Blockchain is not in sync");
    }
    
    assert(pwalletMain != NULL);
   
    CTxDB txdb("r");
    CPegDB pegdb("r");
    
    // make list of 'rated' outputs, multimap with key 'distortion'
    // they are rated to be less distorted towards coins to withdraw
    
    map<uint320,CCoinToUse> mapAllOutputs = mapProvidedOutputs;
    set<uint320> setWalletOutputs;
    
    map<uint320,int64_t> mapAvailableLiquid;
    
    // get available coins
    getAvailableCoins(setConsumedInputs, nCycleNow,
                      setAllOutputs, setWalletOutputs, mapAllOutputs);
    // clean-up consumed, intersect with (wallet+provided)
    cleanupConsumed(setConsumedInputs, setAllOutputs, sConsumedInputs);
    // clean-up provided, remove what is already in wallet
    cleanupProvided(setWalletOutputs, mapProvidedOutputs);
    
    // read available coin fractions to rate
    // also consider only coins with are not less than 5% (and fit 20 inputs max)
    // for liquid calculations we use network peg in next interval
    multimap<double,CCoinToUse> ratedOutputs;
    for(const pair<uint320,CCoinToUse>& item : mapAllOutputs) {
        uint320 fkey = item.first;
        CFractions frOut(0, CFractions::VALUE);
        if (!pegdb.ReadFractions(fkey, frOut, true)) {
            continue;
        }
        
        int64_t nAvailableLiquid = 0;
        frOut = frOut.HighPart(peglevel_net.nSupplyNext, &nAvailableLiquid);
        
        if (nAvailableLiquid < (nAmountWithFee / 20)) {
            continue;
        }
        
        double distortion = frOut.Distortion(frAmount);
        ratedOutputs.insert(pair<double,CCoinToUse>(distortion, item.second));
        mapAvailableLiquid[fkey] = nAvailableLiquid;
    }

    // get available value for selected coins
    set<CCoinToUse> setCoins;
    int64_t nLeftAmount = nAmountWithFee;
    auto it = ratedOutputs.begin();
    for (; it != ratedOutputs.end(); ++it) {
        CCoinToUse out = (*it).second;
        auto txhash = out.txhash;
        auto fkey = uint320(txhash, out.i);
        
        nLeftAmount -= mapAvailableLiquid[fkey];
        out.nAvailableValue = mapAvailableLiquid[fkey];
        setCoins.insert(out);
        
        if (nLeftAmount <= 0) {
            break;
        }
    }
    
    if (nLeftAmount > 0) {
        throw JSONRPCError(RPC_MISC_ERROR, 
                           strprintf("Not enough liquid or coins are too fragmented  on 'exchange' to withdraw %d",
                                     nAmountWithFee));
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
    
    nNumInputs = setCoins.size();
    if (!nNumInputs) return false;
    
    // Inputs to be sorted by address
    vector<CCoinToUse> vCoins;
    for(const CCoinToUse& coin : setCoins) {
        vCoins.push_back(coin);
    }
    sort(vCoins.begin(), vCoins.end(), sortByAddress);
    
    // Collect input addresses
    // Prepare maps for input,available,take
    set<CTxDestination> setInputAddresses;
    vector<CTxDestination> vInputAddresses;
    map<CTxDestination, int64_t> mapAvailableValuesAt;
    map<CTxDestination, int64_t> mapInputValuesAt;
    map<CTxDestination, int64_t> mapTakeValuesAt;
    for(const CCoinToUse& coin : vCoins) {
        CTxDestination address;
        if(!ExtractDestination(coin.scriptPubKey, address))
            continue;
        setInputAddresses.insert(address); // sorted due to vCoins
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
    
    CReserveKey reservekey(pwalletMain);
    reservekey.ReturnKey();

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
    string sTxid;
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
        sTxid = Hash(ss.begin(), ss.end()).GetHex();
        sNotary += sTxid;
        CScript scriptPubKey;
        scriptPubKey.push_back(OP_RETURN);
        unsigned char len_bytes = sNotary.size();
        scriptPubKey.push_back(len_bytes);
        for (size_t j=0; j< sNotary.size(); j++) {
            scriptPubKey.push_back(sNotary[j]);
        }
        rawTx.vout.push_back(CTxOut(1, scriptPubKey));
    }

    // Calculate peg to know 'user' fee
    CFractions feesFractionsCommon(0, CFractions::STD);
    map<int, CFractions> mapTxOutputFractionsSkip;
    computeTxPegForNextCycle(rawTx, peglevel_net, txdb, pegdb, mapAllOutputs,
                             mapTxOutputFractionsSkip, feesFractionsCommon);
    
    // for liquid just first output
    if (!mapTxOutputFractionsSkip.count(0)) {
        throw JSONRPCError(RPC_MISC_ERROR, "No withdraw fractions");
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
    computeTxPegForNextCycle(rawTx, peglevel_net, txdb, pegdb, mapAllOutputs,
                             mapTxOutputFractions, feesFractionsNet);
    CFractions frFeesMaintenance = feesFractionsCommon - feesFractionsNet;
    frMaintenance += frFeesMaintenance;
    
    // for liquid just first output
    if (!mapTxOutputFractions.count(0)) {
        throw JSONRPCError(RPC_MISC_ERROR, "No withdraw fractions");
    }
    
    // signing the transaction to get it ready for broadcast
    int nIn = 0;
    for(const CCoinToUse& coin : vCoins) {
        if (!SignSignature(*pwalletMain, coin.scriptPubKey, rawTx, nIn++)) {
            throw JSONRPCError(RPC_MISC_ERROR, 
                               strprintf("Fail on signing input (%d)", nIn-1));
        }
    }
    // for liquid just first output
    CFractions frProcessed = mapTxOutputFractions[0] + feesFractionsCommon;

    if (frRequested.Total() != nAmountWithFee) {
        throw JSONRPCError(RPC_MISC_ERROR, 
                           strprintf("Mismatch requested and amount_with_fee (%d - %d)",
                                     frRequested.Total(), nAmountWithFee));
    }
    if (frProcessed.Total() != nAmountWithFee) {
        throw JSONRPCError(RPC_MISC_ERROR, 
                           strprintf("Mismatch processed and amount_with_fee (%d - %d)",
                                     frProcessed.Total(), nAmountWithFee));
    }
    
    // save fractions
    string sTxhash = rawTx.GetHash().GetHex();

    // write provided outputs to pegdb
    {
        CPegDB pegdbrw;
        for (size_t i=1; i< rawTx.vout.size(); i++) { // skip 0 (withdraw)
            // save these outputs in pegdb, so they can be used in next withdraws
            auto fkey = uint320(rawTx.GetHash(), i);
            pegdbrw.WriteFractions(fkey, mapTxOutputFractions[i]);
        }
    }
    // get list of consumed and provided outputs
    nMaintenance += nFeeMaintenance;
    prepareConsumedProvided(mapProvidedOutputs, rawTx, sAddress, nCycleNow, 
                            nMaintenance, frMaintenance, peglevel_exchange,
                            sConsumedInputs, sProvidedOutputs);
    
    pdBalance.fractions -= frRequested;
    pdExchange.fractions -= frRequested;
    pdPegShift.fractions += (frRequested - frProcessed);
    
    pdBalance.nLiquid -= nAmountWithFee;
    
    // consume liquid part of pegshift by balance
    // as computation were completed by pegnext it may use fractions
    // of current reserves - at current supply not to consume these fractions
    consumeliquidpegshift(pdBalance.fractions, 
                          pdExchange.fractions, 
                          pdPegShift.fractions, 
                          peglevel_exchange);

    pdExchange.peglevel = peglevel_exchange;
    pdExchange.nLiquid = pdExchange.fractions.High(peglevel_exchange);
    pdExchange.nReserve = pdExchange.fractions.Low(peglevel_exchange);

    CPegData pdProcessed;
    pdProcessed.fractions = frProcessed;
    pdProcessed.peglevel = peglevel_exchange;
    pdProcessed.nLiquid = pdProcessed.fractions.High(peglevel_exchange);
    pdProcessed.nReserve = pdProcessed.fractions.Low(peglevel_exchange);

    CPegData pdRequested;
    pdRequested.fractions = frRequested;
    pdRequested.peglevel = peglevel_exchange;
    pdRequested.nLiquid = pdRequested.fractions.High(peglevel_exchange);
    pdRequested.nReserve = pdRequested.fractions.Low(peglevel_exchange);
    
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << rawTx;
    
    string txstr = HexStr(ss.begin(), ss.end());
    
    Object result;
    result.push_back(Pair("withdraw_id", sTxid));
    result.push_back(Pair("completed", true));
    result.push_back(Pair("txhash", sTxhash));
    result.push_back(Pair("rawtx", txstr));

    result.push_back(Pair("consumed_inputs", sConsumedInputs));
    result.push_back(Pair("provided_outputs", sProvidedOutputs));

    result.push_back(Pair("created_on_cycle", peglevel_exchange.nCycle));
    result.push_back(Pair("broadcast_on_cycle", peglevel_exchange.nCycle+1));
    
    result.push_back(Pair("created_on_peg", peglevel_net.nSupply));
    result.push_back(Pair("broadcast_on_peg", peglevel_net.nSupplyNext));
    
    printpegbalance(pdBalance, result, "balance_");
    printpegbalance(pdExchange, result, "exchange_");
    printpegbalance(pdProcessed, result, "processed_");
    printpegbalance(pdRequested, result, "requested_");
    
    printpegshift(pdPegShift.fractions, peglevel_net, result);
    
    return result;
}

Value preparereservewithdraw(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 8)
        throw runtime_error(
            "preparereservewithdraw "
                "<balance_pegdata_base64> "
                "<exchange_pegdata_base64> "
                "<pegshift_pegdata_base64> "
                "<amount_with_fee> "
                "<address> "
                "<peglevel_hex> "
                "<consumed_inputs> "
                "<provided_outputs>\n"
            );
    
    string balance_pegdata64 = params[0].get_str();
    string exchange_pegdata64 = params[1].get_str();
    string pegshift_pegdata64 = params[2].get_str();
    int64_t nAmountWithFee = params[3].get_int64();
    string sAddress = params[4].get_str();

    CBitcoinAddress address(sAddress);
    if (!address.IsValid()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid BitBay address");
    }
    
    if (nAmountWithFee <0) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Requested to withdraw negative %d",
                                                     nAmountWithFee));
    }
    if (nAmountWithFee <1000000+2) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Requested to withdraw less than 1M fee %d",
                                                     nAmountWithFee));
    }
    
    string peglevel_hex = params[5].get_str();

    // exchange peglevel
    CPegLevel peglevel_exchange(peglevel_hex);
    if (!peglevel_exchange.IsValid()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Can not unpack peglevel");
    }

    int nSupplyNow = pindexBest ? pindexBest->nPegSupplyIndex : 0;
    int nSupplyNext = pindexBest ? pindexBest->GetNextIntervalPegSupplyIndex() : 0;
    int nSupplyNextNext = pindexBest ? pindexBest->GetNextNextIntervalPegSupplyIndex() : 0;
    
    int nPegInterval = Params().PegInterval(nBestHeight);
    int nCycleNow = nBestHeight / nPegInterval;
    
    // network peglevel
    CPegLevel peglevel_net(nCycleNow,
                           nCycleNow-1,
                           0 /*buffer=0, network level*/,
                           nSupplyNow,
                           nSupplyNext,
                           nSupplyNextNext);
    
    CPegData pdBalance(balance_pegdata64);
    if (!pdBalance.IsValid()) {
        string err = "Can not unpack 'balance' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }

    CPegData pdExchange(exchange_pegdata64);
    if (!pdExchange.IsValid()) {
        string err = "Can not unpack 'exchange' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }

    CPegData pdPegShift(pegshift_pegdata64);
    if (!pdPegShift.IsValid()) {
        string err = "Can not unpack 'pegshift' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }
    
    if (!balance_pegdata64.empty() && pdBalance.peglevel.nCycle != peglevel_exchange.nCycle) {
        throw JSONRPCError(RPC_MISC_ERROR, "Balance has other cycle than peglevel");
    }
    
    if (nAmountWithFee > pdBalance.nReserve) {
        throw JSONRPCError(RPC_MISC_ERROR, 
                           strprintf("Not enough reserve %d on 'balance' to withdraw %d",
                                     pdBalance.nReserve,
                                     nAmountWithFee));
    }
    
    int nSupplyEffective = peglevel_exchange.nSupply + peglevel_exchange.nShift;
    bool fPartial = peglevel_exchange.nShiftLastPart >0 && peglevel_exchange.nShiftLastTotal >0;
    
    CFractions frBalanceReserve = pdBalance.fractions.LowPart(nSupplyEffective, nullptr);
    if (fPartial) {
        int64_t nPartialReserve = pdBalance.nReserve - frBalanceReserve.Total();
        if (nPartialReserve < 0) {
            throw JSONRPCError(RPC_MISC_ERROR, 
                               strprintf("Mismatch on nPartialReserve %d",
                                         nPartialReserve));
        }

        frBalanceReserve.f[nSupplyEffective] = nPartialReserve;
    }

    CFractions frAmount = frBalanceReserve.RatioPart(nAmountWithFee);
    CFractions frRequested = frAmount;

    // inputs, outputs
    string sConsumedInputs = params[6].get_str();
    string sProvidedOutputs = params[7].get_str();

    int64_t nMaintenance = 0;
    CFractions frMaintenance(0, CFractions::VALUE);
    set<uint320> setAllOutputs;
    set<uint320> setConsumedInputs;
    map<uint320,CCoinToUse> mapProvidedOutputs;
    parseConsumedAndProvided(sConsumedInputs, sProvidedOutputs, nCycleNow,
                             nMaintenance, frMaintenance,
                             setAllOutputs, setConsumedInputs, mapProvidedOutputs);
    
    if (!pindexBest) {
        throw JSONRPCError(RPC_MISC_ERROR, "Blockchain is not in sync");
    }
    
    assert(pwalletMain != NULL);
   
    CTxDB txdb("r");
    CPegDB pegdb("r");
    
    // make list of 'rated' outputs, multimap with key 'distortion'
    // they are rated to be less distorted towards coins to withdraw
    
    map<uint320,CCoinToUse> mapAllOutputs = mapProvidedOutputs;
    set<uint320> setWalletOutputs;
        
    // get available coins
    getAvailableCoins(setConsumedInputs, nCycleNow,
                      setAllOutputs, setWalletOutputs, mapAllOutputs);
    // clean-up consumed, intersect with (wallet+provided)
    cleanupConsumed(setConsumedInputs, setAllOutputs, sConsumedInputs);
    // clean-up provided, remove what is already in wallet
    cleanupProvided(setWalletOutputs, mapProvidedOutputs);
    
    // read available coin fractions to rate
    // also consider only coins with are not less than 5% (and 20 inputs max)
    // for reserve calculations we use exchange peglevels
    map<uint320,int64_t> mapAvailableReserve;
    multimap<double,CCoinToUse> ratedOutputs;
    for(const pair<uint320,CCoinToUse>& item : mapAllOutputs) {
        uint320 fkey = item.first;
        CFractions frOut(0, CFractions::VALUE);
        if (!pegdb.ReadFractions(fkey, frOut, true)) {
            continue;
        }
        
        int64_t nAvailableReserve = 0;
        frOut = frOut.LowPart(peglevel_exchange.nSupplyNext, &nAvailableReserve);
        
        if (nAvailableReserve < (nAmountWithFee / 20)) {
            continue;
        }
        
        double distortion = frOut.Distortion(frAmount);
        ratedOutputs.insert(pair<double,CCoinToUse>(distortion, item.second));
        mapAvailableReserve[fkey] = nAvailableReserve;
    }

    // get available value for selected coins
    set<CCoinToUse> setCoins;
    int64_t nLeftAmount = nAmountWithFee;
    auto it = ratedOutputs.begin();
    for (; it != ratedOutputs.end(); ++it) {
        CCoinToUse out = (*it).second;
        auto txhash = out.txhash;
        auto fkey = uint320(txhash, out.i);
        
        nLeftAmount -= mapAvailableReserve[fkey];
        out.nAvailableValue = mapAvailableReserve[fkey];
        setCoins.insert(out);
        
        if (nLeftAmount <= 0) {
            break;
        }
    }
    
    if (nLeftAmount > 0) {
        throw JSONRPCError(RPC_MISC_ERROR, 
                           strprintf("Not enough reserve or coins are too fragmented on 'exchange' to withdraw %d",
                                     nAmountWithFee));
    }
    
    int64_t nFeeRet = 1000000 /*common fee deducted from user amount*/;
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
    
    nNumInputs = setCoins.size();
    if (!nNumInputs) return false;
    
    // Inputs to be sorted by address
    vector<CCoinToUse> vCoins;
    for(const CCoinToUse& coin : setCoins) {
        vCoins.push_back(coin);
    }
    sort(vCoins.begin(), vCoins.end(), sortByAddress);
    
    // Collect input addresses
    // Prepare maps for input,available,take
    set<CTxDestination> setInputAddresses;
    vector<CTxDestination> vInputAddresses;
    map<CTxDestination, int64_t> mapAvailableValuesAt;
    map<CTxDestination, int64_t> mapInputValuesAt;
    map<CTxDestination, int64_t> mapTakeValuesAt;
    int64_t nValueToTakeFromChange = 0;
    for(const CCoinToUse& coin : vCoins) {
        CTxDestination address;
        if(!ExtractDestination(coin.scriptPubKey, address))
            continue;
        setInputAddresses.insert(address); // sorted due to vCoins
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
    
    CReserveKey reservekey(pwalletMain);
    reservekey.ReturnKey();
    
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
    string sTxid;
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
        sTxid = Hash(ss.begin(), ss.end()).GetHex();
        sNotary += sTxid;
        CScript scriptPubKey;
        scriptPubKey.push_back(OP_RETURN);
        unsigned char len_bytes = sNotary.size();
        scriptPubKey.push_back(len_bytes);
        for (size_t j=0; j< sNotary.size(); j++) {
            scriptPubKey.push_back(sNotary[j]);
        }
        rawTx.vout.push_back(CTxOut(1, scriptPubKey));
    }
    
    // Calculate peg to know 'user' fee
    CFractions feesFractionsCommon(0, CFractions::STD);
    map<int, CFractions> mapTxOutputFractionsSkip;
    computeTxPegForNextCycle(rawTx, peglevel_net, txdb, pegdb, mapAllOutputs,
                             mapTxOutputFractionsSkip, feesFractionsCommon);

    // first out after F notations (same num as size inputs)
    if (!mapTxOutputFractionsSkip.count(rawTx.vin.size())) {
        throw JSONRPCError(RPC_MISC_ERROR, "No withdraw fractions");
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
    computeTxPegForNextCycle(rawTx, peglevel_net, txdb, pegdb, mapAllOutputs,
                             mapTxOutputFractions, feesFractionsNet);
    CFractions frFeesMaintenance = feesFractionsCommon - feesFractionsNet;
    frMaintenance += frFeesMaintenance;
    
    // first out after F notations (same num as size inputs)
    if (!mapTxOutputFractions.count(rawTx.vin.size())) {
        throw JSONRPCError(RPC_MISC_ERROR, "No withdraw fractions");
    }
    
    // signing the transaction to get it ready for broadcast
    int nIn = 0;
    for(const CCoinToUse& coin : vCoins) {
        if (!SignSignature(*pwalletMain, coin.scriptPubKey, rawTx, nIn++)) {
            throw JSONRPCError(RPC_MISC_ERROR, 
                               strprintf("Fail on signing input (%d)", nIn-1));
        }
    }
        
    // first out after F notations (same num as size inputs)
    CFractions frProcessed = mapTxOutputFractions[rawTx.vin.size()] + feesFractionsCommon;

    if (frRequested.Total() != nAmountWithFee) {
        throw JSONRPCError(RPC_MISC_ERROR, 
                           strprintf("Mismatch requested and amount_with_fee (%d - %d)",
                                     frRequested.Total(), nAmountWithFee));
    }
    if (frProcessed.Total() != nAmountWithFee) {
        throw JSONRPCError(RPC_MISC_ERROR, 
                           strprintf("Mismatch processed and amount_with_fee (%d - %d)",
                                     frProcessed.Total(), nAmountWithFee));
    }
    
    // save fractions
    string sTxhash = rawTx.GetHash().GetHex();

    // write provided outputs to pegdb
    {
        CPegDB pegdbrw;
        for (size_t i=rawTx.vin.size()+1; i< rawTx.vout.size(); i++) { // skip F notations and withdraw
            // save these outputs in pegdb, so they can be used in next withdraws
            auto fkey = uint320(rawTx.GetHash(), i);
            pegdbrw.WriteFractions(fkey, mapTxOutputFractions[i]);
        }
    }
    // get list of consumed and provided outputs
    nMaintenance += nFeeMaintenance;
    prepareConsumedProvided(mapProvidedOutputs, rawTx, sAddress, nCycleNow, 
                            nMaintenance, frMaintenance, peglevel_exchange,
                            sConsumedInputs, sProvidedOutputs);
    
    pdBalance.fractions -= frRequested;
    pdExchange.fractions -= frRequested;
    pdPegShift.fractions += (frRequested - frProcessed);

    pdBalance.nReserve -= nAmountWithFee;
    
    // consume reserve part of pegshift by balance
    // as computation were completed by pegnext it may use fractions
    // of current liquid - at current supply not to consume these fractions
    consumereservepegshift(pdBalance.fractions, 
                           pdExchange.fractions, 
                           pdPegShift.fractions, 
                           peglevel_exchange);
    
    pdExchange.peglevel = peglevel_exchange;
    pdExchange.nLiquid = pdExchange.fractions.High(peglevel_exchange);
    pdExchange.nReserve = pdExchange.fractions.Low(peglevel_exchange);
    
    CPegData pdProcessed;
    pdProcessed.fractions = frProcessed;
    pdProcessed.peglevel = peglevel_exchange;
    pdProcessed.nLiquid = pdProcessed.fractions.High(peglevel_exchange);
    pdProcessed.nReserve = pdProcessed.fractions.Low(peglevel_exchange);

    CPegData pdRequested;
    pdRequested.fractions = frRequested;
    pdRequested.peglevel = peglevel_exchange;
    pdRequested.nLiquid = pdRequested.fractions.High(peglevel_exchange);
    pdRequested.nReserve = pdRequested.fractions.Low(peglevel_exchange);
    
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << rawTx;
    
    string txstr = HexStr(ss.begin(), ss.end());
    
    Object result;
    result.push_back(Pair("withdraw_id", sTxid));
    result.push_back(Pair("completed", true));
    result.push_back(Pair("txhash", sTxhash));
    result.push_back(Pair("rawtx", txstr));

    result.push_back(Pair("consumed_inputs", sConsumedInputs));
    result.push_back(Pair("provided_outputs", sProvidedOutputs));

    result.push_back(Pair("created_on_cycle", peglevel_exchange.nCycle));
    result.push_back(Pair("broadcast_on_cycle", peglevel_exchange.nCycle+1));
    
    result.push_back(Pair("created_on_peg", peglevel_net.nSupply));
    result.push_back(Pair("broadcast_on_peg", peglevel_net.nSupplyNext));
    
    printpegbalance(pdBalance, result, "balance_");
    printpegbalance(pdExchange, result, "exchange_");
    printpegbalance(pdProcessed, result, "processed_");
    printpegbalance(pdRequested, result, "requested_");
    
    printpegshift(pdPegShift.fractions, peglevel_net, result);
    
    return result;
}

Value checkwithdrawstate(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "checkwithdrawstatus "
                "<withdraw_id>\n"
            );
    
    string withdraw_id = params[0].get_str();
    
    Object result;
    result.push_back(Pair("withdraw_id", withdraw_id));
    
    CPegDB pegdb("r");
    uint256 txid(withdraw_id);
    uint256 txhash;
    bool ok = pegdb.ReadPegTxId(txid, txhash);
    if (ok && txhash != 0) {
        CTxDB txdb("r");
        CTxIndex txindex;
        if (!txdb.ReadTxIndex(txhash, txindex)) {
            throw JSONRPCError(RPC_MISC_ERROR, "ReadTxIndex tx failed");
        }
        int nDepth = txindex.GetDepthInMainChain();
        
        result.push_back(Pair("completed", true));
        result.push_back(Pair("confirmations", nDepth));
        result.push_back(Pair("txhash", txhash.ToString()));
    }
    else {
        result.push_back(Pair("completed", false));
        result.push_back(Pair("confirmations", 0));
        result.push_back(Pair("status", "Transaction is not found in mined blocks"));
    }
    
    return result;
}


Value accountmaintenance(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "accountmaintenance "
                "<pegshift_pegdata_base64> "
                "<consumed_inputs> "
                "<provided_outputs>\n"
            );
    
    string pegshift_pegdata64 = params[0].get_str();

    int nSupplyNow = pindexBest ? pindexBest->nPegSupplyIndex : 0;
    int nSupplyNext = pindexBest ? pindexBest->GetNextIntervalPegSupplyIndex() : 0;
    int nSupplyNextNext = pindexBest ? pindexBest->GetNextNextIntervalPegSupplyIndex() : 0;
    
    int nPegInterval = Params().PegInterval(nBestHeight);
    int nCycleNow = nBestHeight / nPegInterval;
    
    // network peglevel
    CPegLevel peglevel_net(nCycleNow,
                           nCycleNow-1,
                           0,
                           nSupplyNow,
                           nSupplyNext,
                           nSupplyNextNext);
    
    CPegData pdPegShift(pegshift_pegdata64);
    if (!pdPegShift.IsValid()) {
        string err = "Can not unpack 'pegshift' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }
    
    // inputs, outputs
    string sConsumedInputs = params[1].get_str();
    string sProvidedOutputs = params[2].get_str();

    int64_t nMaintenance = 0;
    CFractions frMaintenance(0, CFractions::VALUE);
    set<uint320> setAllOutputs;
    set<uint320> setConsumedInputs;
    map<uint320,CCoinToUse> mapProvidedOutputs;
    parseConsumedAndProvided(sConsumedInputs, sProvidedOutputs, nCycleNow,
                             nMaintenance, frMaintenance,
                             setAllOutputs, setConsumedInputs, mapProvidedOutputs);
    
    if (!pindexBest) {
        throw JSONRPCError(RPC_MISC_ERROR, "Blockchain is not in sync");
    }
    
    assert(pwalletMain != NULL);
   
    CTxDB txdb("r");
    CPegDB pegdb("r");
    
    // make list of 'rated' outputs, multimap with key 'distortion'
    // they are rated to be less distorted towards coins to withdraw
    
    map<uint320,CCoinToUse> mapAllOutputs = mapProvidedOutputs;
    set<uint320> setWalletOutputs;
        
    // get available coins
    getAvailableCoins(setConsumedInputs, nCycleNow,
                      setAllOutputs, setWalletOutputs, mapAllOutputs);
    // clean-up consumed, intersect with (wallet+provided)
    cleanupConsumed(setConsumedInputs, setAllOutputs, sConsumedInputs);
    // clean-up provided, remove what is already in wallet
    cleanupProvided(setWalletOutputs, mapProvidedOutputs);
    
    // do not consider outputs less than 1M baytoshi
    map<uint320,CCoinToUse> mapAllOutputsFiltered;
    for(const pair<uint320,CCoinToUse>& item : mapAllOutputs) {
        if (item.second.nValue < 1000000) continue;
        mapAllOutputsFiltered[item.first] = item.second;
    }
    mapAllOutputs = mapAllOutputsFiltered;
    
    if (mapAllOutputs.size() < 5) { /*4 first lest distored +1 to join*/
        Object result;
        result.push_back(Pair("completed", true));
        result.push_back(Pair("processed", false));
        result.push_back(Pair("status", "Nothing todo"));
    
        result.push_back(Pair("consumed_inputs", sConsumedInputs));
        result.push_back(Pair("provided_outputs", sProvidedOutputs));
    
        printpegshift(pdPegShift.fractions, peglevel_net, result);
        
        return result;
    }
    
    // read available coin fractions to sum up
    CFractions frAllLiquid(0, CFractions::STD);
    map<uint320, CFractions> mapAvailableCoins;
    for(const pair<uint320,CCoinToUse>& item : mapAllOutputs) {
        uint320 fkey = item.first;
        CFractions frOut(0, CFractions::VALUE);
        if (!pegdb.ReadFractions(fkey, frOut, true)) {
            continue;
        }
        
        mapAvailableCoins[fkey] = frOut;
        frAllLiquid += frOut.HighPart(peglevel_net.nSupply, nullptr);
    }
    
    // rate available coin fractions
    map<uint320,int64_t> mapAvailableLiquid;
    multimap<double,CCoinToUse> ratedOutputs;
    for(const pair<uint320,CCoinToUse>& item : mapAllOutputs) {
        uint320 fkey = item.first;
        if (!mapAvailableCoins.count(fkey)) {
            continue;
        }
        
        int64_t nAvailableLiquid = 0;
        CFractions frOut = mapAvailableCoins[fkey];
        frOut = frOut.HighPart(peglevel_net.nSupply, &nAvailableLiquid);
        
        /*less distorted are first*/
        double distortion = frOut.Distortion(frAllLiquid); 
        ratedOutputs.insert(pair<double,CCoinToUse>(distortion, item.second));
        mapAvailableLiquid[fkey] = nAvailableLiquid;
    }

    // use first 50 most distorted coins
    // get available value for selected coins
    int nCountInputs = std::min(50, int(ratedOutputs.size()-4));
    set<CCoinToUse> setCoins;
    
    int64_t nLiquidToJoin = 0;
    auto rit = ratedOutputs.rbegin();
    while (rit != ratedOutputs.rend()) {
        CCoinToUse out = (*rit).second;
        auto txhash = out.txhash;
        auto fkey = uint320(txhash, out.i);
        
        out.nAvailableValue = mapAvailableLiquid[fkey];
        nLiquidToJoin += out.nAvailableValue;
        
        setCoins.insert(out);
        nCountInputs--;
        
        if (!nCountInputs) {
            break;
        }
        rit++;
    }
    
    // first 4 less distorted to join with
    set<CCoinToUse> setBaseCoins;
    int nCountOutputs = 4;
    auto it = ratedOutputs.begin();
    for (; it != ratedOutputs.end(); ++it) {
        CCoinToUse out = (*it).second;
        auto txhash = out.txhash;
        auto fkey = uint320(txhash, out.i);
        
        out.nAvailableValue = mapAvailableLiquid[fkey];
        setBaseCoins.insert(out);
        nCountInputs--;
        
        if (!nCountOutputs) {
            break;
        }
    }
    
    CTransaction rawTx;
    
    size_t nNumInputs = setCoins.size();
    if (!nNumInputs) return false;

    // Base inputs to be sorted by address
    vector<CCoinToUse> vBaseCoins;
    for(const CCoinToUse& baseCoin : setBaseCoins) {
        vBaseCoins.push_back(baseCoin);
    }
    sort(vBaseCoins.begin(), vBaseCoins.end(), sortByAddress);
    // Inputs to be sorted by address
    vector<CCoinToUse> vCoins;
    for(const CCoinToUse& coin : setCoins) {
        vCoins.push_back(coin);
    }
    sort(vCoins.begin(), vCoins.end(), sortByAddress);
    
    // Collect input addresses
    // Prepare maps for input,available,take
    set<CTxDestination> setInputAddresses;
    vector<CTxDestination> vInputAddresses;
    map<CTxDestination, int64_t> mapAvailableValuesAt;
    map<CTxDestination, int64_t> mapInputValuesAt;
    map<CTxDestination, int64_t> mapTakeValuesAt;
    for(const CCoinToUse& coin : vCoins) {
        CTxDestination address;
        if(!ExtractDestination(coin.scriptPubKey, address))
            continue;
        setInputAddresses.insert(address); // sorted due to vCoins
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
    for(const CCoinToUse& baseCoin : vBaseCoins) {
        rawTx.vout.push_back(CTxOut(baseCoin.nValue
                                    +nLiquidToJoin/vBaseCoins.size(), 
                                    baseCoin.scriptPubKey));
    }
    
    CReserveKey reservekey(pwalletMain);
    reservekey.ReturnKey();
    
    // Available values - liquidity
    // Compute values to take from each address (liquidity is common)
    int64_t nValueLeft = nLiquidToJoin;
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
    
    // Calculate change (minus fee and part taken from change)
    map<CTxDestination, int> mapChangeOutputs;
    for (const CTxDestination& address : vInputAddresses) {
        CScript scriptPubKey;
        scriptPubKey.SetDestination(address);
        int64_t& nValueTakeAt = mapTakeValuesAt[address];
        int64_t nValueInput = mapInputValuesAt[address];
        int64_t nValueChange = nValueInput - nValueTakeAt;
        if (nValueChange == 0) continue;
        nValueTakeAt += nValueChange;
        mapChangeOutputs[address] = rawTx.vout.size();
        rawTx.vout.push_back(CTxOut(nValueChange, scriptPubKey));
    }

    // notation for exchange control
    string sTxid;
    {
        string sNotary = "XCH:M:";
        CDataStream ss(SER_GETHASH, 0);
        size_t n_inp = rawTx.vin.size();
        for(size_t j=0; j< n_inp; j++) {
            ss << rawTx.vin[j].prevout.hash;
            ss << rawTx.vin[j].prevout.n;
        }
        ss << string("M");
        sTxid = Hash(ss.begin(), ss.end()).GetHex();
        sNotary += sTxid;
        CScript scriptPubKey;
        scriptPubKey.push_back(OP_RETURN);
        unsigned char len_bytes = sNotary.size();
        scriptPubKey.push_back(len_bytes);
        for (size_t j=0; j< sNotary.size(); j++) {
            scriptPubKey.push_back(sNotary[j]);
        }
        rawTx.vout.push_back(CTxOut(1, scriptPubKey));
    }
    
    // Fill vin
    for(const CCoinToUse& coin : vBaseCoins) {
        rawTx.vin.push_back(CTxIn(coin.txhash,coin.i));
    }
    for(const CCoinToUse& coin : vCoins) {
        rawTx.vin.push_back(CTxIn(coin.txhash,coin.i));
    }
    
    // Fee to be simple taken from first output
    int64_t nFeeNetwork = 10000 * (rawTx.vin.size() + rawTx.vout.size());
    rawTx.vout[0].nValue -= nFeeNetwork;
    
    // Calculate peg to know fee fractions
    CFractions feesFractions(0, CFractions::STD);
    map<int, CFractions> mapTxOutputFractionsSkip;
    computeTxPegForNextCycle(rawTx, peglevel_net, txdb, pegdb, mapAllOutputs,
                             mapTxOutputFractionsSkip, feesFractions);
    
    // signing the transaction to get it ready for broadcast
//    int nIn = 0;
//    for(const CCoinToUse& coin : vCoins) {
//        if (!SignSignature(*pwalletMain, coin.scriptPubKey, rawTx, nIn++)) {
//            throw JSONRPCError(RPC_MISC_ERROR, 
//                               strprintf("Fail on signing input (%d)", nIn-1));
//        }
//    }
        
//    // first out after F notations (same num as size inputs)
//    CFractions frProcessed = mapTxOutputFractions[rawTx.vin.size()] + feesFractionsCommon;
//    CFractions frRequested = frAmount;

//    if (frRequested.Total() != nAmountWithFee) {
//        throw JSONRPCError(RPC_MISC_ERROR, 
//                           strprintf("Mismatch requested and amount_with_fee (%d - %d)",
//                                     frRequested.Total(), nAmountWithFee));
//    }
//    if (frProcessed.Total() != nAmountWithFee) {
//        throw JSONRPCError(RPC_MISC_ERROR, 
//                           strprintf("Mismatch processed and amount_with_fee (%d - %d)",
//                                     frProcessed.Total(), nAmountWithFee));
//    }
    
    // save fractions
    string sTxhash = rawTx.GetHash().GetHex();

    // get list of changes and add to current provided outputs
//    map<string, int64_t> mapTxChanges;
//    {
//        CPegDB pegdbrw;
//        for (size_t i=rawTx.vin.size()+1; i< rawTx.vout.size(); i++) { // skip notations and withdraw
//            // make map of change outputs
//            string txout = rawTx.GetHash().GetHex()+":"+itostr(i);
//            mapTxChanges[txout] = rawTx.vout[i].nValue;
//            // save these outputs in pegdb, so they can be used in next withdraws
//            auto fkey = uint320(rawTx.GetHash(), i);
//            pegdbrw.WriteFractions(fkey, mapTxOutputFractions[i]);
//        }
//    }
    // get list of consumed and provided outputs
//    nMaintenance += nFeeMaintenance;
//    prepareConsumedProvided(mapProvidedOutputs, rawTx, sAddress, nCycleNow, 
//                            nMaintenance, frMaintenance, peglevel_exchange,
//                            sConsumedInputs, sProvidedOutputs);
    
//    frBalance -= frRequested;
//    frExchange -= frRequested;
//    frPegShift += (frRequested - frProcessed);
    
    // consume reserve part of pegshift by balance
    // as computation were completed by pegnext it may use fractions
    // of current liquid - at current supply not to consume these fractions
//    consumereservepegshift(frBalance, frExchange, frPegShift, peglevel_exchange);
    
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << rawTx;
    
    string txstr = HexStr(ss.begin(), ss.end());
    
    Object result;
    result.push_back(Pair("completed", true));
    result.push_back(Pair("processed", true));
    result.push_back(Pair("txhash", sTxhash));
    result.push_back(Pair("rawtx", txstr));
    result.push_back(Pair("fee", feesFractions.Total()));

    result.push_back(Pair("consumed_inputs", sConsumedInputs));
    result.push_back(Pair("provided_outputs", sProvidedOutputs));

    printpegshift(pdPegShift.fractions, peglevel_net, result);
    
    return result;
}
