// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "init.h"
#include "main.h"
#include "net.h"
#include "netbase.h"
#include "rpcserver.h"
#include "timedata.h"
#include "util.h"
#include "txdb-leveldb.h"
#include "pegdb-leveldb.h"
#ifdef ENABLE_WALLET
#include "wallet.h"
#include "walletdb.h"
#endif

#include <stdint.h>

#include <boost/assign/list_of.hpp>
#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;

Value getinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getinfo\n"
            "Returns an object containing various state info.");

    proxyType proxy;
    GetProxy(NET_IPV4, proxy);

    Object obj, diff;
    obj.push_back(Pair("version",       FormatFullVersion()));
    obj.push_back(Pair("protocolversion",(int)PROTOCOL_VERSION));
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        obj.push_back(Pair("walletversion", pwalletMain->GetVersion()));
        obj.push_back(Pair("balance",       ValueFromAmount(pwalletMain->GetBalance())));
        obj.push_back(Pair("liquidity",     ValueFromAmount(pwalletMain->GetLiquidity())));
        obj.push_back(Pair("reserve",       ValueFromAmount(pwalletMain->GetReserve())));
        obj.push_back(Pair("frozen",        ValueFromAmount(pwalletMain->GetFrozen())));
        obj.push_back(Pair("newmint",       ValueFromAmount(pwalletMain->GetNewMint())));
        obj.push_back(Pair("stake",         ValueFromAmount(pwalletMain->GetStake())));
    }
#endif
    obj.push_back(Pair("blocks",        (int)nBestHeight));
    obj.push_back(Pair("timeoffset",    (int64_t)GetTimeOffset()));
    obj.push_back(Pair("moneysupply",   ValueFromAmount(pindexBest->nMoneySupply)));
    obj.push_back(Pair("connections",   (int)vNodes.size()));
    obj.push_back(Pair("proxy",         (proxy.IsValid() ? proxy.ToStringIPPort() : string())));
    obj.push_back(Pair("ip",            GetLocalAddress(NULL).ToStringIP()));

    diff.push_back(Pair("proof-of-work",  GetDifficulty()));
    diff.push_back(Pair("proof-of-stake", GetDifficulty(GetLastBlockIndex(pindexBest, true))));
    obj.push_back(Pair("difficulty",    diff));

    obj.push_back(Pair("testnet",       TestNet()));
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        obj.push_back(Pair("keypoololdest", (int64_t)pwalletMain->GetOldestKeyPoolTime()));
        obj.push_back(Pair("keypoolsize",   (int)pwalletMain->GetKeyPoolSize()));
    }
    obj.push_back(Pair("paytxfee",      ValueFromAmount(nTransactionFee)));
    obj.push_back(Pair("mininput",      ValueFromAmount(nMinimumInputValue)));
    if (pwalletMain && pwalletMain->IsCrypted())
        obj.push_back(Pair("unlocked_until", (int64_t)nWalletUnlockTime));
#endif
    obj.push_back(Pair("errors",        GetWarnings("statusbar")));
    return obj;
}

Value getpeginfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getpeginfo\n"
            "Returns an object containing peg state info.");

    Object peg;
    int nPegInterval = Params().PegInterval(nBestHeight);
    int nCycle = nBestHeight / nPegInterval;
    peg.push_back(Pair("steps", PEG_SIZE));
    peg.push_back(Pair("cycle", nCycle));
    peg.push_back(Pair("interval", nPegInterval));
    peg.push_back(Pair("startingblock", nPegStartHeight));
    peg.push_back(Pair("pegfeeperinput", PEG_MAKETX_FEE_INP_OUT));
    peg.push_back(Pair("subpremiumrating", PEG_SUBPREMIUM_RATING));
    peg.push_back(Pair("peg", pindexBest->nPegSupplyIndex));
    peg.push_back(Pair("pegnext", pindexBest->GetNextIntervalPegSupplyIndex()));
    peg.push_back(Pair("pegnextnext", pindexBest->GetNextNextIntervalPegSupplyIndex()));
    return peg;
}

