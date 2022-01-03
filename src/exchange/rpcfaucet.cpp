// Copyright (c) 2019 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//#include <boost/assign/list_of.hpp>

#include "base58.h"
#include "rpcserver.h"
#include "init.h"
#include "main.h"
#include "wallet.h"

#include "pegdata.h"

using namespace std;
using namespace json_spirit;

Value faucet(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "faucet <address>\n"
            );
    
    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid BitBay address");

    Object result;
    bool completed_liquid = true;
    bool completed_reserve = true;
    string status = "Unknown";
    
    int64_t amount = 100000000000;
    CFractions fr(amount, CFractions::VALUE);
    fr = fr.Std();

    int nSupply = pindexBest ? pindexBest->nPegSupplyIndex : 0;
    int64_t liquid = fr.High(nSupply);
    int64_t reserve = fr.Low(nSupply);
    
    if (liquid >0) {
        PegTxType txType = PEG_MAKETX_SEND_LIQUIDITY;
        
        vector<pair<CScript, int64_t> > vecSend;
        CScript scriptPubKey;
        scriptPubKey.SetDestination(address.Get());
        vecSend.push_back(make_pair(scriptPubKey, liquid));
        
        CWalletTx wtx;
        CReserveKey keyChange(pwalletMain);
        int64_t nFeeRequired = 0;
        string sFailCause;
        bool fCreated = pwalletMain->CreateTransaction(txType, vecSend, wtx, keyChange, nFeeRequired, nullptr, false /*fTest*/, sFailCause);
        if (fCreated) {
            
            bool fCommitted = pwalletMain->CommitTransaction(wtx, keyChange);
            if (fCommitted) {
                completed_liquid = true;
            } else {
                completed_liquid = false;
                status = "Failed to commit a liquid transaction";
            }
            
        } else {
            completed_liquid = false;
            status = "Failed to create a liquid transaction";
        }
    }
    
    if (reserve >0) {
        PegTxType txType = PEG_MAKETX_SEND_RESERVE;
        
        vector<pair<CScript, int64_t> > vecSend;
        CScript scriptPubKey;
        scriptPubKey.SetDestination(address.Get());
        vecSend.push_back(make_pair(scriptPubKey, reserve));
        
        CWalletTx wtx;
        CReserveKey keyChange(pwalletMain);
        int64_t nFeeRequired = 0;
        string sFailCause;
        bool fCreated = pwalletMain->CreateTransaction(txType, vecSend, wtx, keyChange, nFeeRequired, nullptr, false /*fTest*/, sFailCause);
        if (fCreated) {
            
            bool fCommitted = pwalletMain->CommitTransaction(wtx, keyChange);
            if (fCommitted) {
                completed_reserve = true;
            } else {
                completed_reserve = false;
                status = "Failed to commit a reserve transaction";
            }
            
        } else {
            completed_reserve = false;
            status = "Failed to create a reserve transaction";
        }
    }
    result.push_back(Pair("completed", completed_reserve && completed_liquid));
    result.push_back(Pair("completed_liquid", completed_liquid));
    result.push_back(Pair("completed_reserve", completed_reserve));
    result.push_back(Pair("status", status));
    
    return result;
}

