// Copyright (c) 2018 yshurik
//
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The use in another cyptocurrency project the code is licensed under
// Jelurida Public License (JPL). See https://www.jelurida.com/resources/jpl

#ifndef BITBAY_PEGSTD_H
#define BITBAY_PEGSTD_H

#include <list>
#include <functional>

#include "bignum.h"
#include "tinyformat.h"
#include "pegdata.h"

class CTxDB;
class CPegDB;
class CTxOut;
class CTransaction;
class CPegLevel;
class CFractions;
typedef std::map<uint320, CTxOut> MapPrevOut;

// functors for messagings
typedef std::function<void(const std::string &)> LoadMsg;

enum PegTxType {
    PEG_MAKETX_SEND_RESERVE     = 0,
    PEG_MAKETX_SEND_LIQUIDITY   = 1,
    PEG_MAKETX_FREEZE_RESERVE   = 2,
    PEG_MAKETX_FREEZE_LIQUIDITY = 3,
//    PEG_MAKETX_SEND_TOCOLD      = 4,
//    PEG_MAKETX_SEND_FROMCOLD    = 5
};

enum PegVoteType {
    PEG_VOTE_NONE       = 0,
    PEG_VOTE_AUTO       = 1,
    PEG_VOTE_INFLATE    = 2,
    PEG_VOTE_DEFLATE    = 3,
    PEG_VOTE_NOCHANGE   = 4
};

enum PegRewardType {
    PEG_REWARD_5    = 0,
    PEG_REWARD_10   = 1,
    PEG_REWARD_20   = 2,
    PEG_REWARD_40   = 3,
    PEG_REWARD_LAST
};

struct FrozenTxOut {
    int64_t nValue                      = 0;
    bool fIsColdOutput                  = false;
    long nFairWithdrawFromEscrowIndex1  = -1;
    long nFairWithdrawFromEscrowIndex2  = -1;
    std::string sAddress;
    CFractions fractions;
};

bool CalculateStandardFractions(const CTransaction & tx,
                                int nSupply,
                                unsigned int nTime,
                                MapPrevOut & inputs,
                                MapFractions& finputs,
                                MapFractions& mapTestFractionsPool,
                                CFractions& feesFractions,
                                std::string& sPegFailCause);

#endif