Value getfractions(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getfractions <txhash:output> [pegsupplyindex]\n"
            "Returns array containing rate info.");

    int supply = -1; 
    if (params.size() == 2) {
        supply = params[1].get_int();
        if (supply <0) {
            throw runtime_error(
                        "getfractions <txhash:output> [pegsupplyindex]\n"
                        "pegsupplyindex below zero");
        }
        if (supply > nPegMaxSupplyIndex) {
            throw runtime_error(
                        "getfractions <txhash:output> [pegsupplyindex]\n"
                        "pegsupplyindex above maximum possible");
        }
    }
    
    string txhashnout = params[0].get_str();
    vector<string> txhashnout_args;
    boost::split(txhashnout_args, txhashnout, boost::is_any_of(":"));
    
    if (txhashnout_args.size() != 2) {
        throw runtime_error(
            "getfractions <txhash:output> [pegsupplyindex]\n"
            "First parameter should refer transaction output txhash:output");
    }
    string txhash_str = txhashnout_args.front();
    int nout = std::stoi(txhashnout_args.back());
    
    uint256 txhash;
    txhash.SetHex(txhash_str);
    
    if (supply <0) {
        // current if tx is not on disk
        supply = pindexBest ? pindexBest->nPegSupplyIndex : 0;
        // read from block
        unsigned int nTxNum = 0;
        uint256 blockhash;
        {
            CTxDB txdb("r");
            CTxIndex txindex;
            if (txdb.ReadTxIndex(txhash, txindex)) {
                txindex.GetHeightInMainChain(&nTxNum, txhash, &blockhash);
                if (mapBlockIndex.count(blockhash)) {
                    supply = mapBlockIndex.ref(blockhash)->nPegSupplyIndex;
                }
            }
        }
    }
    
    Object obj;
    CPegDB pegdb("r");
    auto fkey = uint320(txhash, nout);
    CFractions fractions(0, CFractions::VALUE);
    if (!pegdb.ReadFractions(fkey, fractions, true)) {
        if (!mempool.lookup(txhash, nout, fractions)) {
            return obj;
        }
    }
    fractions = fractions.Std();
    
    Array f;
    int64_t total = 0;
    int64_t reserve = 0;
    int64_t liquidity = 0;
    for(int i=0; i<PEG_SIZE; i++) {
        total += fractions.f[i];
        if (i<supply) reserve += fractions.f[i];
        if (i>=supply) liquidity += fractions.f[i];
        f.push_back(fractions.f[i]);
    }
    
    int lock = 0;
    string flags;
    if (fractions.nFlags & CFractions::NOTARY_F) flags = "F";
    if (fractions.nFlags & CFractions::NOTARY_V) flags = "V";
    if (fractions.nFlags & CFractions::NOTARY_C) flags = "C";
    if (!flags.empty()) lock = fractions.nLockTime;
    
    obj.push_back(Pair("total", total));
    obj.push_back(Pair("reserve", reserve));
    obj.push_back(Pair("liquidity", liquidity));
    obj.push_back(Pair("peg", supply));
    obj.push_back(Pair("lock", lock));
    obj.push_back(Pair("flags", flags));
    obj.push_back(Pair("values", f));
    
    return obj;
}

Value getfractionsbase64(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getfractionsbase64 <txhash:output>\n"
            "Returns base64 string containing rate info.");

    string txhashnout = params[0].get_str();
    vector<string> txhashnout_args;
    boost::split(txhashnout_args, txhashnout, boost::is_any_of(":"));
    
    if (txhashnout_args.size() != 2) {
        throw runtime_error(
            "getfractionsbase64 <txhash:output>\n"
            "First parameter should refer transaction output txhash:output");
    }
    string txhash_str = txhashnout_args.front();
    int nout = std::stoi(txhashnout_args.back());
    
    uint256 txhash;
    txhash.SetHex(txhash_str);
    
    Object obj;
    CPegDB pegdb("r");
    auto fkey = uint320(txhash, nout);
    CFractions fractions(0, CFractions::VALUE);
    if (!pegdb.ReadFractions(fkey, fractions, true)) {
        if (!mempool.lookup(txhash, nout, fractions)) {
            return obj;
        }
    }
    
    CPegData pd;
    pd.peglevel = CPegLevel(1,0,0,0,0,0); // can get block of
    {
        CTxDB txdb("r");
        CTxIndex txindex;
        if (txdb.ReadTxIndex(txhash, txindex)) {
            CBlock block;
            if (block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false)) {
                map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(block.GetHash());
                if (mi != mapBlockIndex.end()) {
                    CBlockIndex* pindex = (*mi).second;
                    int nPegInterval = Params().PegInterval(pindex->nHeight);
                    pd.peglevel.nCycle = pindex->nHeight / nPegInterval;
                    pd.peglevel.nCyclePrev = pd.peglevel.nCycle -1;
                    pd.peglevel.nBuffer = 0;
                    pd.peglevel.nSupply = pindex->nPegSupplyIndex;
                    pd.peglevel.nSupplyNext = pindex->GetNextIntervalPegSupplyIndex();
                    pd.peglevel.nSupplyNextNext = pindex->GetNextNextIntervalPegSupplyIndex();
                }
            }
        }
    }
    pd.fractions = fractions.Std();
    pd.nLiquid = fractions.High(pd.peglevel);
    pd.nReserve = fractions.Low(pd.peglevel);
    return pd.ToString();
}

