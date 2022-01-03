// Copyright (c) 2018 yshurik
//
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The use in another cyptocurrency project the code is licensed under
// Jelurida Public License (JPL). See https://www.jelurida.com/resources/jpl

#include <map>
#include <set>
#include <cstdint>
#include <type_traits>
#include <fstream>
#include <utility>
#include <algorithm>

#include <boost/version.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

#include <leveldb/env.h>
#include <leveldb/cache.h>
#include <leveldb/filter_policy.h>
#include <memenv/memenv.h>

#include "peg.h"
#include "txdb-leveldb.h"
#include "pegdb-leveldb.h"
#include "util.h"
#include "main.h"
#include "base58.h"

#include <zconf.h>
#include <zlib.h>

using namespace std;
using namespace boost;

int nPegStartHeight = 1000000000; // 1G blocks as high number
int nPegMaxSupplyIndex = 1198;
bool fPegIsActivatedViaTx = false;

static string sBurnAddress =
    "bJnV8J5v74MGctMyVSVPfGu1mGQ9nMTiB3";

extern leveldb::DB *txdb; // global pointer for LevelDB object instance

bool SetBlocksIndexesReadyForPeg(CTxDB & ctxdb, LoadMsg load_msg) {
    leveldb::Iterator *iterator = txdb->NewIterator(leveldb::ReadOptions());
    // Seek to start key.
    CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
    ssStartKey << make_pair(string("blockindex"), uint256(0));
    iterator->Seek(ssStartKey.str());
    // Now read each entry.
    int indexCount = 0;
    while (iterator->Valid())
    {
        boost::this_thread::interruption_point();
        // Unpack keys and values.
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.write(iterator->key().data(), iterator->key().size());
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        ssValue.write(iterator->value().data(), iterator->value().size());
        string strType;
        ssKey >> strType;
        // Did we reach the end of the data to read?
        if (strType != "blockindex")
            break;
        CDiskBlockIndex diskindex;
        ssValue >> diskindex;

        uint256 blockHash = diskindex.GetBlockHash();
        CBlockIndex* pindexNew = ctxdb.InsertBlockIndex(blockHash);

        pindexNew->SetPeg(pindexNew->nHeight >= nPegStartHeight);
        diskindex.SetPeg(pindexNew->nHeight >= nPegStartHeight);
        ctxdb.WriteBlockIndex(diskindex);

        iterator->Next();

        indexCount++;
        if (indexCount % 10000 == 0) {
            load_msg(std::string(" update block indexes for peg: ")+std::to_string(indexCount));
        }
    }
    delete iterator;

    if (!ctxdb.WriteBlockIndexIsPegReady(true))
        return error("SetBlocksIndexesReadyForPeg() : flag write failed");

    return true;
}

bool CalculateBlockPegIndex(CBlockIndex* pindex)
{
    if (!pindex->pprev) {
        pindex->nPegSupplyIndex =0;
        return true;
    }

    pindex->nPegSupplyIndex = pindex->pprev->GetNextBlockPegSupplyIndex();
    return true;
}

int CBlockIndex::GetNextBlockPegSupplyIndex() const
{
    int nNextHeight = nHeight+1;
    int nPegInterval = Params().PegInterval(nNextHeight);

    if (nNextHeight < nPegStartHeight) {
        return 0;
    }
    if (nNextHeight % nPegInterval != 0) {
        return nPegSupplyIndex;
    }

    // back to 2 intervals and -1 to count voice of back-third interval, as votes sum at nPegInterval-1
    auto usevotesindex = this;
    while (usevotesindex->nHeight > (nNextHeight - nPegInterval*2 -1))
        usevotesindex = usevotesindex->pprev;

    // back to 3 intervals and -1 for votes calculations of 2x and 3x
    auto prevvotesindex = this;
    while (prevvotesindex->nHeight > (nNextHeight - nPegInterval*3 -1))
        prevvotesindex = prevvotesindex->pprev;

    return ComputeNextPegSupplyIndex(nPegSupplyIndex, usevotesindex, prevvotesindex);
}

