// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITBAY_PEG_H
#define BITBAY_PEG_H

#include <functional>
#include <list>
#include <set>

#include "bignum.h"
#include "pegdata.h"
#include "pegstd.h"
#include "tinyformat.h"

class CTxDB;
class CPegDB;
class CBlock;
class CBlockIndex;
class CTxOut;
class CTxIndex;
class CTransaction;
class CPegLevel;
class CFractions;
class CBitcoinAddress;
typedef std::map<uint256, std::pair<CTxIndex, CTransaction> > MapPrevTx;
typedef std::map<uint320, CTxOut>                             MapPrevOut;

extern int  nPegStartHeight;
extern int  nPegMaxSupplyIndex;
extern bool fPegIsActivatedViaTx;

// functors for messagings
typedef std::function<void(const std::string&)> LoadMsg;

bool SetBlocksIndexesReadyForPeg(CTxDB& ctxdb, LoadMsg load_msg);
bool CalculateBlockPegIndex(CPegDB& pegdb, CBlockIndex* pindex);
bool CalculateBlockPegVotes(const CBlock& cblock,
                            CBlockIndex*  pindex,
                            CTxDB&        txdb,
                            CPegDB&       pegdb,
                            std::string&  staker_addr);
int  CalculatePegVotes(const CFractions& fractions, int nPegSupplyIndex);

bool CalculateStandardFractions(const CTransaction& tx,
                                int                 nSupply,
                                uint32_t            nBlockTime,
                                MapPrevTx&          inputs,
                                MapFractions&       finputs,
                                std::set<uint32_t>  setTimeLockPass,
                                MapFractions&       mapTestFractionsPool,
                                CFractions&         feesFractions,
                                std::string&        sPegFailCause);

bool CalculateCoinMintFractions(const CTransaction&                      tx,
                                int                                      nSupply,
                                uint32_t                                 nTime,
                                const std::map<std::string, CBridgeInfo> bridges,
                                std::function<CMerkleInfo(std::string)>  fnMerkleIn,
                                int                                      nBridgePoolNout,
                                MapPrevTx&                               inputs,
                                MapFractions&                            finputs,
                                MapFractions&                            mapTestFractionsPool,
                                CFractions&                              feesFractions,
                                std::string&                             sPegFailCause);

bool CalculateStakingFractions(const CTransaction&          tx,
                               const CBlockIndex*           pindexBlock,
                               MapPrevTx&                   inputs,
                               MapFractions&                finputs,
                               std::map<uint256, CTxIndex>& mapTestPool,
                               MapFractions&                mapTestFractionsPool,
                               const CFractions&            feesFractions,
                               int64_t                      nCalculatedStakeRewardWithoutFees,
                               std::string&                 sPegFailCause);

void PrunePegForBlock(const CBlock&, CPegDB&);

// bridge

bool ConnectConsensusStates(CPegDB& pegdb, CBlockIndex* pindex);

bool CalculateBlockBridgeVotes(const CBlock& cblock,
                               CBlockIndex*  pindex,
                               CTxDB&        txdb,
                               CPegDB&       pegdb,
                               std::string   staker_addr);

bool FetchBridgePoolsFractions(CPegDB&       pegdb,
                               CBlockIndex*  pindex,
                               MapFractions& mapQueuedFractionsChanges);
bool ConnectBridgeCycleBurns(CPegDB&       pegdb,
                             CBlockIndex*  pindex,
                             MapFractions& mapQueuedFractionsChanges);

bool ComputeMintMerkleLeaf(const std::string&   dest_addr_str,
                           std::vector<int64_t> sections,
                           int                  section_peg,
                           int                  nonce,
                           const std::string&   from,
                           std::string&         out_leaf_hex);
bool ComputeMintMerkleRoot(const std::string&       inp_leaf_hex,
                           std::vector<std::string> proofs,
                           std::string&             out_root_hex);
bool DecodeAndValidateMintSigScript(const CScript&            sigScript,
                                    CBitcoinAddress&          addr_dest,
                                    std::vector<int64_t>&     sections,
                                    int&                      section_peg,
                                    int&                      nonce,
                                    std::string&              from,
                                    std::string&              leaf,
                                    std::vector<std::string>& proofs);

#endif