Value getliquidityrate(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "getliquidityrate <txhash:output> <pegsupplyindex>\n"
            "Returns array containing rate info.");

    string txhashnout = params[0].get_str();
    vector<string> txhashnout_args;
    boost::split(txhashnout_args, txhashnout, boost::is_any_of(":"));
    
    if (txhashnout_args.size() != 2) {
        throw runtime_error(
            "getliquidityrate <txhash:output> <pegsupplyindex>\n"
            "First parameter should refer transaction output txhash:output");
    }
    string txhash_str = txhashnout_args.front();
    int nout = std::stoi(txhashnout_args.back());
    int supply = std::stoi(params[1].get_str());
    
    uint256 txhash;
    txhash.SetHex(txhash_str);
    
    Object obj;
    CPegDB pegdb("r");
    auto fkey = uint320(txhash, nout);
    CFractions fractions(0, CFractions::VALUE);
    if (!pegdb.ReadFractions(fkey, fractions, true)) {
        if (!mempool.lookup(txhash, nout, fractions)) {
            return obj;
        }
    }
    fractions = fractions.Std();
    
    int64_t total = 0;
    int highkey = 0;
    for(int i=supply; i<PEG_SIZE; i++) {
        total += fractions.f[i];
        if (fractions.f[i] >0)
            highkey = i;
    }
    
    if (total == 0) {
        return obj;
    }
    
    double average = 0;
    int multiplier = 1;
    vector<double> periods;
    for(int i=supply; i<=highkey; i++) {
        double e = double(fractions.f[i])/double(total);
        average += e;
        while (true) {
            if (multiplier >20) break;
            if (average <= 0.1*multiplier) break;
            if (periods.size() != size_t((multiplier-1)*2)) break;
            
            periods.push_back(i-supply);
            periods.push_back(average);
            multiplier += 1;
        }
    }
    
    while (periods.size() <18) {
        periods.push_back(0);
    }
    
    double average_f = 0;
    for(int i=1; i<=18/2; i++) {
        average_f += periods[i*2-1]*periods[i*2-2];
    }
    obj.push_back(Pair("rate", int(std::floor(average_f))));
    
    Object rates;
    for(size_t i=0; i< periods.size()/2; i++) {
        rates.push_back(Pair(std::to_string(int(periods[i*2])), periods[i*2+1]));
    }
    obj.push_back(Pair("periods", rates));
    
    return obj;
}

#ifdef ENABLE_WALLET
class DescribeAddressVisitor : public boost::static_visitor<Object>
{
private:
    isminetype mine;

public:
    DescribeAddressVisitor(isminetype mineIn) : mine(mineIn) {}

    Object operator()(const CNoDestination &dest) const { return Object(); }

    Object operator()(const CKeyID &keyID) const {
        Object obj;
        CPubKey vchPubKey;
        obj.push_back(Pair("isscript", false));
        if (mine == MINE_SPENDABLE) {
            pwalletMain->GetPubKey(keyID, vchPubKey);
            obj.push_back(Pair("pubkey", HexStr(vchPubKey)));
            obj.push_back(Pair("iscompressed", vchPubKey.IsCompressed()));
        }
        return obj;
    }

    Object operator()(const CScriptID &scriptID) const {
        Object obj;
        obj.push_back(Pair("isscript", true));
        if (mine == MINE_SPENDABLE) {
            CScript subscript;
            pwalletMain->GetCScript(scriptID, subscript);
            std::vector<CTxDestination> addresses;
            txnouttype whichType;
            int nRequired;
            ExtractDestinations(subscript, whichType, addresses, nRequired);
            obj.push_back(Pair("script", GetTxnOutputType(whichType)));
            obj.push_back(Pair("hex", HexStr(subscript.begin(), subscript.end())));
            Array a;
            for(const CTxDestination& addr : addresses) {
                a.push_back(CBitcoinAddress(addr).ToString());
            }
            obj.push_back(Pair("addresses", a));
            if (whichType == TX_MULTISIG)
                obj.push_back(Pair("sigsrequired", nRequired));
        }
        return obj;
    }
};
#endif

Value validateaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "validateaddress <bitbayaddress>\n"
            "Return information about <bitbayaddress>.");

    CBitcoinAddress address(params[0].get_str());
    bool isValid = address.IsValid();

    Object ret;
    ret.push_back(Pair("isvalid", isValid));
    if (isValid)
    {
        CTxDestination dest = address.Get();
        string currentAddress = address.ToString();
        ret.push_back(Pair("address", currentAddress));
#ifdef ENABLE_WALLET
        isminetype mine = pwalletMain ? IsMine(*pwalletMain, dest) : MINE_NO;
        ret.push_back(Pair("ismine", (mine & MINE_SPENDABLE) ? true : false));
        if (mine != MINE_NO) {
            ret.push_back(Pair("watchonly", (mine & MINE_WATCH_ONLY) ? true: false));
            Object detail = boost::apply_visitor(DescribeAddressVisitor(mine), dest);
            ret.insert(ret.end(), detail.begin(), detail.end());
        }
        if (pwalletMain && pwalletMain->mapAddressBook.count(dest))
            ret.push_back(Pair("account", pwalletMain->mapAddressBook[dest]));
#endif
    }
    return ret;
}