int CBlockIndex::GetNextIntervalPegSupplyIndex() const
{
    if (nHeight < nPegStartHeight) {
        return 0;
    }

    int nPegInterval = Params().PegInterval(nHeight);
    int nCurrentInterval = nHeight / nPegInterval;
    int nCurrentIntervalStart = nCurrentInterval * nPegInterval;

    // back to 2 intervals and -1 to count voice of back-third interval, as votes sum at nPegInterval-1
    auto usevotesindex = this;
    while (usevotesindex->nHeight > (nCurrentIntervalStart - nPegInterval*1 -1))
        usevotesindex = usevotesindex->pprev;

    // back to 3 intervals and -1 for votes calculations of 2x and 3x
    auto prevvotesindex = this;
    while (prevvotesindex->nHeight > (nCurrentIntervalStart - nPegInterval*2 -1))
        prevvotesindex = prevvotesindex->pprev;

    return CBlockIndex::ComputeNextPegSupplyIndex(nPegSupplyIndex, usevotesindex, prevvotesindex);
}

int CBlockIndex::GetNextNextIntervalPegSupplyIndex() const
{
    if (nHeight < nPegStartHeight) {
        return 0;
    }

    int nPegInterval = Params().PegInterval(nHeight);
    int nCurrentInterval = nHeight / nPegInterval;
    int nCurrentIntervalStart = nCurrentInterval * nPegInterval;

    // back to 2 intervals and -1 to count voice of back-third interval, as votes sum at nPegInterval-1
    auto usevotesindex = this;
    while (usevotesindex->nHeight > (nCurrentIntervalStart - nPegInterval*0 -1))
        usevotesindex = usevotesindex->pprev;

    // back to 3 intervals and -1 for votes calculations of 2x and 3x
    auto prevvotesindex = this;
    while (prevvotesindex->nHeight > (nCurrentIntervalStart - nPegInterval*1 -1))
        prevvotesindex = prevvotesindex->pprev;

    return CBlockIndex::ComputeNextPegSupplyIndex(GetNextIntervalPegSupplyIndex(), usevotesindex, prevvotesindex);
}

// #NOTE15
int CBlockIndex::ComputeNextPegSupplyIndex(int nPegBase,
                                           const CBlockIndex* back2interval,
                                           const CBlockIndex* back3interval) {
    auto usevotesindex = back2interval;
    auto prevvotesindex = back3interval;
    int nNextPegSupplyIndex = nPegBase;

    int inflate = usevotesindex->nPegVotesInflate;
    int deflate = usevotesindex->nPegVotesDeflate;
    int nochange = usevotesindex->nPegVotesNochange;

    int inflate_prev = prevvotesindex->nPegVotesInflate;
    int deflate_prev = prevvotesindex->nPegVotesDeflate;
    int nochange_prev = prevvotesindex->nPegVotesNochange;

    if (deflate > inflate && deflate > nochange) {
        nNextPegSupplyIndex++;
        if (deflate > 2*inflate_prev && deflate > 2*nochange_prev) nNextPegSupplyIndex++;
        if (deflate > 3*inflate_prev && deflate > 3*nochange_prev) nNextPegSupplyIndex++;
    }
    if (inflate > deflate && inflate > nochange) {
        nNextPegSupplyIndex--;
        if (inflate > 2*deflate_prev && inflate > 2*nochange_prev) nNextPegSupplyIndex--;
        if (inflate > 3*deflate_prev && inflate > 3*nochange_prev) nNextPegSupplyIndex--;
    }

    // over max
    if (nNextPegSupplyIndex >= nPegMaxSupplyIndex)
        nNextPegSupplyIndex = nPegMaxSupplyIndex;
    // less min
    else if (nNextPegSupplyIndex <0)
        nNextPegSupplyIndex = 0;

    return nNextPegSupplyIndex;
}

