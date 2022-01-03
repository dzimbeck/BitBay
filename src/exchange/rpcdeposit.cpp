// Copyright (c) 2019 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "rpcserver.h"
#include "txdb.h"
#include "main.h"
#include "init.h"
#include "wallet.h"
#include "pegdata.h"

#include <boost/assign/list_of.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;

void printpeglevel(const CPegLevel & peglevel,
                   Object & result);

void printpegtxout(const CPegData & pegdata,
                   Object & result,
                   string prefix);

void printpegbalance(const CPegData & pegdata,
                     Object & result,
                     string prefix);

// API calls

Value listdeposits(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
            "listdeposits [minconf=1] [maxconf=9999999] [\"address\",...]\n"
            "Returns array of unspent transaction outputs\n"
            "with between minconf and maxconf (inclusive) confirmations.\n"
            "Optionally filtered to only include txouts paid to specified addresses.\n"
            "Results are an array of Objects, each of which has:\n"
            "{txid, vout, scriptPubKey, amount, confirmations}");
    
    RPCTypeCheck(params, list_of(int_type)(int_type)(array_type));

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    int nMaxDepth = 9999999;
    if (params.size() > 1)
        nMaxDepth = params[1].get_int();

    set<CBitcoinAddress> setAddress;
    if (params.size() > 2)
    {
        Array inputs = params[2].get_array();
        for(Value& input : inputs)
        {
            CBitcoinAddress address(input.get_str());
            if (!address.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid BitBay address: ")+input.get_str());
            if (setAddress.count(address))
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+input.get_str());
           setAddress.insert(address);
        }
    }

    Array results;
    vector<COutput> vecOutputs;
    assert(pwalletMain != NULL);
    unsigned int nLastBlockTime = pindexBest->nTime;
    pwalletMain->AvailableCoins(vecOutputs, false, true, NULL);
    pwalletMain->FrozenCoins(vecOutputs, false, false, NULL);
    for(const COutput& out : vecOutputs)
    {
        if (out.nDepth < nMinDepth || out.nDepth > nMaxDepth)
            continue;
        if (out.tx->IsCoinBase() || out.tx->IsCoinStake())
            continue;

        uint256 wid;
        int nExchangeOut = -1;
        bool fExchangeTx = out.tx->IsExchangeTx(nExchangeOut, wid);
        if (fExchangeTx && nExchangeOut <0) {
            continue; // exchange tx, internal tx
        }
        if (fExchangeTx && nExchangeOut != out.i) {
            continue; // exchange tx, one of change
        }

        if(setAddress.size())
        {
            CTxDestination address;
            if(!ExtractDestination(out.tx->vout[out.i].scriptPubKey, address))
                continue;

            if (!setAddress.count(address))
                continue;
        }

        int64_t nValue = out.tx->vout[out.i].nValue;
        const CScript& pk = out.tx->vout[out.i].scriptPubKey;
        Object entry;
        entry.push_back(Pair("txid", out.tx->GetHash().GetHex()));
        entry.push_back(Pair("vout", out.i));
        CTxDestination address;
        if (ExtractDestination(out.tx->vout[out.i].scriptPubKey, address))
        {
            entry.push_back(Pair("address", CBitcoinAddress(address).ToString()));
            if (pwalletMain->mapAddressBook.count(address))
                entry.push_back(Pair("account", pwalletMain->mapAddressBook[address]));
        }
        entry.push_back(Pair("scriptPubKey", HexStr(pk.begin(), pk.end())));
        if (pk.IsPayToScriptHash())
        {
            CTxDestination address;
            if (ExtractDestination(pk, address))
            {
                const CScriptID& hash = boost::get<CScriptID>(address);
                CScript redeemScript;
                if (pwalletMain->GetCScript(hash, redeemScript))
                    entry.push_back(Pair("redeemScript", HexStr(redeemScript.begin(), redeemScript.end())));
            }
        }
        entry.push_back(Pair("amount", ValueFromAmount(nValue)));
        if (pindexBest && out.tx->vOutFractions.size() > size_t(out.i)) {
            int nSupply = pindexBest->nPegSupplyIndex;
            const CFractions & fractions = out.tx->vOutFractions[out.i].Ref();
            if (fractions.Total() == nValue) {
                entry.push_back(Pair("reserve", ValueFromAmount(fractions.Low(nSupply))));
                entry.push_back(Pair("liquidity", ValueFromAmount(fractions.High(nSupply))));
            }
        }
        bool fFrozen = out.IsFrozen(nLastBlockTime);
        entry.push_back(Pair("confirmations", out.nDepth));
        entry.push_back(Pair("spendable", out.fSpendable & !fFrozen));
        entry.push_back(Pair("frozen", fFrozen));
        if (fFrozen) {
            entry.push_back(Pair("unlocktime", out.FrozenUnlockTime()));
            entry.push_back(Pair("lastblocktime", uint64_t(nLastBlockTime)));
        }
        results.push_back(entry);
    }

    return results;
}

