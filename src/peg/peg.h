// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITBAY_PEG_H
#define BITBAY_PEG_H

#include <list>
#include <functional>

#include "bignum.h"
#include "tinyformat.h"
#include "pegdata.h"
#include "pegstd.h"

class CTxDB;
class CPegDB;
class CBlock;
class CBlockIndex;
class CTxOut;
class CTxIndex;
class CTransaction;
class CPegLevel;
class CFractions;
typedef std::map<uint256, std::pair<CTxIndex, CTransaction> > MapPrevTx;
typedef std::map<uint320, CTxOut> MapPrevOut;

extern int nPegStartHeight;
extern int nPegMaxSupplyIndex;
extern bool fPegIsActivatedViaTx;

// functors for messagings
typedef std::function<void(const std::string &)> LoadMsg;

bool SetBlocksIndexesReadyForPeg(CTxDB & ctxdb,
                                 LoadMsg load_msg);
bool CalculateBlockPegIndex(CBlockIndex* pindex);
bool CalculateBlockPegVotes(const CBlock & cblock,
                            CBlockIndex* pindex,
                            CPegDB& pegdb);
int CalculatePegVotes(const CFractions & fractions, 
                      int nPegSupplyIndex);

bool CalculateStandardFractions(const CTransaction & tx,
                                int nSupply,
                                unsigned int nTime,
                                MapPrevTx & inputs,
                                MapFractions& finputs,
                                MapFractions& mapTestFractionsPool,
                                CFractions& feesFractions,
                                std::string& sPegFailCause);

bool CalculateStakingFractions(const CTransaction & tx,
                               const CBlockIndex* pindexBlock,
                               MapPrevTx & inputs,
                               MapFractions& finputs,
                               std::map<uint256, CTxIndex>& mapTestPool,
                               MapFractions& mapTestFractionsPool,
                               const CFractions& feesFractions,
                               int64_t nCalculatedStakeRewardWithoutFees,
                               std::string& sPegFailCause);
void PrunePegForBlock(const CBlock&, CPegDB&);

#endif