int CalculatePegVotes(const CFractions & fractions, int nPegSupplyIndex)
{
    int nVotes=1;

    int64_t nReserveWeight=0;
    int64_t nLiquidWeight=0;

    fractions.LowPart(nPegSupplyIndex, &nReserveWeight);
    fractions.HighPart(nPegSupplyIndex, &nLiquidWeight);

    if (nLiquidWeight > INT_LEAST64_MAX/(nPegSupplyIndex+2)) {
        // check for rare extreme case when user stake more than about 100M coins
        // in this case multiplication is very close int64_t overflow (int64 max is ~92 GCoins)
        multiprecision::uint128_t nLiquidWeight128(nLiquidWeight);
        multiprecision::uint128_t nPegSupplyIndex128(nPegSupplyIndex);
        multiprecision::uint128_t nPegMaxSupplyIndex128(nPegMaxSupplyIndex);
        multiprecision::uint128_t f128 = (nLiquidWeight128*nPegSupplyIndex128)/nPegMaxSupplyIndex128;
        nLiquidWeight -= f128.convert_to<int64_t>();
    }
    else // usual case, fast calculations
        nLiquidWeight -= nLiquidWeight * nPegSupplyIndex / nPegMaxSupplyIndex;

    int nWeightMultiplier = nPegSupplyIndex/120+1;
    if (nLiquidWeight > (nReserveWeight*4)) {
        nVotes = 4*nWeightMultiplier;
    }
    else if (nLiquidWeight > (nReserveWeight*3)) {
        nVotes = 3*nWeightMultiplier;
    }
    else if (nLiquidWeight > (nReserveWeight*2)) {
        nVotes = 2*nWeightMultiplier;
    }

    return nVotes;
}

bool CalculateBlockPegVotes(const CBlock & cblock, CBlockIndex* pindex, CPegDB& pegdb)
{
    int nPegInterval = Params().PegInterval(pindex->nHeight);

    if (!cblock.IsProofOfStake() || pindex->nHeight < nPegStartHeight) {
        pindex->nPegVotesInflate =0;
        pindex->nPegVotesDeflate =0;
        pindex->nPegVotesNochange =0;
        return true;
    }

    if (pindex->nHeight % nPegInterval == 0) {
        pindex->nPegVotesInflate =0;
        pindex->nPegVotesDeflate =0;
        pindex->nPegVotesNochange =0;
    }
    else if (pindex->pprev) {
        pindex->nPegVotesInflate = pindex->pprev->nPegVotesInflate;
        pindex->nPegVotesDeflate = pindex->pprev->nPegVotesDeflate;
        pindex->nPegVotesNochange = pindex->pprev->nPegVotesNochange;
    }

    int nVotes=1;

    const CTransaction & tx = cblock.vtx[1];

    size_t n_vin = tx.vin.size();
    if (tx.IsCoinBase()) n_vin = 0;
    for (unsigned int i = 0; i < n_vin; i++)
    {
        CFractions fractions(0, CFractions::STD);
        const COutPoint & prevout = tx.vin[i].prevout;
        if (!pegdb.ReadFractions(uint320(prevout.hash, prevout.n), fractions)) {
            return false;
        }

        nVotes = CalculatePegVotes(fractions, pindex->nPegSupplyIndex);
        break;
    }

    for(const CTxOut & out : tx.vout) {
        const CScript& scriptPubKey = out.scriptPubKey;

        txnouttype type;
        vector<CTxDestination> addresses;
        int nRequired;

        if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
            continue;
        }

        bool voted = false;
        for(const CTxDestination& addr : addresses) {
            std::string str_addr = CBitcoinAddress(addr).ToString();
            if (str_addr == Params().PegInflateAddr()) {
                pindex->nPegVotesInflate += nVotes;
                voted = true;
                break;
            }
            else if (str_addr == Params().PegDeflateAddr()) {
                pindex->nPegVotesDeflate += nVotes;
                voted = true;
                break;
            }
            else if (str_addr == Params().PegNochangeAddr()) {
                pindex->nPegVotesNochange += nVotes;
                voted = true;
                break;
            }
        }

        if (voted) // only one vote to count
            break;
    }

    return true;
}