Value registerdeposit(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 4)
        throw runtime_error(
            "registerdeposit "
                "<txid:nout> "
                "<balance_pegdata_base64> "
                "<exchange_pegdata_base64> "
                "<peglevel_hex>\n"
            );
    
    string sTxout = params[0].get_str();
    vector<string> vTxoutArgs;
    boost::split(vTxoutArgs, sTxout, boost::is_any_of(":"));
    
    if (vTxoutArgs.size() != 2) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, 
                           "Txout is not recognized, format txid:nout");
    }
    
    string sTxid = vTxoutArgs[0];
    char * pEnd = nullptr;
    long nout = strtol(vTxoutArgs[1].c_str(), &pEnd, 0);
    if (pEnd == vTxoutArgs[1].c_str()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, 
                           "Txout is not recognized, format txid:nout, nout is not number");
    }

    string balance_pegdata64 = params[1].get_str();
    string exchange_pegdata64 = params[2].get_str();
    string peglevel_hex = params[3].get_str();

    CPegLevel peglevel(peglevel_hex);
    if (!peglevel.IsValid()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Can not unpack peglevel");
    }

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
    
    if (!balance_pegdata64.empty() && pdBalance.peglevel.nCycle != peglevel.nCycle) {
        throw JSONRPCError(RPC_MISC_ERROR, "Balance has other cycle than peglevel");
    }

    uint256 txhash;
    txhash.SetHex(sTxid);
    
    Object result;
    
    CTxDB txdb("r");
    CTxIndex txindex;
    if (!txdb.ReadTxIndex(txhash, txindex)) {
        throw JSONRPCError(RPC_MISC_ERROR, "ReadTxIndex tx failed");
    }
    CTransaction tx;
    if (!tx.ReadFromDisk(txindex.pos)) {
        throw JSONRPCError(RPC_MISC_ERROR, "ReadFromDisk tx failed");
    }
    if (nout <0 || size_t(nout) >= tx.vout.size()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Nout is out of index");
    }
    
    uint256 wid;
    int nExchangeOut = -1;
    bool fExchangeTx = tx.IsExchangeTx(nExchangeOut, wid);
    if (fExchangeTx && nExchangeOut <0) {
        throw JSONRPCError(RPC_MISC_ERROR, "Can not register output from exchange internal tx");
    }
    if (fExchangeTx && nExchangeOut != nout) {
        throw JSONRPCError(RPC_MISC_ERROR, "Can not register change output from exchange tx");
    }
    
    int nPegInterval = Params().PegInterval(nBestHeight);
    int nDepth = txindex.GetDepthInMainChain();
    int nHeight = nBestHeight - nDepth +1;
    int nNextHeight = (nHeight / nPegInterval +1) * nPegInterval;
    int nRegisterHeight = std::max(nNextHeight, nHeight + nRecommendedConfirmations);

    if (nDepth < nRecommendedConfirmations) {
        result.push_back(Pair("deposited", false));
        result.push_back(Pair("atblock", nRegisterHeight));
        result.push_back(Pair("status", strprintf("Need at least %d confirmations", nRecommendedConfirmations)));
        return result;
    }

    if (nRegisterHeight > nBestHeight) {
        result.push_back(Pair("deposited", false));
        result.push_back(Pair("atblock", nRegisterHeight));
        result.push_back(Pair("status", strprintf("Need to wait for registration at %d block", nRegisterHeight)));
        return result;
    }
    
    CPegDB pegdb("r");
    auto fkey = uint320(txhash, nout);
    CFractions frDeposit(tx.vout[nout].nValue, CFractions::VALUE);
    if (!pegdb.ReadFractions(fkey, frDeposit, false)) {
        result.push_back(Pair("deposited", false));
        result.push_back(Pair("atblock", nRegisterHeight));
        result.push_back(Pair("status", "No peg data read failed"));
        return result;
    }
    
    unsigned int nLastBlockTime = pindexBest->nTime;
    bool fF = frDeposit.nFlags & CFractions::NOTARY_F;
    bool fV = frDeposit.nFlags & CFractions::NOTARY_V;
    bool fC = frDeposit.nFlags & CFractions::NOTARY_C;
    fF &= frDeposit.nLockTime >= nLastBlockTime;
    fV &= frDeposit.nLockTime >= nLastBlockTime;
    if (fF || fV) {
        result.push_back(Pair("deposited", false));
        result.push_back(Pair("frozen", true));
        result.push_back(Pair("until", frDeposit.nLockTime));
        result.push_back(Pair("status", "Need to wait to unfreeze"));
        return result;
    }
    if (fC) {
        result.push_back(Pair("deposited", false));
        result.push_back(Pair("frozen", true));
        result.push_back(Pair("status", "Can not register cold output"));
        return result;
    }
    
    frDeposit = frDeposit.Std();
    CPegData pdTxout;
    pdTxout.fractions.sReturnAddr = HexStr(
        tx.vout[nout].scriptPubKey.begin(), 
        tx.vout[nout].scriptPubKey.end()
    );
    pdTxout.fractions += frDeposit;
    pdBalance.fractions += frDeposit;
    pdExchange.fractions += frDeposit;

    pdTxout.peglevel = peglevel;
    pdTxout.nLiquid = pdTxout.fractions.High(peglevel);
    pdTxout.nReserve = pdTxout.fractions.Low(peglevel);
    
    pdBalance.peglevel = peglevel;
    pdBalance.nLiquid = pdBalance.fractions.High(peglevel);
    pdBalance.nReserve = pdBalance.fractions.Low(peglevel);

    pdExchange.peglevel = peglevel;
    pdExchange.nLiquid = pdExchange.fractions.High(peglevel);
    pdExchange.nReserve = pdExchange.fractions.Low(peglevel);
    
    result.push_back(Pair("deposited", true));
    result.push_back(Pair("status", "Registered"));
    result.push_back(Pair("atblock", nRegisterHeight));
    
    result.push_back(Pair("cycle", peglevel.nCycle));
    
    printpeglevel(peglevel, result);
    printpegtxout(pdTxout, result, "txout_");
    printpegbalance(pdBalance, result, "balance_");
    printpegbalance(pdExchange, result, "exchange_");
    
    return result;
}