Value validatepubkey(const Array& params, bool fHelp)
{
    if (fHelp || !params.size() || params.size() > 2)
        throw runtime_error(
            "validatepubkey <bitbaypubkey>\n"
            "Return information about <bitbaypubkey>.");

    std::vector<unsigned char> vchPubKey = ParseHex(params[0].get_str());
    CPubKey pubKey(vchPubKey);

    bool isValid = pubKey.IsValid();
    bool isCompressed = pubKey.IsCompressed();
    CKeyID keyID = pubKey.GetID();

    CBitcoinAddress address;
    address.Set(keyID);

    Object ret;
    ret.push_back(Pair("isvalid", isValid));
    if (isValid)
    {
        CTxDestination dest = address.Get();
        string currentAddress = address.ToString();
        ret.push_back(Pair("address", currentAddress));
        ret.push_back(Pair("iscompressed", isCompressed));
#ifdef ENABLE_WALLET
        isminetype mine = pwalletMain ? IsMine(*pwalletMain, dest) : MINE_NO;
        ret.push_back(Pair("ismine", (mine & MINE_SPENDABLE) ? true : false));
        if (mine != MINE_NO) {
            ret.push_back(Pair("watchonly", (mine & MINE_WATCH_ONLY) ? true: false));
            Object detail = boost::apply_visitor(DescribeAddressVisitor(mine), dest);
            ret.insert(ret.end(), detail.begin(), detail.end());
        }
        if (pwalletMain && pwalletMain->mapAddressBook.count(dest))
            ret.push_back(Pair("account", pwalletMain->mapAddressBook[dest]));
#endif
    }
    return ret;
}

Value verifymessage(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "verifymessage <bitbayaddress> <signature> <message>\n"
            "Verify a signed message");

    string strAddress  = params[0].get_str();
    string strSign     = params[1].get_str();
    string strMessage  = params[2].get_str();

    CBitcoinAddress addr(strAddress);
    if (!addr.IsValid())
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");

    CKeyID keyID;
    if (!addr.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");

    bool fInvalid = false;
    vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;

    return (pubkey.GetID() == keyID);
}

Value validaterawtransaction(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "validaterawtransaction <hex string> <pegsupplyindex>\n"
            "Validate the transaction (serialized, hex-encoded).\n"
            "Second argument is peg supply level for the validation of the transaction with peg system.\n"
            "Returns json object with keys:\n"
            "  complete : 1 if transaction bypassed all peg checks\n"
            );

    vector<unsigned char> txData(ParseHex(params[0].get_str()));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    vector<CTransaction> txVariants;
    while (!ssData.empty())
    {
        try {
            CTransaction tx;
            ssData >> tx;
            txVariants.push_back(tx);
        }
        catch (std::exception &e) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        }
    }

    if (txVariants.empty())
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transaction");

    int nSupply = params[1].get_int();
    if (nSupply <0) {
        throw runtime_error(
            "validaterawtransaction <hex string> <pegsupplyindex>\n"
            "pegsupplyindex below zero");
    }
    if (nSupply > nPegMaxSupplyIndex) {
        throw runtime_error(
            "validaterawtransaction <hex string> <pegsupplyindex>\n"
            "pegsupplyindex above maximum possible");
    }

    // tx will end up with all the checks; it
    // starts as a clone of the rawtx:
    CTransaction tx(txVariants[0]);

    // Fetch previous transactions (inputs):
    MapPrevTx mapInputs;
    MapFractions mapInputsFractions;
    CTxDB txdb("r");
    CPegDB pegdb("r");
    map<uint256, CTxIndex> mapUnused;
    MapFractions mapOutputsFractions;
    CFractions feesFractions;
    bool fInvalid;

    if (!tx.FetchInputs(txdb, pegdb,
                        mapUnused, mapOutputsFractions,
                        false, false,
                        mapInputs, mapInputsFractions, fInvalid)) {
        Object result;
        result.push_back(Pair("complete", false));
        result.push_back(Pair("cause", "Can not fetch transaction inputs"));
        return result;
    }

    string sPegFailCause;
    bool peg_ok = CalculateStandardFractions(tx,
                                             nSupply,
                                             tx.nTime,
                                             mapInputs, mapInputsFractions,
                                             mapOutputsFractions,
                                             feesFractions,
                                             sPegFailCause);
    if (!peg_ok) {
        Object result;
        result.push_back(Pair("complete", false));
        result.push_back(Pair("cause", sPegFailCause.c_str()));
        return result;
    }

    Object result;
    result.push_back(Pair("complete", true));
    return result;
}