static string toAddress(const CScript& scriptPubKey,
                        bool* ptrIsNotary = nullptr,
                        string* ptrNotary = nullptr) {
    int nRequired;
    txnouttype type;
    vector<CTxDestination> addresses;
    if (ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
        std::string str_addr_all;
        bool fNone = true;
        for(const CTxDestination& addr : addresses) {
            std::string str_addr = CBitcoinAddress(addr).ToString();
            if (!str_addr_all.empty())
                str_addr_all += "\n";
            str_addr_all += str_addr;
            fNone = false;
        }
        if (!fNone)
            return str_addr_all;
    }

    if (ptrNotary || ptrIsNotary) {
        if (ptrIsNotary) *ptrIsNotary = false;
        if (ptrNotary) *ptrNotary = "";

        opcodetype opcode1;
        vector<unsigned char> vch1;
        CScript::const_iterator pc1 = scriptPubKey.begin();
        if (scriptPubKey.GetOp(pc1, opcode1, vch1)) {
            if (opcode1 == OP_RETURN && scriptPubKey.size()>1) {
                if (ptrIsNotary) *ptrIsNotary = true;
                if (ptrNotary) {
                    unsigned long len_bytes = scriptPubKey[1];
                    if (len_bytes > scriptPubKey.size()-2)
                        len_bytes = scriptPubKey.size()-2;
                    for (uint32_t i=0; i< len_bytes; i++) {
                        ptrNotary->push_back(char(scriptPubKey[i+2]));
                    }
                }
            }
        }
    }

    string as_bytes;
    unsigned long len_bytes = scriptPubKey.size();
    for(unsigned int i=0; i< len_bytes; i++) {
        as_bytes += char(scriptPubKey[i]);
    }
    return as_bytes;
}

bool CalculateStandardFractions(const CTransaction & tx,
                                int nSupply,
                                unsigned int nTime,
                                MapPrevTx & mapTxInputs,
                                MapFractions& mapInputsFractions,
                                MapFractions& mapTestFractionsPool,
                                CFractions& feesFractions,
                                std::string& sFailCause)
{
    MapPrevOut mapInputs;
    size_t n_vin = tx.vin.size();

    if (tx.IsCoinBase()) n_vin = 0;
    for (unsigned int i = 0; i < n_vin; i++)
    {
        const COutPoint & prevout = tx.vin[i].prevout;
        CTransaction& txPrev = mapTxInputs[prevout.hash].second;
        if (prevout.n >= txPrev.vout.size()) {
            sFailCause = "P-G-P-1: Refered output out of range";
            return false;
        }

        auto fkey = uint320(prevout.hash, prevout.n);
        mapInputs[fkey] = txPrev.vout[prevout.n];
    }

    return CalculateStandardFractions(tx,
                                      nSupply,
                                      nTime,
                                      mapInputs,
                                      mapInputsFractions,
                                      mapTestFractionsPool,
                                      feesFractions,
                                      sFailCause);
}

extern bool CalculateStakingFractions_testnet200k(const CTransaction & tx,
                                                  const CBlockIndex* pindexBlock,
                                                  MapPrevTx & inputs,
                                                  MapFractions& fInputs,
                                                  std::map<uint256, CTxIndex>& mapTestPool,
                                                  MapFractions& mapTestFractionsPool,
                                                  const CFractions& feesFractions,
                                                  int64_t nCalculatedStakeRewardWithoutFees,
                                                  std::string& sFailCause);

bool CalculateStakingFractions_v2(const CTransaction & tx,
                                  const CBlockIndex* pindexBlock,
                                  MapPrevTx & inputs,
                                  MapFractions& fInputs,
                                  std::map<uint256, CTxIndex>& mapTestPool,
                                  MapFractions& mapTestFractionsPool,
                                  const CFractions& feesFractions,
                                  int64_t nCalculatedStakeRewardWithoutFees,
                                  std::string& sFailCause);

bool CalculateStakingFractions(const CTransaction & tx,
                               const CBlockIndex* pindexBlock,
                               MapPrevTx & inputs,
                               MapFractions& fInputs,
                               std::map<uint256, CTxIndex>& mapTestPool,
                               MapFractions& mapTestFractionsPool,
                               const CFractions& feesFractions,
                               int64_t nCalculatedStakeRewardWithoutFees,
                               std::string& sFailCause)
{
    if (!pindexBlock) {
        // need a block info
        return false;
    }
    if (TestNet()) {
        if (pindexBlock->nHeight < 200000) {
            return CalculateStakingFractions_testnet200k(
                        tx,
                        pindexBlock,
                        inputs,
                        fInputs,
                        mapTestPool,
                        mapTestFractionsPool,
                        feesFractions,
                        nCalculatedStakeRewardWithoutFees,
                        sFailCause);
        }
    }

    return CalculateStakingFractions_v2(
                tx,
                pindexBlock,
                inputs,
                fInputs,
                mapTestPool,
                mapTestFractionsPool,
                feesFractions,
                nCalculatedStakeRewardWithoutFees,
                sFailCause);
}