Value updatetxout(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "updatetxout "
                "<txid:nout> "
                "<peglevel_hex>\n"
            );
    
    string sTxout = params[0].get_str();
    vector<string> vTxoutArgs;
    boost::split(vTxoutArgs, sTxout, boost::is_any_of(":"));
    
    if (vTxoutArgs.size() != 2) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, 
                           "Txout is not recognized, format txid:nout");
    }
    
    string sTxid = vTxoutArgs[0];
    char * pEnd = nullptr;
    long nout = strtol(vTxoutArgs[1].c_str(), &pEnd, 0);
    if (pEnd == vTxoutArgs[1].c_str()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, 
                           "Txout is not recognized, format txid:nout, nout is not number");
    }

    string peglevel_hex = params[1].get_str();

    CPegLevel peglevel(peglevel_hex);
    if (!peglevel.IsValid()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Can not unpack peglevel");
    }

    uint256 txhash;
    txhash.SetHex(sTxid);
    
    Object result;
    
    CTxDB txdb("r");
    CTxIndex txindex;
    if (!txdb.ReadTxIndex(txhash, txindex)) {
        throw JSONRPCError(RPC_MISC_ERROR, "ReadTxIndex tx failed");
    }
    CTransaction tx;
    if (!tx.ReadFromDisk(txindex.pos)) {
        throw JSONRPCError(RPC_MISC_ERROR, "ReadFromDisk tx failed");
    }
    if (nout <0 || size_t(nout) >= tx.vout.size()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Nout is out of index");
    }

    CPegDB pegdb("r");
    auto fkey = uint320(txhash, nout);
    CFractions frDeposit(tx.vout[nout].nValue, CFractions::VALUE);
    if (!pegdb.ReadFractions(fkey, frDeposit, false)) {
        result.push_back(Pair("completed", false));
        result.push_back(Pair("status", "No peg data read failed"));
        return result;
    }
    
    frDeposit = frDeposit.Std();
    CPegData pdTxout;
    pdTxout.fractions.sReturnAddr = HexStr(
        tx.vout[nout].scriptPubKey.begin(), 
        tx.vout[nout].scriptPubKey.end()
    );
    pdTxout.fractions += frDeposit;

    pdTxout.peglevel = peglevel;
    pdTxout.nLiquid = pdTxout.fractions.High(peglevel);
    pdTxout.nReserve = pdTxout.fractions.Low(peglevel);
    
    result.push_back(Pair("completed", true));
    result.push_back(Pair("status", "Updated"));
    
    result.push_back(Pair("cycle", peglevel.nCycle));
    
    printpeglevel(peglevel, result);
    printpegtxout(pdTxout, result, "txout_");
    
    return result;
}