//extern std::map<string, int> stake_addr_stats;

bool CalculateStakingFractions_v2(const CTransaction & tx,
                                  const CBlockIndex* pindexBlock,
                                  MapPrevTx & inputs,
                                  MapFractions& fInputs,
                                  std::map<uint256, CTxIndex>& mapTestPool,
                                  MapFractions& mapTestFractionsPool,
                                  const CFractions& feesFractions,
                                  int64_t nCalculatedStakeRewardWithoutFees,
                                  std::string& sFailCause)
{
    size_t n_vin = tx.vin.size();
    size_t n_vout = tx.vout.size();

    if (n_vin != 1) {
        sFailCause = "More than one input";
        return false;
    }

    if (n_vout > 8) {
        sFailCause = "More than 8 outputs";
        return false;
    }

    int64_t nValueStakeIn = 0;
    CFractions frStake(0, CFractions::STD);

    string sInputAddress;

    // only one input
    {
        unsigned int i = 0;
        const COutPoint & prevout = tx.vin[i].prevout;
        CTransaction& txPrev = inputs[prevout.hash].second;
        if (prevout.n >= txPrev.vout.size()) {
            sFailCause = "P-I-1: Refered output out of range";
            return false;
        }

        int64_t nValue = txPrev.vout[prevout.n].nValue;
        nValueStakeIn = nValue;

        auto sAddress = toAddress(txPrev.vout[prevout.n].scriptPubKey);
        sInputAddress = sAddress;

        auto fkey = uint320(prevout.hash, prevout.n);
        if (fInputs.find(fkey) == fInputs.end()) {
            sFailCause = "P-I-2: No input fractions found";
            return false;
        }

        frStake = fInputs[fkey].Std();
        if (frStake.Total() != nValueStakeIn) {
            sFailCause = "P-I-3: Input fraction total mismatches value";
            return false;
        }
    }

    //stake_addr_stats[sInputAddress]++;

    // Check funds to be returned to same address
    int64_t nValueReturn = 0;
    for (unsigned int i = 0; i < n_vout; i++) {
        std::string sAddress = toAddress(tx.vout[i].scriptPubKey);
        if (sInputAddress == sAddress) {
            nValueReturn += tx.vout[i].nValue;
        }
    }
    if (nValueReturn < nValueStakeIn) {
        sFailCause = "No enough funds returned to input address";
        return false;
    }

    CFractions frReward(nCalculatedStakeRewardWithoutFees, CFractions::STD);
    frReward += feesFractions;

    int64_t nValueRewardLeft = frReward.Total();

    bool fFailedPegOut = false;
    unsigned int nStakeOut = 0;

    // Transfer mark and set stake output
    for (unsigned int i = 0; i < n_vout; i++)
    {
        int64_t nValue = tx.vout[i].nValue;

        auto fkey = uint320(tx.GetHash(), i);
        auto & frOut = mapTestFractionsPool[fkey];

        string sNotary;
        bool fNotary = false;
        auto sAddress = toAddress(tx.vout[i].scriptPubKey, &fNotary, &sNotary);

        // first output returning on same address and greater or equal stake value
        if (nValue >= nValueStakeIn && sInputAddress == sAddress) {

            if (nValue > (nValueStakeIn + nValueRewardLeft)) {
                sFailCause = "P-O-1: No enough coins for stake output";
                fFailedPegOut = true;
            }

            int64_t nValueToTake = nValue;
            int64_t nStakeToTake = nValue;

            if (nStakeToTake > nValueStakeIn) {
                nStakeToTake = nValueStakeIn;
            }

            // first take whole stake input
            nValueToTake -= nStakeToTake;
            frOut = frStake;

            // leftover value take from reward
            if (nValueToTake >0) {
                nValueRewardLeft -= nValueToTake;
                frReward.MoveRatioPartTo(nValueToTake, frOut);
            }

            // transfer marks and locktime
            if (frStake.nFlags & CFractions::NOTARY_F) {
                if (!frOut.SetMark(CFractions::MARK_TRANSFER, CFractions::NOTARY_F, frStake.nLockTime)) {
                    sFailCause = "P-O-2: Crossing marks are detected";
                    fFailedPegOut = true;
                }
            }
            else if (frStake.nFlags & CFractions::NOTARY_V) {
                if (!frOut.SetMark(CFractions::MARK_TRANSFER, CFractions::NOTARY_V, frStake.nLockTime)) {
                    sFailCause = "P-O-3: Crossing marks are detected";
                    fFailedPegOut = true;
                }
            }
            else if (frStake.nFlags & CFractions::NOTARY_C) {
                if (!frOut.SetMark(CFractions::MARK_TRANSFER, CFractions::NOTARY_C, frStake.nLockTime)) {
                    sFailCause = "P-O-4: Crossing marks are detected";
                    fFailedPegOut = true;
                }
            }

            nStakeOut = i;
            break;
        }
    }

    if (fFailedPegOut) {
        return false;
    }

    if (nStakeOut == 0 && !fFailedPegOut) {
        sFailCause = "P-O-5: No stake funds returned to input address";
        fFailedPegOut = true;
    }

    // Calculation of outputs
    for (unsigned int i = 0; i < n_vout; i++)
    {
        int64_t nValue = tx.vout[i].nValue;

        if (i == nStakeOut) {
            // already calculated and marked
            continue;
        }

        auto fkey = uint320(tx.GetHash(), i);
        auto & frOut = mapTestFractionsPool[fkey];

        if (nValue > nValueRewardLeft) {
            sFailCause = "P-O-6: No coins left";
            fFailedPegOut = true;
            break;
        }

        frReward.MoveRatioPartTo(nValue, frOut);
        nValueRewardLeft -= nValue;
    }

    if (!fFailedPegOut) {
        // lets do some extra checks for totals
        for (unsigned int i = 0; i < n_vout; i++)
        {
            auto fkey = uint320(tx.GetHash(), i);
            auto f = mapTestFractionsPool[fkey];
            int64_t nValue = tx.vout[i].nValue;
            if (nValue != f.Total() || !f.IsPositive()) {
                sFailCause = "P-O-7: Total mismatch on output "+std::to_string(i);
                fFailedPegOut = true;
                break;
            }
        }
    }

    if (fFailedPegOut) {
        // for now remove failed fractions from pool so they
        // are not written to db
        for (unsigned int i = 0; i < n_vout; i++) {
            auto fkey = uint320(tx.GetHash(), i);
            if (mapTestFractionsPool.count(fkey)) {
                auto it = mapTestFractionsPool.find(fkey);
                mapTestFractionsPool.erase(it);
            }
        }
        return false;
    }

    return true;
}

void PrunePegForBlock(const CBlock& blockprune, CPegDB& pegdb)
{
    for(size_t i=0; i<blockprune.vtx.size(); i++) {
        const CTransaction& tx = blockprune.vtx[i];
        for (size_t j=0; j< tx.vin.size(); j++) {
            COutPoint prevout = tx.vin[j].prevout;
            auto fkey = uint320(prevout.hash, prevout.n);
            pegdb.Erase(fkey);
        }
        if (!tx.IsCoinStake())
            continue;

        uint256 txhash = tx.GetHash();
        for (size_t j=0; j< tx.vout.size(); j++) {
            auto fkey = uint320(txhash, j);

            CTxOut out = tx.vout[j];

            if (out.nValue == 0) {
                pegdb.Erase(fkey);
                continue;
            }

            const CScript& scriptPubKey = out.scriptPubKey;
            txnouttype type;
            vector<CTxDestination> addresses;
            int nRequired;

            if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
                opcodetype opcode1;
                vector<unsigned char> vch1;
                CScript::const_iterator pc1 = scriptPubKey.begin();
                if (scriptPubKey.GetOp(pc1, opcode1, vch1)) {
                    if (opcode1 == OP_RETURN && scriptPubKey.size()>1) {
                        pegdb.Erase(fkey);
                        continue;
                    }
                }
                continue;
            }

            bool voted = false;
            for(const CTxDestination& addr : addresses) {
                std::string str_addr = CBitcoinAddress(addr).ToString();
                if (str_addr == Params().PegInflateAddr()) { voted = true; }
                else if (str_addr == Params().PegDeflateAddr()) { voted = true; }
                else if (str_addr == Params().PegNochangeAddr()) { voted = true; }
            }
            if (!voted)
                continue;

            pegdb.Erase(fkey);
        }
    }
}
