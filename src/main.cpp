// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2020 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include "alert.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "db.h"
#include "init.h"
#include "kernel.h"
#include "net.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "peg.h"
#include "base58.h"
#include "blockindexmap.h"

#include <zconf.h>
#include <zlib.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string.hpp>

#include <boost/bind/bind.hpp>
using namespace boost::placeholders;

using namespace std;
using namespace boost;

#if defined(NDEBUG)
# error "BitBay cannot be compiled without assertions."
#endif

//
// Global state
//

CCriticalSection cs_setpwalletRegistered;
set<CWallet*> setpwalletRegistered;

CCriticalSection cs_main;

CTxMemPool mempool;

CBlockIndexMap mapBlockIndex;
set<pair<COutPoint, unsigned int> > setStakeSeen;

CBigNum bnProofOfStakeLimit(~uint256(0) >> 20);
CBigNum bnProofOfStakeLimitV2(~uint256(0) >> 48);

int nStakeMinConfirmations = 120;
int nRecommendedConfirmations = 10;
unsigned int nStakeMinAge = 2 * 60 * 60; // 2 hours
unsigned int nModifierInterval = 10 * 60; // time to elapse before new modifier is computed

int nCoinbaseMaturity = 50;
CBlockIndex* pindexGenesisBlock = NULL;
int nBestHeight = -1;

uint256 nBestChainTrust = 0;
uint256 nBestInvalidTrust = 0;

uint256 hashBestChain = 0;
CBlockIndex* pindexBest = NULL;
int64_t nTimeBestReceived = 0;
bool fImporting = false;
bool fReindex = false;
bool fHaveGUI = false;
bool fAboutToSendGUI = false;

struct COrphanBlock {
    uint256 hashBlock;
    uint256 hashPrev;
    std::pair<COutPoint, unsigned int> stake;
    vector<unsigned char> vchBlock;
};
map<uint256, COrphanBlock*> mapOrphanBlocks;
multimap<uint256, COrphanBlock*> mapOrphanBlocksByPrev;
set<pair<COutPoint, unsigned int> > setStakeSeenOrphan;

map<uint256, CTransaction> mapOrphanTransactions;
map<uint256, set<uint256> > mapOrphanTransactionsByPrev;

// Constant stuff for coinbase transactions we create:
CScript COINBASE_FLAGS;

const string strMessageMagic = "BitBay Signed Message:\n";

//////////////////////////////////////////////////////////////////////////////
//
// dispatching functions
//

// These functions dispatch to one or all registered wallets

namespace {
struct CMainSignals {
    // Notifies listeners of updated transaction data (passing hash, transaction, and optionally the block it is found in.
    boost::signals2::signal<void (const CTransaction &, const CBlock *, bool, MapFractions& mapOutputFractions)> SyncTransaction;
    // Notifies listeners of an erased transaction (currently disabled, requires transaction replacement).
    boost::signals2::signal<void (const uint256 &)> EraseTransaction;
    // Notifies listeners of an updated transaction without new data (for now: a coinbase potentially becoming visible).
    boost::signals2::signal<void (const uint256 &)> UpdatedTransaction;
    // Notifies listeners of a new active block chain.
    boost::signals2::signal<void (const CBlockLocator &)> SetBestChain;
    // Notifies listeners about an inventory item being seen on the network.
    boost::signals2::signal<void (const uint256 &)> Inventory;
    // Tells listeners to broadcast their data.
    boost::signals2::signal<void (bool)> Broadcast;
} g_signals;
}

void RegisterWallet(CWalletInterface* pwalletIn) {
    g_signals.SyncTransaction.connect(boost::bind(&CWalletInterface::SyncTransaction, pwalletIn, _1, _2, _3, _4));
    g_signals.EraseTransaction.connect(boost::bind(&CWalletInterface::EraseFromWallet, pwalletIn, _1));
    g_signals.UpdatedTransaction.connect(boost::bind(&CWalletInterface::UpdatedTransaction, pwalletIn, _1));
    g_signals.SetBestChain.connect(boost::bind(&CWalletInterface::SetBestChain, pwalletIn, _1));
    g_signals.Inventory.connect(boost::bind(&CWalletInterface::Inventory, pwalletIn, _1));
    g_signals.Broadcast.connect(boost::bind(&CWalletInterface::ResendWalletTransactions, pwalletIn, _1));
}

void UnregisterWallet(CWalletInterface* pwalletIn) {
    g_signals.Broadcast.disconnect(boost::bind(&CWalletInterface::ResendWalletTransactions, pwalletIn, _1));
    g_signals.Inventory.disconnect(boost::bind(&CWalletInterface::Inventory, pwalletIn, _1));
    g_signals.SetBestChain.disconnect(boost::bind(&CWalletInterface::SetBestChain, pwalletIn, _1));
    g_signals.UpdatedTransaction.disconnect(boost::bind(&CWalletInterface::UpdatedTransaction, pwalletIn, _1));
    g_signals.EraseTransaction.disconnect(boost::bind(&CWalletInterface::EraseFromWallet, pwalletIn, _1));
    g_signals.SyncTransaction.disconnect(boost::bind(&CWalletInterface::SyncTransaction, pwalletIn, _1, _2, _3, _4));
}

void UnregisterAllWallets() {
    g_signals.Broadcast.disconnect_all_slots();
    g_signals.Inventory.disconnect_all_slots();
    g_signals.SetBestChain.disconnect_all_slots();
    g_signals.UpdatedTransaction.disconnect_all_slots();
    g_signals.EraseTransaction.disconnect_all_slots();
    g_signals.SyncTransaction.disconnect_all_slots();
}

void SyncWithWallets(const CTransaction &tx, 
                     const CBlock *pblock, 
                     bool fConnect, 
                     MapFractions& mapQueuedFractionsChanges) {
    g_signals.SyncTransaction(tx, pblock, fConnect, mapQueuedFractionsChanges);
}

void ResendWalletTransactions(bool fForce) {
    g_signals.Broadcast(fForce);
}


//////////////////////////////////////////////////////////////////////////////
//
// Registration of network node signals.
//

void RegisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.ProcessMessages.connect(&ProcessMessages);
    nodeSignals.SendMessages.connect(&SendMessages);
}

void UnregisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.ProcessMessages.disconnect(&ProcessMessages);
    nodeSignals.SendMessages.disconnect(&SendMessages);
}



//////////////////////////////////////////////////////////////////////////////
//
// mapOrphanTransactions
//

bool AddOrphanTx(const CTransaction& tx)
{
    uint256 hash = tx.GetHash();
    if (mapOrphanTransactions.count(hash))
        return false;

    // Ignore big transactions, to avoid a
    // send-big-orphans memory exhaustion attack. If a peer has a legitimate
    // large transaction with a missing parent then we assume
    // it will rebroadcast it later, after the parent transaction(s)
    // have been mined or received.
    // 10,000 orphans, each of which is at most 5,000 bytes big is
    // at most 500 megabytes of orphans:

    size_t nSize = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);

    if (nSize > 5000)
    {
        LogPrint("mempool", "ignoring large orphan tx (size: %u, hash: %s)\n", nSize, hash.ToString());
        return false;
    }

    mapOrphanTransactions[hash] = tx;
    for(const CTxIn& txin : tx.vin) {
        mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);
    }

    LogPrint("mempool", "stored orphan tx %s (mapsz %u)\n", hash.ToString(),
        mapOrphanTransactions.size());
    return true;
}

void static EraseOrphanTx(uint256 hash)
{
    map<uint256, CTransaction>::iterator it = mapOrphanTransactions.find(hash);
    if (it == mapOrphanTransactions.end())
        return;
    for(const CTxIn& txin : it->second.vin)
    {
        map<uint256, set<uint256> >::iterator itPrev = mapOrphanTransactionsByPrev.find(txin.prevout.hash);
        if (itPrev == mapOrphanTransactionsByPrev.end())
            continue;
        itPrev->second.erase(hash);
        if (itPrev->second.empty())
            mapOrphanTransactionsByPrev.erase(itPrev);
    }
    mapOrphanTransactions.erase(it);
}

unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans)
{
    unsigned int nEvicted = 0;
    while (mapOrphanTransactions.size() > nMaxOrphans)
    {
        // Evict a random orphan:
        uint256 randomhash = GetRandHash();
        map<uint256, CTransaction>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
        if (it == mapOrphanTransactions.end())
            it = mapOrphanTransactions.begin();
        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}







//////////////////////////////////////////////////////////////////////////////
//
// CTransaction and CTxIndex
//

bool CTransaction::ReadFromDisk(CTxDB& txdb, COutPoint prevout, CTxIndex& txindexRet)
{
    SetNull();
    if (!txdb.ReadTxIndex(prevout.hash, txindexRet))
        return false;
    if (!ReadFromDisk(txindexRet.pos))
        return false;
    if (prevout.n >= vout.size())
    {
        SetNull();
        return false;
    }
    return true;
}

bool CTransaction::ReadFromDisk(CTxDB& txdb, COutPoint prevout)
{
    CTxIndex txindex;
    return ReadFromDisk(txdb, prevout, txindex);
}

bool CTransaction::ReadFromDisk(COutPoint prevout)
{
    CTxDB txdb("r");
    CTxIndex txindex;
    return ReadFromDisk(txdb, prevout, txindex);
}

bool IsStandardTx(const CTransaction& tx, string& reason)
{
    if (tx.nVersion > CTransaction::CURRENT_VERSION || tx.nVersion < 1) {
        reason = "version";
        return false;
    }

    // Treat non-final transactions as non-standard to prevent a specific type
    // of double-spend attack, as well as DoS attacks. (if the transaction
    // can't be mined, the attacker isn't expending resources broadcasting it)
    // Basically we don't want to propagate transactions that can't be included in
    // the next block.
    //
    // However, IsFinalTx() is confusing... Without arguments, it uses
    // chainActive.Height() to evaluate nLockTime; when a block is accepted, chainActive.Height()
    // is set to the value of nHeight in the block. However, when IsFinalTx()
    // is called within CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a transaction can
    // be part of the *next* block, we need to call IsFinalTx() with one more
    // than chainActive.Height().
    //
    // Timestamps on the other hand don't get any special treatment, because we
    // can't know what timestamp the next block will have, and there aren't
    // timestamp applications where it matters.
    if (!IsFinalTx(tx, nBestHeight + 1)) {
        reason = "non-final";
        return false;
    }
    // nTime has different purpose from nLockTime but can be used in similar attacks
    if (tx.nTime > FutureDrift(GetAdjustedTime(), nBestHeight + 1)) {
        reason = "time-too-new";
        return false;
    }

    // Extremely large transactions with lots of inputs can cost the network
    // almost as much to process as they cost the sender in fees, because
    // computing signature hashes is O(ninputs*txsize). Limiting transactions
    // to MAX_STANDARD_TX_SIZE mitigates CPU exhaustion attacks.
    unsigned int sz = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);
    if (sz >= MAX_STANDARD_TX_SIZE) {
        reason = "tx-size";
        return false;
    }

    for(const CTxIn& txin : tx.vin)
    {
        // Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
        // keys. (remember the 520 byte limit on redeemScript size) That works
        // out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
        // bytes of scriptSig, which we round off to 1650 bytes for some minor
        // future-proofing. That's also enough to spend a 20-of-20
        // CHECKMULTISIG scriptPubKey, though such a scriptPubKey is not
        // considered standard)
        if (txin.scriptSig.size() > 1650) {
            reason = "scriptsig-size";
            return false;
        }
        if (!txin.scriptSig.IsPushOnly()) {
            reason = "scriptsig-not-pushonly";
            return false;
        }
        if (!txin.scriptSig.HasCanonicalPushes()) {
            reason = "scriptsig-non-canonical-push";
            return false;
        }
    }

    unsigned int nDataOut = 0;
    txnouttype whichType;
    for(const CTxOut& txout : tx.vout) {
        if (!::IsStandard(txout.scriptPubKey, whichType)) {
            reason = "scriptpubkey";
            return false;
        }
        if (whichType == TX_NULL_DATA)
            nDataOut++;
        if (txout.nValue == 0) {
            reason = "dust";
            return false;
        }
        if (!txout.scriptPubKey.HasCanonicalPushes()) {
            reason = "scriptpubkey-non-canonical-push";
            return false;
        }
    }

    // allow: more than one data txout

    return true;
}

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    AssertLockHeld(cs_main);
    // Time based nLockTime implemented in 0.1.6
    if (tx.nLockTime == 0)
        return true;
    if (nBlockHeight == 0)
        nBlockHeight = nBestHeight;
    if (nBlockTime == 0)
        nBlockTime = GetAdjustedTime();
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for(const CTxIn& txin : tx.vin) {
        if (!txin.IsFinal())
            return false;
    }
    return true;
}

//
// Check transaction inputs to mitigate two
// potential denial-of-service attacks:
//
// 1. scriptSigs with extra data stuffed into them,
//    not consumed by scriptPubKey (or P2SH script)
// 2. P2SH scripts with a crazy number of expensive
//    CHECKSIG/CHECKMULTISIG operations
//
bool AreInputsStandard(const CTransaction& tx, const MapPrevTx& mapInputs)
{
    if (tx.IsCoinBase())
        return true; // Coinbases don't use vin normally

    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        CTxOut prev;
        tx.GetOutputFor(tx.vin[i], mapInputs, prev);

        vector<vector<unsigned char> > vSolutions;
        txnouttype whichType;
        // get the scriptPubKey corresponding to this input:
        const CScript& prevScript = prev.scriptPubKey;
        if (!Solver(prevScript, whichType, vSolutions))
            return false;
        int nArgsExpected = ScriptSigArgsExpected(whichType, vSolutions);
        if (nArgsExpected < 0)
            return false;

        // Transactions with extra stuff in their scriptSigs are
        // non-standard. Note that this EvalScript() call will
        // be quick, because if there are any operations
        // beside "push data" in the scriptSig
        // IsStandard() will have already returned false
        // and this method isn't called.
        vector<vector<unsigned char> > stack;
        if (!EvalScript(stack, tx.vin[i].scriptSig, tx, i, SCRIPT_VERIFY_NONE, 0))
            return false;

        if (whichType == TX_SCRIPTHASH)
        {
            if (stack.empty())
                return false;
            CScript subscript(stack.back().begin(), stack.back().end());
            vector<vector<unsigned char> > vSolutions2;
            txnouttype whichType2;
            if (Solver(subscript, whichType2, vSolutions2))
            {
                int tmpExpected = ScriptSigArgsExpected(whichType2, vSolutions2);
                if (tmpExpected < 0)
                    return false;
                nArgsExpected += tmpExpected;
            }
            else
            {
                // Any other Script with less than 15 sigops OK:
                unsigned int sigops = subscript.GetSigOpCount(true);
                // ... extra data left on the stack after execution is OK, too:
                return (sigops <= MAX_P2SH_SIGOPS);
            }
        }

        if (stack.size() != (unsigned int)nArgsExpected)
            return false;
    }

    return true;
}

unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
    unsigned int nSigOps = 0;
    for(const CTxIn& txin : tx.vin)
    {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    for(const CTxOut& txout : tx.vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const MapPrevTx& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        CTxOut prevout;
        tx.GetOutputFor(tx.vin[i], inputs, prevout);
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

int CMerkleTx::SetMerkleBranch(const CBlock* pblock)
{
    AssertLockHeld(cs_main);

    CBlock blockTmp;
    if (pblock == NULL)
    {
        // Load the block this tx is in
        CTxIndex txindex;
        if (!CTxDB("r").ReadTxIndex(GetHash(), txindex))
            return 0;
        if (!blockTmp.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos))
            return 0;
        pblock = &blockTmp;
    }

    // Update the tx's hashBlock
    hashBlock = pblock->GetHash();

    // Locate the transaction
    for (nIndex = 0; nIndex < (int)pblock->vtx.size(); nIndex++)
        if (pblock->vtx[nIndex] == *(CTransaction*)this)
            break;
    if (nIndex == (int)pblock->vtx.size())
    {
        vMerkleBranch.clear();
        nIndex = -1;
        LogPrintf("ERROR: SetMerkleBranch() : couldn't find tx in block\n");
        return 0;
    }

    // Fill in merkle branch
    vMerkleBranch = pblock->GetMerkleBranch(nIndex);

    // Is the tx in a block that's in the main chain
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !pindex->IsInMainChain())
        return 0;

    return pindexBest->nHeight - pindex->nHeight + 1;
}







bool CTransaction::CheckTransaction() const
{
    // Basic checks that don't depend on any context
    if (vin.empty())
        return DoS(10, error("CTransaction::CheckTransaction() : vin empty"));
    if (vout.empty())
        return DoS(10, error("CTransaction::CheckTransaction() : vout empty"));
    // Size limits
    if (::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
        return DoS(100, error("CTransaction::CheckTransaction() : size limits failed"));

    // Check for negative or overflow output values
    int64_t nValueOut = 0;
    for (unsigned int i = 0; i < vout.size(); i++)
    {
        const CTxOut& txout = vout[i];
        if (txout.IsEmpty() && !IsCoinBase() && !IsCoinStake())
            return DoS(100, error("CTransaction::CheckTransaction() : txout empty for user transaction"));
        if (txout.nValue < 0)
            return DoS(100, error("CTransaction::CheckTransaction() : txout.nValue negative"));
        if (txout.nValue > MAX_MONEY)
            return DoS(100, error("CTransaction::CheckTransaction() : txout.nValue too high"));
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return DoS(100, error("CTransaction::CheckTransaction() : txout total out of range"));
    }

    // Check for duplicate inputs
    set<COutPoint> vInOutPoints;
    for(const CTxIn& txin : vin)
    {
        if (vInOutPoints.count(txin.prevout))
            return false;
        vInOutPoints.insert(txin.prevout);
    }

    if (IsCoinBase())
    {
        if (vin[0].scriptSig.size() < 2 || vin[0].scriptSig.size() > 100)
            return DoS(100, error("CTransaction::CheckTransaction() : coinbase script size is invalid"));
    }
    else
    {
        for(const CTxIn& txin : vin) {
            if (txin.prevout.IsNull())
                return DoS(10, error("CTransaction::CheckTransaction() : prevout is null"));
        }
    }

    return true;
}

int64_t GetMinFee(const CTransaction& tx, int nHeight, unsigned int nBlockSize, enum GetMinFee_mode mode, unsigned int nBytes)
{
    // Base fee is either MIN_TX_FEE or MIN_RELAY_TX_FEE
    int64_t nBaseFee = (mode == GMF_RELAY) ? MIN_RELAY_TX_FEE : MIN_TX_FEE;

    unsigned int nNewBlockSize = nBlockSize + nBytes;
    int64_t nMinFee = (1 + (int64_t)nBytes / 1000) * nBaseFee;

    // Raise the price as the block approaches full
    if (nBlockSize != 1 && nNewBlockSize >= MAX_BLOCK_SIZE_GEN/2)
    {
        if (nNewBlockSize >= MAX_BLOCK_SIZE_GEN)
            return MAX_MONEY;
        nMinFee *= MAX_BLOCK_SIZE_GEN / (MAX_BLOCK_SIZE_GEN - nNewBlockSize);
    }
    
    // Min fee based on number of outputs and inputs
    if (IsProtocolVP(nHeight)) {
        int64_t nMinFeeByNum = PEG_MAKETX_FEE_INP_OUT * (tx.vin.size()+tx.vout.size());
        nMinFee = max(nMinFee, nMinFeeByNum);
    }

    if (!MoneyRange(nMinFee))
        nMinFee = MAX_MONEY;
    return nMinFee;
}


bool AcceptToMemoryPool(CTxMemPool& pool, 
                        CTransaction &tx, 
                        bool fLimitFree,
                        bool* pfMissingInputs)
{
    AssertLockHeld(cs_main);
    if (pfMissingInputs)
        *pfMissingInputs = false;

    if (!tx.CheckTransaction())
        return error("AcceptToMemoryPool : CheckTransaction failed");

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
        return tx.DoS(100, error("AcceptToMemoryPool : coinbase as individual tx"));

    // ppcoin: coinstake is also only valid in a block, not as a loose transaction
    if (tx.IsCoinStake())
        return tx.DoS(100, error("AcceptToMemoryPool : coinstake as individual tx"));

    // Rather not work on nonstandard transactions (unless -testnet)
    string reason;
    if (!TestNet() && !IsStandardTx(tx, reason))
        return error("AcceptToMemoryPool : nonstandard transaction: %s",
                     reason);

    // is it already in the memory pool?
    uint256 hash = tx.GetHash();
    if (pool.exists(hash))
        return false;

    // Check for conflicts with in-memory transactions
    {
        LOCK(pool.cs); // protect pool.mapNextTx
        for (unsigned int i = 0; i < tx.vin.size(); i++)
        {
            COutPoint outpoint = tx.vin[i].prevout;
            if (pool.mapNextTx.count(outpoint))
            {
                // Disable replacement feature for now
                return false;
            }
        }
    }

    MapPrevTx mapInputs;
    MapFractions mapInputsFractions;
    map<uint256, CTxIndex> mapUnused;
    MapPrevOut mapPrevOuts;
    MapFractions mapOutputsFractions;
    CFractions feesFractions;
    
    {
        CTxDB txdb("r");
        CPegDB pegdb("r");

        // do we already have it?
        if (txdb.ContainsTx(hash))
            return false;

        bool fInvalid = false;
        if (!tx.FetchInputs(txdb, pegdb, mapUnused, mapOutputsFractions, false, false, mapInputs, mapInputsFractions, fInvalid))
        {
            if (fInvalid)
                return error("AcceptToMemoryPool : FetchInputs found invalid tx %s", hash.ToString());
            if (pfMissingInputs)
                *pfMissingInputs = true;
            return false;
        }

        size_t n_vin = tx.vin.size();
        for (unsigned int i = 0; i < n_vin; i++)
        {
            const COutPoint & prevout = tx.vin[i].prevout;
            CTransaction& txPrev = mapInputs[prevout.hash].second;
            auto fkey = uint320(prevout.hash, prevout.n);
            mapPrevOuts[fkey] = txPrev.vout[prevout.n];
        }
        
        // Check for non-standard pay-to-script-hash in inputs
        if (!TestNet() && !AreInputsStandard(tx, mapInputs))
            return error("AcceptToMemoryPool : nonstandard transaction input");

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
        unsigned int nSigOps = GetLegacySigOpCount(tx);
        nSigOps += GetP2SHSigOpCount(tx, mapInputs);
        if (nSigOps > MAX_TX_SIGOPS)
            return tx.DoS(0,
                          error("AcceptToMemoryPool : too many sigops %s, %d > %d",
                                hash.ToString(), nSigOps, MAX_TX_SIGOPS));

        int64_t nFees = tx.GetValueIn(mapInputs)-tx.GetValueOut();
        unsigned int nSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);

        // Don't accept it if it can't get into a block
        int nHeight = pindexBest ? pindexBest->nHeight : 0;
        int64_t txMinFee = GetMinFee(tx, nHeight, 1000, GMF_RELAY, nSize);
        if ((fLimitFree && nFees < txMinFee) || (!fLimitFree && nFees < MIN_TX_FEE))
            return error("AcceptToMemoryPool : not enough fees %s, %d < %d",
                         hash.ToString(),
                         nFees, txMinFee);

        // Continuously rate-limit free transactions
        // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
        // be annoying or make others' transactions take longer to confirm.
        if (fLimitFree && nFees < MIN_RELAY_TX_FEE)
        {
            static CCriticalSection csFreeLimiter;
            static double dFreeCount;
            static int64_t nLastTime;
            int64_t nNow = GetTime();

            LOCK(csFreeLimiter);

            // Use an exponentially decaying ~10-minute window:
            dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
            nLastTime = nNow;
            // -limitfreerelay unit is thousand-bytes-per-minute
            // At default rate it would take over a month to fill 1GB
            if (dFreeCount > GetArg("-limitfreerelay", 15)*10*1000)
                return error("AcceptToMemoryPool : free transaction rejected by rate limiter");
            LogPrint("mempool", "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount+nSize);
            dFreeCount += nSize;
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        if (!tx.ConnectInputs(mapInputs, mapInputsFractions,
                              mapUnused, mapOutputsFractions,
                              feesFractions,
                              CDiskTxPos(1,1,1), pindexBest, false, false,
                              STANDARD_SCRIPT_VERIFY_FLAGS))
        {
            return error("AcceptToMemoryPool : ConnectInputs failed %s", hash.ToString());
        }

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack.
        MapFractions mapOutputsFractionsUnused;
        if (!tx.ConnectInputs(mapInputs, mapInputsFractions,
                              mapUnused, mapOutputsFractionsUnused,
                              feesFractions,
                              CDiskTxPos(1,1,1), pindexBest, false, false,
                              MANDATORY_SCRIPT_VERIFY_FLAGS))
        {
            return error("AcceptToMemoryPool: : BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s", hash.ToString());
        }
    }

    // Store transaction in memory
    pool.addUnchecked(hash, tx, mapPrevOuts, mapOutputsFractions);

    SyncWithWallets(tx, NULL, true, mapOutputsFractions);

    unsigned int nPoolSize = 0;
    {
        LOCK(pool.cs);
        nPoolSize = pool.mapTx.size();
    }
    
    LogPrint("mempool", "AcceptToMemoryPool : accepted %s (poolsz %u)\n",
           hash.ToString(),
           nPoolSize);
    return true;
}




int CMerkleTx::GetDepthInMainChainINTERNAL(CBlockIndex* &pindexRet) const
{
    if (hashBlock == 0 || nIndex == -1)
        return 0;
    AssertLockHeld(cs_main);

    // Find the block it claims to be in
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !pindex->IsInMainChain())
        return 0;

    // Make sure the merkle branch connects to this block
    if (!fMerkleVerified)
    {
        if (CBlock::CheckMerkleBranch(GetHash(), vMerkleBranch, nIndex) != pindex->hashMerkleRoot)
            return 0;
        fMerkleVerified = true;
    }

    pindexRet = pindex;
    return pindexBest->nHeight - pindex->nHeight + 1;
}

int CMerkleTx::GetDepthInMainChain(CBlockIndex* &pindexRet) const
{
    AssertLockHeld(cs_main);
    int nResult = GetDepthInMainChainINTERNAL(pindexRet);
    if (nResult == 0 && !mempool.exists(GetHash()))
        return -1; // Not in chain, not in mempool

    return nResult;
}

int CMerkleTx::GetBlocksToMaturity() const
{
    if (!(IsCoinBase() || IsCoinStake()))
        return 0;
    return max(0, (nCoinbaseMaturity+1) - GetDepthInMainChain());
}


bool CMerkleTx::AcceptToMemoryPool(bool fLimitFree)
{
    return ::AcceptToMemoryPool(mempool, *this, fLimitFree, NULL);
}



bool CWalletTx::AcceptWalletTransaction(CTxDB& txdb)
{

    {
        // Add previous supporting transactions first
        for(CMerkleTx& tx : vtxPrev)
        {
            if (!(tx.IsCoinBase() || tx.IsCoinStake()))
            {
                uint256 hash = tx.GetHash();
                if (!mempool.exists(hash) && !txdb.ContainsTx(hash))
                    tx.AcceptToMemoryPool(false);
            }
        }
        return AcceptToMemoryPool(false);
    }
    return false;
}

bool CWalletTx::AcceptWalletTransaction()
{
    CTxDB txdb("r");
    return AcceptWalletTransaction(txdb);
}

int CTxIndex::GetDepthInMainChain() const
{
    // Read block header
    CBlock block;
    if (!block.ReadFromDisk(pos.nFile, pos.nBlockPos, false))
        return 0;
    // Find the block in the index
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(block.GetHash());
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !pindex->IsInMainChain())
        return 0;
    return 1 + nBestHeight - pindex->nHeight;
}

int CTxIndex::GetHeightInMainChain(unsigned int* vtxidx, uint256 txhash, uint256* blockhash) const
{
    // Read block header
    CBlock block;
    if (!block.ReadFromDisk(pos.nFile, pos.nBlockPos, vtxidx != nullptr))
        return 0;
    if (vtxidx) {
        for(unsigned int i=0; i<block.vtx.size(); i++) {
            if (block.vtx[i].GetHash() == txhash)
                *vtxidx = i;
        }
    }
    // Find the block in the index
    uint256 bhash = block.GetHash();
    if (blockhash) *blockhash = bhash;
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(bhash);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !pindex->IsInMainChain())
        return 0;
    return pindex->nHeight;
}

// Return transaction in tx, and if it was found inside a block, its hash is placed in hashBlock
bool GetTransaction(const uint256 &hash, CTransaction &tx, uint256 &hashBlock)
{
    {
        LOCK(cs_main);
        {
            MapFractions mapFractions;
            if (mempool.lookup(hash, tx, mapFractions))
            {
                return true;
            }
        }
        CTxDB txdb("r");
        CTxIndex txindex;
        if (tx.ReadFromDisk(txdb, COutPoint(hash, 0), txindex))
        {
            CBlock block;
            if (block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false))
                hashBlock = block.GetHash();
            return true;
        }
    }
    return false;
}








//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

static CBlockIndex* pblockindexFBBHLast;
CBlockIndex* FindBlockByHeight(int nHeight)
{
    CBlockIndex *pblockindex;
    if (nHeight > nBestHeight)
        return NULL;
    if (nHeight < nBestHeight / 2)
        pblockindex = pindexGenesisBlock;
    else 
        pblockindex = pindexBest;
    if (pblockindexFBBHLast && abs(nHeight - pblockindex->nHeight) > abs(nHeight - pblockindexFBBHLast->nHeight))
        pblockindex = pblockindexFBBHLast;
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;
    while (pblockindex->nHeight < nHeight)
        pblockindex = pblockindex->pnext;
    pblockindexFBBHLast = pblockindex;
    return pblockindex;
}

bool CBlock::ReadFromDisk(const CBlockIndex* pindex, bool fReadTransactions)
{
    if (!fReadTransactions)
    {
        *this = pindex->GetBlockHeader();
        return true;
    }
    if (!ReadFromDisk(pindex->nFile, pindex->nBlockPos, fReadTransactions))
        return false;
    if (GetHash() != pindex->GetBlockHash())
        return error("CBlock::ReadFromDisk() : GetHash() doesn't match index");
    return true;
}

uint256 static GetOrphanRoot(const uint256& hash)
{
    map<uint256, COrphanBlock*>::iterator it = mapOrphanBlocks.find(hash);
    if (it == mapOrphanBlocks.end())
        return hash;

    // Work back to the first block in the orphan chain
    do {
        map<uint256, COrphanBlock*>::iterator it2 = mapOrphanBlocks.find(it->second->hashPrev);
        if (it2 == mapOrphanBlocks.end())
            return it->first;
        it = it2;
    } while(true);
}

// ppcoin: find block wanted by given orphan block
uint256 WantedByOrphan(const COrphanBlock* pblockOrphan)
{
    // Work back to the first block in the orphan chain
    while (mapOrphanBlocks.count(pblockOrphan->hashPrev))
        pblockOrphan = mapOrphanBlocks[pblockOrphan->hashPrev];
    return pblockOrphan->hashPrev;
}

// Remove a random orphan block (which does not have any dependent orphans).
void static PruneOrphanBlocks()
{
    if (mapOrphanBlocksByPrev.size() <= (size_t)std::max((int64_t)0, GetArg("-maxorphanblocks", DEFAULT_MAX_ORPHAN_BLOCKS)))
        return;

    // Pick a random orphan block.
    int pos = insecure_rand() % mapOrphanBlocksByPrev.size();
    std::multimap<uint256, COrphanBlock*>::iterator it = mapOrphanBlocksByPrev.begin();
    while (pos--) it++;

    // As long as this block has other orphans depending on it, move to one of those successors.
    do {
        std::multimap<uint256, COrphanBlock*>::iterator it2 = mapOrphanBlocksByPrev.find(it->second->hashBlock);
        if (it2 == mapOrphanBlocksByPrev.end())
            break;
        it = it2;
    } while(1);

    setStakeSeenOrphan.erase(it->second->stake);
    uint256 hash = it->second->hashBlock;
    delete it->second;
    mapOrphanBlocksByPrev.erase(it);
    mapOrphanBlocks.erase(hash);
}

static CBigNum GetProofOfStakeLimit(int nHeight)
{
    if (IsProtocolV2(nHeight))
        return bnProofOfStakeLimitV2;
    else
        return bnProofOfStakeLimit;
}

// miner's coin base reward
int64_t GetProofOfWorkReward(int64_t nFees)
{
    int64_t nSubsidy = 0 * COIN;

        if(pindexBest->nHeight == 1)
        {
            nSubsidy = 1000000000 * COIN; // 1 billion coins for ico
        }

    LogPrint("creation", "GetProofOfWorkReward() : create=%s nSubsidy=%d\n", FormatMoney(nSubsidy), nSubsidy);

    return nSubsidy + nFees;
}

// miner's coin stake reward
int64_t GetProofOfStakeReward(const CBlockIndex* pindexPrev, 
                              int64_t nCoinAge, /*for old protocols*/
                              int64_t nFees, 
                              const CFractions& inp)
{
    int64_t nSubsidy;
    if (IsProtocolV3(pindexPrev->nTime)) {
        if (IsProtocolVS(pindexPrev->nTime)) {
            if (IsProtocolVP((pindexPrev->nHeight+1))) {
                // #NOTE9
                if (inp.nFlags & CFractions::NOTARY_V) {
                    nSubsidy = COIN * 40;
                }
                else if (inp.nFlags & CFractions::NOTARY_F) {
                    nSubsidy = COIN * 20;
                }
                else {
                    int64_t reserve = inp.Low(pindexPrev->GetNextBlockPegSupplyIndex());
                    int64_t liquidity = inp.High(pindexPrev->GetNextBlockPegSupplyIndex());
                    if (liquidity < reserve) {
                        nSubsidy = COIN * 10;
                    } else {
                        nSubsidy = COIN * 5;
                    }
                }
            }
            else {
                nSubsidy = COIN * 20;
            }
        }
        else {
            nSubsidy = COIN * 3 / 2;
        }
    }
    else {
        nSubsidy = nCoinAge * COIN_YEAR_REWARD * 33 / (365 * 33 + 8);
    }

    LogPrint("creation", "GetProofOfStakeReward(): create=%s nCoinAge=%d\n", FormatMoney(nSubsidy), nCoinAge);

    return nSubsidy + nFees;
}

static const int64_t nTargetTimespan = 16 * 60;  // 16 mins

// ppcoin: find last block index up to pindex
const CBlockIndex* GetLastBlockIndex(const CBlockIndex* pindex, bool fProofOfStake)
{
    while (pindex && pindex->pprev && (pindex->IsProofOfStake() != fProofOfStake))
        pindex = pindex->pprev;
    return pindex;
}

unsigned int GetNextTargetRequired(const CBlockIndex* pindexLast, bool fProofOfStake)
{
    CBigNum bnTargetLimit = fProofOfStake ? GetProofOfStakeLimit(pindexLast->nHeight) : Params().ProofOfWorkLimit();

    if (pindexLast == NULL)
        return bnTargetLimit.GetCompact(); // genesis block

    const CBlockIndex* pindexPrev = GetLastBlockIndex(pindexLast, fProofOfStake);
    if (pindexPrev->pprev == NULL)
        return bnTargetLimit.GetCompact(); // first block
    const CBlockIndex* pindexPrevPrev = GetLastBlockIndex(pindexPrev->pprev, fProofOfStake);
    if (pindexPrevPrev->pprev == NULL)
        return bnTargetLimit.GetCompact(); // second block

    int64_t nTargetSpacing = GetTargetSpacing(pindexLast->nHeight);
    int64_t nActualSpacing = pindexPrev->GetBlockTime() - pindexPrevPrev->GetBlockTime();
    if (IsProtocolV1RetargetingFixed(pindexLast->nHeight)) {
        if (nActualSpacing < 0)
            nActualSpacing = nTargetSpacing;
    }
    if (IsProtocolV3(pindexLast->nTime)) {
        if (nActualSpacing > nTargetSpacing * 10)
            nActualSpacing = nTargetSpacing * 10;
    }

    // ppcoin: target change every block
    // ppcoin: retarget with exponential moving toward target spacing
    CBigNum bnNew;
    bnNew.SetCompact(pindexPrev->nBits);
    int64_t nInterval = nTargetTimespan / nTargetSpacing;
    bnNew *= ((nInterval - 1) * nTargetSpacing + nActualSpacing + nActualSpacing);
    bnNew /= ((nInterval + 1) * nTargetSpacing);

    if (bnNew <= 0 || bnNew > bnTargetLimit)
        bnNew = bnTargetLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits)
{
    CBigNum bnTarget;
    bnTarget.SetCompact(nBits);

    // Check range
    if (bnTarget <= 0 || bnTarget > Params().ProofOfWorkLimit())
        return error("CheckProofOfWork() : nBits below minimum work");

    // Check proof of work matches claimed amount
    if (hash > bnTarget.getuint256())
        return error("CheckProofOfWork() : hash doesn't match nBits");

    return true;
}

bool IsInitialBlockDownload()
{
    LOCK(cs_main);
    if (pindexBest == NULL || nBestHeight < Checkpoints::GetTotalBlocksEstimate())
        return true;
    static int64_t nLastUpdate;
    static CBlockIndex* pindexLastBest;
    if (pindexBest != pindexLastBest)
    {
        pindexLastBest = pindexBest;
        nLastUpdate = GetTime();
    }
    return (GetTime() - nLastUpdate < 15 &&
            pindexBest->GetBlockTime() < GetTime() - 8 * 60 * 60);
}

void static InvalidChainFound(CBlockIndex* pindexNew)
{
    if (pindexNew->nChainTrust > nBestInvalidTrust)
    {
        nBestInvalidTrust = pindexNew->nChainTrust;
        CTxDB().WriteBestInvalidTrust(CBigNum(nBestInvalidTrust));
    }

    uint256 nBestInvalidBlockTrust = pindexNew->nChainTrust - pindexNew->pprev->nChainTrust;
    uint256 nBestBlockTrust = pindexBest->nHeight != 0 ? (pindexBest->nChainTrust - pindexBest->pprev->nChainTrust) : pindexBest->nChainTrust;

    LogPrintf("InvalidChainFound: invalid block=%s  height=%d  trust=%s  blocktrust=%d  date=%s\n",
      pindexNew->GetBlockHash().ToString(), pindexNew->nHeight,
      CBigNum(pindexNew->nChainTrust).ToString(), nBestInvalidBlockTrust.GetLow64(),
      DateTimeStrFormat("%x %H:%M:%S", pindexNew->GetBlockTime()));
    LogPrintf("InvalidChainFound:  current best=%s  height=%d  trust=%s  blocktrust=%d  date=%s\n",
      hashBestChain.ToString(), nBestHeight,
      CBigNum(pindexBest->nChainTrust).ToString(),
      nBestBlockTrust.GetLow64(),
      DateTimeStrFormat("%x %H:%M:%S", pindexBest->GetBlockTime()));
}

void CBlock::UpdateTime(const CBlockIndex* pindexPrev)
{
    nTime = max(GetBlockTime(), GetAdjustedTime());
}

bool IsConfirmedInNPrevBlocks(const CTxIndex& txindex, const CBlockIndex* pindexFrom, int nMaxDepth, int& nActualDepth)
{
    for (const CBlockIndex* pindex = pindexFrom; pindex && pindexFrom->nHeight - pindex->nHeight < nMaxDepth; pindex = pindex->pprev)
    {
        if (pindex->nBlockPos == txindex.pos.nBlockPos && pindex->nFile == txindex.pos.nFile)
        {
            nActualDepth = pindexFrom->nHeight - pindex->nHeight;
            return true;
        }
    }

    return false;
}

string scripttoaddress(const CScript& scriptPubKey,
                       bool* ptrIsNotary = nullptr,
                       string* ptrNotary = nullptr);

bool CTransaction::IsExchangeTx(int & nOut, uint256 & wid) const
{
    nOut = -1;
    wid = uint256();
    size_t n_out = vout.size();
    for(size_t i=0; i< n_out; i++) {
        string sNotary;
        bool fNotary = false;
        scripttoaddress(vout[i].scriptPubKey, &fNotary, &sNotary);
        if (!fNotary) continue;
        if (!boost::starts_with(sNotary, "XCH:")) continue;
        vector<string> vOutputArgs;
        boost::split(vOutputArgs, sNotary, boost::is_any_of(":"));
        if (vOutputArgs.size() <2) {
            return true; // no output, just exchange tx
        }
        string sOut = vOutputArgs[1];
        nOut = std::stoi(sOut);
        if (nOut <0 || nOut >= int(n_out)) {
            nOut = -1;
            return true;
        }
        
        if (vOutputArgs.size() <3) {
            nOut = -1;
            return true; // no control hash
        }
        
        string sControlHash = vOutputArgs[2];
        int64_t nAmount = vout[nOut].nValue;
        string sAddress = scripttoaddress(vout[nOut].scriptPubKey, &fNotary, &sNotary);
        {
            { // liquid withdraw control
                CDataStream ss(SER_GETHASH, 0);
                size_t n_inp = vin.size();
                for(size_t j=0; j< n_inp; j++) {
                    ss << vin[j].prevout.hash;
                    ss << vin[j].prevout.n;
                }
                ss << string("L");
                ss << sAddress;
                ss << nAmount;
                string sHash = Hash(ss.begin(), ss.end()).GetHex();
                if (sHash == sControlHash) {
                    wid = uint256(sHash);
                    return true;
                }
            }
            
            { // reserve withdraw control
                CDataStream ss(SER_GETHASH, 0);
                size_t n_inp = vin.size();
                for(size_t j=0; j< n_inp; j++) {
                    ss << vin[j].prevout.hash;
                    ss << vin[j].prevout.n;
                }
                ss << string("R");
                ss << sAddress;
                ss << nAmount;
                string sHash = Hash(ss.begin(), ss.end()).GetHex();
                if (sHash == sControlHash) {
                    wid = uint256(sHash);
                    return true;
                }
            }
            
            // control id does not match, reset output number
            nOut = -1;
        }
        
        return true;
    }
    return false;
}

bool CTransaction::DisconnectInputs(CTxDB& txdb, CPegDB& pegdb)
{
    bool fUtxoDbEnabled = false;
    bool fUtxoDbIsReady = false;
    txdb.ReadUtxoDbEnabled(fUtxoDbEnabled);
    txdb.ReadUtxoDbIsReady(fUtxoDbIsReady);
    if (!fUtxoDbIsReady) fUtxoDbEnabled = false;
    
    // Relinquish previous transactions' spent pointers
    if (!IsCoinBase())
    {
        MapPrevTx mapInputs;
        MapFractions mapInputsFractions;
        MapFractions mapOutputsFractions;
        
        for(const CTxIn& txin : vin)
        {
            COutPoint prevout = txin.prevout;

            // Get prev txindex from disk
            CTxIndex& txindex = mapInputs[prevout.hash].first;
            if (!txdb.ReadTxIndex(prevout.hash, txindex))
                return error("DisconnectInputs() : ReadTxIndex failed");
            if (fUtxoDbEnabled) {
                CTransaction& prev = mapInputs[prevout.hash].second;
                if(!txdb.ReadDiskTx(prevout.hash, prev))
                    return error("DisconnectInputs() : ReadDiskTx failed");
                auto txoutid = uint320(prevout.hash, prevout.n);
                CFractions& fractions = mapInputsFractions[txoutid];
                fractions = CFractions(0, CFractions::VALUE);
                if (!pegdb.ReadFractions(txoutid, fractions, true /*must_have*/)) {
                    mapInputsFractions.erase(txoutid);
                }
            }

            if (prevout.n >= txindex.vSpent.size())
                return error("DisconnectInputs() : prevout.n out of range");

            // Mark outpoint as not spent
            txindex.vSpent[prevout.n].SetNull();

            // Write back
            if (!txdb.UpdateTxIndex(prevout.hash, txindex))
                return error("DisconnectInputs() : UpdateTxIndex failed");
        }
        
        if (fUtxoDbEnabled) {
            for(size_t i=0; i< vout.size(); i++)
            {
                auto txoutid = uint320(GetHash(), i);
                CFractions& fractions = mapOutputsFractions[txoutid];
                fractions = CFractions(0, CFractions::VALUE);
                if (!pegdb.ReadFractions(txoutid, fractions, true /*must_have*/)) {
                    mapOutputsFractions.erase(txoutid);
                }
            }
            if (!DisconnectUtxo(txdb, pegdb, mapInputs, mapInputsFractions, mapOutputsFractions)) {
                // report, but it is not cause to stop/reject for now
                error("DisconnectInputs() : DisconnectUtxo failed");
            }
        }
    }

    // Remove transaction from index
    // This can fail if a duplicate of this transaction was in a chain that got
    // reorganized away. This is only possible if this transaction was completely
    // spent, so erasing it would be a no-op anyway.
    txdb.EraseTxIndex(*this);

    return true;
}


bool CTransaction::FetchInputs(CTxDB& txdb,
                               CPegDB& pegdb,
                               const map<uint256, CTxIndex>& mapTestPool,
                               const MapFractions& mapTestFractionsPool,
                               bool fBlock, bool fMiner,
                               MapPrevTx& inputsRet,
                               MapFractions& finputsRet,
                               bool& fInvalid,
                               bool fSkipPruned)
{
    // FetchInputs can return false either because we just haven't seen some inputs
    // (in which case the transaction should be stored as an orphan)
    // or because the transaction is malformed (in which case the transaction should
    // be dropped).  If tx is definitely invalid, fInvalid will be set to true.
    fInvalid = false;

    if (IsCoinBase())
        return true; // Coinbase transactions have no inputs to fetch.

    for (unsigned int i = 0; i < vin.size(); i++)
    {
        COutPoint prevout = vin[i].prevout;
        if (inputsRet.count(prevout.hash))
            continue; // Got it already

        // Read txindex
        CTxIndex& txindex = inputsRet[prevout.hash].first;
        bool fFound = true;
        if ((fBlock || fMiner) && mapTestPool.count(prevout.hash))
        {
            // Get txindex from current proposed changes
            txindex = mapTestPool.find(prevout.hash)->second;
        }
        else
        {
            // Read txindex from txdb
            fFound = txdb.ReadTxIndex(prevout.hash, txindex);
        }
        if (!fFound && (fBlock || fMiner))
            return fMiner ? false : error("FetchInputs() : %s prev tx %s index entry not found", GetHash().ToString(),  prevout.hash.ToString());

        // Read txPrev
        CTransaction& txPrev = inputsRet[prevout.hash].second;
        if (!fFound || txindex.pos == CDiskTxPos(1,1,1))
        {
            // Get prev tx from single transactions in memory
            if (!mempool.lookup(prevout.hash, txPrev, finputsRet))
                return error("FetchInputs() : %s mempool Tx prev not found %s", GetHash().ToString(),  prevout.hash.ToString());
            if (!fFound)
                txindex.vSpent.resize(txPrev.vout.size());
        }
        else
        {
            // Get prev tx from disk
            if (!txPrev.ReadFromDisk(txindex.pos))
                return error("FetchInputs() : %s ReadFromDisk prev tx %s failed", GetHash().ToString(),  prevout.hash.ToString());
        }
    }

    // Make sure all prevout.n indexes are valid:
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        const COutPoint prevout = vin[i].prevout;
        assert(inputsRet.count(prevout.hash) != 0);
        const CTxIndex& txindex = inputsRet[prevout.hash].first;
        const CTransaction& txPrev = inputsRet[prevout.hash].second;
        if (prevout.n >= txPrev.vout.size() || prevout.n >= txindex.vSpent.size())
        {
            // Revisit this if/when transaction replacement is implemented and allows
            // adding inputs:
            fInvalid = true;
            return DoS(100, error("FetchInputs() : %s prevout.n out of range %d %u %u prev tx %s\n%s", 
                                  GetHash().ToString(), 
                                  prevout.n, 
                                  txPrev.vout.size(), 
                                  txindex.vSpent.size(), 
                                  prevout.hash.ToString(), 
                                  txPrev.ToString()));
        }
        // Read previous fractions
        auto fkey = uint320(prevout.hash, prevout.n);
        if (finputsRet.count(fkey)) {
            // Already filled from mempool
            continue;
        }
        CFractions& fractions = finputsRet[fkey];
        if ((fBlock || fMiner) && mapTestFractionsPool.count(fkey))
        {
            // Get fractions from current proposed changes
            fractions = mapTestFractionsPool.find(fkey)->second;
        }
        else {
            // Know the height
            bool fMustHaveFractions = false;
            int nHeight = txindex.nHeight;
            if (nHeight >= nPegStartHeight) {
                fMustHaveFractions = true;
            }
            fractions = CFractions(txPrev.vout[prevout.n].nValue, CFractions::VALUE);
            if (!pegdb.ReadFractions(fkey, fractions, fMustHaveFractions)) {
                if (fSkipPruned) {
                    finputsRet.erase(fkey);
                    continue;
                }
                return false;
            }
        }
    }

    return true;
}

void CTransaction::GetOutputFor(const CTxIn& input, const MapPrevTx& inputs, CTxOut& out) const
{
    MapPrevTx::const_iterator mi = inputs.find(input.prevout.hash);
    if (mi == inputs.end())
        throw std::runtime_error("CTransaction::GetOutputFor() : prevout.hash not found");

    const CTransaction& txPrev = (mi->second).second;
    if (input.prevout.n >= txPrev.vout.size())
        throw std::runtime_error("CTransaction::GetOutputFor() : prevout.n out of range");

    // Exception for baLN8KM7q9jizZrXXFLgMkf52bcTyfTieZ p2sh to BS4B3oTqEKw9vZVL45MZGEKsGL9sKPXiyC p2pkh
    unsigned char BS4BExceptionBytes[] = {0xa9, 0x14, 0xEC, 0xFD, 0xBC, 0x26, 0xA4, 0x93, 0x04, 0x1B, 0x5D, 0xB9, 0xF4, 0x83, 0x2F, 0xA0, 0xAF, 0x77, 0x11, 0xE2, 0x16, 0x47, 0x87};
    CScript BS4BExceptionScript(BS4BExceptionBytes,BS4BExceptionBytes + 23);
    if(txPrev.vout[input.prevout.n].scriptPubKey == BS4BExceptionScript) {
        unsigned char BS4BExceptionP2PKHBytes[] = {0x76, 0xa9, 0x14, 0xEC, 0xFD, 0xBC, 0x26, 0xA4, 0x93, 0x04, 0x1B, 0x5D, 0xB9, 0xF4, 0x83, 0x2F, 0xA0, 0xAF, 0x77, 0x11, 0xE2, 0x16, 0x47, 0x88, 0xac};
        out.nValue = txPrev.vout[input.prevout.n].nValue;
        out.scriptPubKey = CScript(BS4BExceptionP2PKHBytes, BS4BExceptionP2PKHBytes + 25);
        return;
    }

    out = txPrev.vout[input.prevout.n];
}

int64_t CTransaction::GetValueIn(const MapPrevTx& inputs) const
{
    if (IsCoinBase())
        return 0;

    int64_t nResult = 0;
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        CTxOut txout;
        GetOutputFor(vin[i], inputs, txout);
        nResult += txout.nValue;
    }
    return nResult;

}

bool CTransaction::ConnectInputs(MapPrevTx inputs,
                                 MapFractions& finputs,
                                 map<uint256, CTxIndex>& mapTestPool,
                                 MapFractions& mapTestFractionsPool,
                                 CFractions& feesFractions,
                                 const CDiskTxPos& posThisTx,
                                 const CBlockIndex* pindexBlock,
                                 bool fBlock, 
                                 bool fMiner, 
                                 unsigned int flags)
{
    // Take over previous transactions' spent pointers
    // fBlock is true when this is called from AcceptBlock when a new best-block is added to the blockchain
    // fMiner is true when called from the internal bitcoin miner
    // ... both are false when called from CTransaction::AcceptToMemoryPool
    if (IsCoinBase())
    {
        return true;
    }

    int64_t nValueIn = 0;
    int64_t nFees = 0;
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        COutPoint prevout = vin[i].prevout;
        assert(inputs.count(prevout.hash) > 0);
        CTxIndex& txindex = inputs[prevout.hash].first;
        CTransaction& txPrev = inputs[prevout.hash].second;

        if (prevout.n >= txPrev.vout.size() || prevout.n >= txindex.vSpent.size())
            return DoS(100, error("ConnectInputs() : %s prevout.n out of range %d %u %u prev tx %s\n%s", GetHash().ToString(), prevout.n, txPrev.vout.size(), txindex.vSpent.size(), prevout.hash.ToString(), txPrev.ToString()));

        // If prev is coinbase or coinstake, check that it's matured
        if (txPrev.IsCoinBase() || txPrev.IsCoinStake())
        {
            int nSpendDepth;
            if (IsConfirmedInNPrevBlocks(txindex, pindexBlock, nCoinbaseMaturity, nSpendDepth))
                return error("ConnectInputs() : tried to spend %s at depth %d", txPrev.IsCoinBase() ? "coinbase" : "coinstake", nSpendDepth);
        }

        // ppcoin: check transaction timestamp
        if (txPrev.nTime > nTime)
            return DoS(100, error("ConnectInputs() : transaction timestamp earlier than input transaction"));

        if (IsProtocolV3(nTime))
        {
            if (txPrev.vout[prevout.n].IsEmpty())
                return DoS(1, error("ConnectInputs() : special marker is not spendable"));
        }

        // Check for negative or overflow input values
        nValueIn += txPrev.vout[prevout.n].nValue;
        if (!MoneyRange(txPrev.vout[prevout.n].nValue) || !MoneyRange(nValueIn))
            return DoS(100, error("ConnectInputs() : txin values out of range"));

    }

    // Calculation of fractions is considered less expensive than
    // signatures checks. Same time collect all fees fractions.
    // #NOTE1, #NOTE2
    if (!IsCoinStake()) {
        if (pindexBlock->nHeight >= nPegStartHeight) {
            string sPegFailCause;
            bool peg_ok = CalculateStandardFractions(*this,
                                                     pindexBlock->nPegSupplyIndex,
                                                     pindexBlock->nTime,
                                                     inputs, finputs,
                                                     mapTestFractionsPool,
                                                     feesFractions,
                                                     sPegFailCause);
            if (!peg_ok) {
                return DoS(100, error("ConnectInputs() : fail on calculations of tx fractions (cause=%s)", sPegFailCause.c_str()));
            }
        }
    }

    // The first loop above does all the inexpensive checks.
    // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
    // Helps prevent CPU exhaustion attacks.
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        COutPoint prevout = vin[i].prevout;
        assert(inputs.count(prevout.hash) > 0);
        CTxIndex& txindex = inputs[prevout.hash].first;
        CTransaction& txPrev = inputs[prevout.hash].second;

        // Check for conflicts (double-spend)
        // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
        // for an attacker to attempt to split the network.
        if (!txindex.vSpent[prevout.n].IsNull())
            return fMiner ? false : error("ConnectInputs() : %s prev tx already used at %s", GetHash().ToString(), txindex.vSpent[prevout.n].ToString());

        // Skip ECDSA signature verification when connecting blocks (fBlock=true)
        // before the last blockchain checkpoint. This is safe because block merkle hashes are
        // still computed and checked, and any change will be caught at the next checkpoint.
        if (!(fBlock && (nBestHeight < Checkpoints::GetTotalBlocksEstimate())))
        {
            // Verify signature
            if (!VerifySignature(txPrev, *this, i, flags, 0))
            {
                if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
                    // Check whether the failure was caused by a
                    // non-mandatory script verification check, such as
                    // non-null dummy arguments;
                    // if so, don't trigger DoS protection to
                    // avoid splitting the network between upgraded and
                    // non-upgraded nodes.
                    if (VerifySignature(txPrev, *this, i, flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, 0))
                        return error("ConnectInputs() : %s non-mandatory VerifySignature failed", GetHash().ToString());
                }
                // Failures of other flags indicate a transaction that is
                // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
                // such nodes as they are not following the protocol. That
                // said during an upgrade careful thought should be taken
                // as to the correct behavior - we may want to continue
                // peering with non-upgraded nodes even after a soft-fork
                // super-majority vote has passed.
                return DoS(100,error("ConnectInputs() : %s VerifySignature failed", GetHash().ToString()));
            }
        }

        // Mark outpoints as spent
        txindex.vSpent[prevout.n] = posThisTx;

        // Write back
        if (fBlock || fMiner)
        {
            mapTestPool[prevout.hash] = txindex;
        }
    }

    if (IsCoinStake()) {
        return true;
    }

    if (nValueIn < GetValueOut())
        return DoS(100, error("ConnectInputs() : %s value in < value out", GetHash().ToString()));

    // Tally transaction fees
    int64_t nTxFee = nValueIn - GetValueOut();
    if (nTxFee < 0)
        return DoS(100, error("ConnectInputs() : %s nTxFee < 0", GetHash().ToString()));

    nFees += nTxFee;
    if (!MoneyRange(nFees))
        return DoS(100, error("ConnectInputs() : nFees out of range"));

    return true;
}

static void CreateUtxoHistoryRecord(CTxDB& txdb, 
                                    string sAddress,
                                    uint64_t nTime,
                                    uint64_t nHeight,
                                    int64_t nIndex,
                                    uint256 txhash,
                                    map<string, CAddressBalance>& mapAddressesBalances,
                                    map<string, int64_t>& mapAddressesBalancesIdxs) {
    if (mapAddressesBalances.count(sAddress)) 
        return; // already present
    int64_t nLastIndex = -1;
    CAddressBalance & balance = mapAddressesBalances[sAddress];
    txdb.ReadAddressLastBalance(sAddress, balance, nLastIndex);
    mapAddressesBalancesIdxs[sAddress] = nLastIndex+1;
    balance.nTime   = nTime;
    balance.nHeight = nHeight;
    balance.nIndex  = nIndex;
    balance.txhash  = txhash;
    balance.nCredit = 0;
    balance.nDebit  = 0;
}

bool CTransaction::ConnectUtxo(CTxDB& txdb, const CBlockIndex* pindex, int16_t nTxIdx, 
                               MapPrevTx& mapInputs, 
                               MapFractions& mapInputsFractions,
                               MapFractions& mapOutputsFractions) const
{
    auto txhash = GetHash();
    map<string, int64_t> mapAddressesBalancesIdxs;
    map<string, CAddressBalance> mapAddressesBalances;
    
    bool isStakeFrozen = false;
    
    // credit input addresses
    // remove spents utxo
    for(size_t j =0; j < vin.size(); j++) {
        if (IsCoinBase()) continue;
        const COutPoint & prevout = vin[j].prevout;
        const CTransaction& prev = mapInputs[prevout.hash].second;
        if (prev.vout.size() <= prevout.n)
            return error("ConnectUtxo() : wrong prevout.n");
        const CTxOut & txout = prev.vout[prevout.n];
        CTxDestination dst;
        if(!ExtractDestination(txout.scriptPubKey, dst))
            continue;
        string sAddress = CBitcoinAddress(dst).ToString();
        if (sAddress.size() != 34)
            continue;
        
        auto txoutid = uint320(prevout.hash, prevout.n);
        CFractions frbase(txout.nValue, CFractions::VALUE);
        CFractions& fractions = frbase;
        bool frozen = false;
        if (mapInputsFractions.count(txoutid)) {
            fractions = mapInputsFractions[txoutid];
            if ((fractions.nFlags & CFractions::NOTARY_F) || 
                (fractions.nFlags & CFractions::NOTARY_V)) { 
                // check if it is still in queue or not over lock time
                CFrozenQueued skip;
                frozen = txdb.ReadFrozenQueued(fractions.nLockTime, txoutid, skip);
                if (IsCoinStake()) {
                    isStakeFrozen = frozen;
                    if (fractions.nLockTime > (pindex->nTime+1000))
                        isStakeFrozen = true;
                }
                if (!txdb.EraseFromFrozenQueue(fractions.nLockTime, txoutid))
                    return error("ConnectUtxo() : EraseFromFrozenQueue");
            }
        }
        // remove spent or frozen
        CAddressUnspent unspent;
        if (txdb.ReadUnspent(sAddress, txoutid, unspent)) {
            if (!txdb.DeductSpent(sAddress, fractions, unspent.nHeight >= nPegStartHeight))
                return error("ConnectUtxo() : DeductSpent");
            if (!txdb.EraseUnspent(sAddress, txoutid))
                return error("ConnectUtxo() : EraseUnspent");
        } else {
            if (!txdb.EraseFrozen(sAddress, txoutid))
                return error("ConnectUtxo() : EraseFrozen");
        }
        // make a record if not ready
        CreateUtxoHistoryRecord(txdb, 
                                sAddress, 
                                nTime,
                                pindex->nHeight,
                                nTxIdx,
                                txhash,
                                mapAddressesBalances, 
                                mapAddressesBalancesIdxs);
        // credit record
        CAddressBalance & balance = mapAddressesBalances[sAddress];
        balance.nCredit += txout.nValue;
        if (frozen)
            balance.nFrozen -= txout.nValue;
    }
    // make new via outputs
    for(size_t j =0; j < vout.size(); j++) {
        const CTxOut & txout = vout[j];
        CTxDestination dst;
        if(!ExtractDestination(txout.scriptPubKey, dst))
            continue;
        string sAddress = CBitcoinAddress(dst).ToString();
        if (sAddress.size() != 34)
            continue;
        
        // add new utxo
        CAddressUnspent unspent;
        unspent.nHeight = pindex->nHeight;
        unspent.nIndex  = nTxIdx;
        unspent.nAmount = txout.nValue;
        auto txoutid = uint320(txhash, j);
        CFractions frbase(txout.nValue, CFractions::VALUE);
        CFractions& fractions = frbase;
        bool frozen = false;
        if (mapOutputsFractions.count(txoutid)) {
            fractions = mapOutputsFractions[txoutid];
            if (fractions.nFlags & CFractions::NOTARY_F) { 
                unspent.nFlags = CFractions::NOTARY_F;
                unspent.nLockTime = fractions.nLockTime;
                frozen = IsCoinStake() ? isStakeFrozen : true;
            }
            if (fractions.nFlags & CFractions::NOTARY_V) {
                unspent.nFlags = CFractions::NOTARY_V;
                unspent.nLockTime = fractions.nLockTime;
                frozen = IsCoinStake() ? isStakeFrozen : true;
            }
        }
        if (frozen) {
            if (!txdb.AddFrozen(sAddress, txoutid, unspent))
                return error("ConnectUtxo() : AddFrozen");
            if (!txdb.AddToFrozenQueue(unspent.nLockTime, txoutid, CFrozenQueued(sAddress,unspent.nAmount)))
                return error("ConnectUtxo() : AddToFrozenQueue");
        } else {
            if (!txdb.AddUnspent(sAddress, txoutid, unspent))
                return error("ConnectUtxo() : AddUnspent");
            if (!txdb.AppendUnspent(sAddress, fractions, unspent.nHeight >= nPegStartHeight))
                return error("ConnectUtxo() : AppendSpent");
        }
        // make a record if not ready
        CreateUtxoHistoryRecord(txdb, 
                                sAddress, 
                                nTime,
                                pindex->nHeight,
                                nTxIdx,
                                txhash,
                                mapAddressesBalances, 
                                mapAddressesBalancesIdxs);
        // debit record
        CAddressBalance & balance = mapAddressesBalances[sAddress];
        balance.nDebit += txout.nValue;
        // frozen change
        if (frozen)
            balance.nFrozen += txout.nValue;
    }
    // write history records
    for(pair<const string, CAddressBalance> & item : mapAddressesBalances) {
        const string& sAddress = item.first;
        CAddressBalance& balance = item.second;
        int64_t nIdx = mapAddressesBalancesIdxs[sAddress];
        int64_t nDiff = balance.nDebit - balance.nCredit;
        if (nDiff < 0) {
            balance.nDebit = 0;
            balance.nCredit = -nDiff;
        } else if (nDiff > 0) {
            balance.nDebit = nDiff;
            balance.nCredit = 0;
        } else {
            balance.nDebit = 0;
            balance.nCredit = 0;
        }
        balance.nBalance = balance.nBalance + nDiff;
        if (!txdb.AddBalance(sAddress, nIdx, balance))
            return error("ConnectUtxo() : AddBalance");
    }
    
    return true;
}

// remove records from frozen queue and add artificial 
// balance records indicating the unfreezing amount
bool CBlock::ProcessFrozenQueue(CTxDB& txdb, 
                                CPegDB& pegdb,
                                MapFractions& mapFractions,
                                const CBlockIndex* pindex,
                                bool fLoading)
{
    std::vector<CFrozenQueued> records;
    txdb.ReadFrozenQueue(nTime, records);
    for(auto record : records) {
        // 1. remove from queue
        if (!txdb.EraseFromFrozenQueue(record.nLockTime, record.txoutid))
            return error("ProcessFrozenQueue() : EraseFromFrozenQueue");
        // 2. unfreezing balance record
        int64_t nLastIndex = -1;
        CAddressBalance balance;
        if (!txdb.ReadAddressLastBalance(record.sAddress, balance, nLastIndex))
            return error("ProcessFrozenQueue() : ReadAddressLastBalance");
        nLastIndex = nLastIndex+1;
        balance.nTime     = nTime;                  /*blocktime*/
        balance.nHeight   = pindex->nHeight;        /*blockheight*/
        balance.nIndex    = -1-record.txoutid.b2(); /*minus indicate unfreeze*/
        balance.txhash    = record.txoutid.b1();    /*txhash*/
        balance.nCredit   = 0;
        balance.nDebit    = record.nAmount; 
        balance.nLockTime = record.nLockTime;
        balance.nFrozen   -= record.nAmount;
        if (!txdb.AddBalance(record.sAddress, nLastIndex, balance))
            return error("ConnectUtxo() : AddBalance");
        // 3. move from ftxo to utxo
        CAddressUnspent frozen;
        if (!txdb.ReadFrozen(record.sAddress, record.txoutid, frozen))
            return error("ProcessFrozenQueue() : ReadFrozen");
        if (!txdb.EraseFrozen(record.sAddress, record.txoutid))
            return error("ProcessFrozenQueue() : EraseFrozen");
        if (!txdb.AddUnspent(record.sAddress, record.txoutid, frozen))
            return error("ProcessFrozenQueue() : AddUnspent");
        if (balance.nHeight >= uint64_t(nPegStartHeight)) {
            CFractions fractions(frozen.nAmount, CFractions::VALUE);
            if (mapFractions.count(record.txoutid))
                fractions = mapFractions[record.txoutid];
            else
                if (!pegdb.ReadFractions(record.txoutid, fractions, !fLoading /*must_have*/))
                    return error("ProcessFrozenQueue() : ReadFractions: %s", record.txoutid.GetHex());
            if (!txdb.AppendUnspent(record.sAddress, fractions, true /*peg_on*/))
                return error("ProcessFrozenQueue() : AppendSpent");
        }
    }
    return true;
}

bool CTransaction::DisconnectUtxo(CTxDB& txdb, 
                                  CPegDB& pegdb,
                                  MapPrevTx& mapInputs, 
                                  MapFractions& mapInputsFractions,
                                  MapFractions& mapOutputsFractions) const
{
    auto txhash = GetHash();
    set<string> setAddresses;
    
    // collect addresses
    for(size_t j =0; j < vout.size(); j++) {
        const CTxOut & txout = vout[j];
        CTxDestination dst;
        if(!ExtractDestination(txout.scriptPubKey, dst))
            continue;
        string sAddress = CBitcoinAddress(dst).ToString();
        if (sAddress.size() != 34)
            continue;
        
        setAddresses.insert(sAddress);
    }
    // collect addresses
    for(size_t j =0; j < vin.size(); j++) {
        if (IsCoinBase()) continue;
        const COutPoint & prevout = vin[j].prevout;
        const CTransaction& prev = mapInputs[prevout.hash].second;
        if (prev.vout.size() <= prevout.n)
            return error("DisconnectUtxo() : wrong prevout.n");
        const CTxOut & txout = prev.vout[prevout.n];
        CTxDestination dst;
        if(!ExtractDestination(txout.scriptPubKey, dst))
            continue;
        string sAddress = CBitcoinAddress(dst).ToString();
        if (sAddress.size() != 34)
            continue;
        
        setAddresses.insert(sAddress);
    }
    
    // before disconnecting utxos move frozen back to pool
    // and remove unfreezing balance history records (they are top)
    // and add them back as frozen queue records, 
    // so it affects only intersecting addresses
    for(string sAddress : setAddresses) {
        bool done = false;
        do {
            int64_t nLastIndex = -1;
            CAddressBalance balance;
            if (!txdb.ReadAddressLastBalance(sAddress, balance, nLastIndex)) 
                return error("DisconnectUtxo() : frozen queue: ReadAddressLastBalance not found last record for %s", sAddress);
            if (balance.nIndex <0) {
                if (!txdb.EraseBalance(sAddress, nLastIndex))
                    return error("DisconnectUtxo() : frozen queue: EraseBalance (unfreezing)");
                // back to freeze queue
                uint64_t nLockTime = balance.nLockTime;
                uint320 txoutid(balance.txhash, -balance.nIndex-1);
                if (!txdb.AddToFrozenQueue(nLockTime, txoutid, CFrozenQueued(sAddress, balance.nDebit)))
                    return error("DisconnectUtxo() : frozen queue: AddToFrozenQueue");
                // move from utxo to ftxo
                CAddressUnspent unspent;
                if (!txdb.ReadUnspent(sAddress, txoutid, unspent))
                    return error("DisconnectUtxo() : frozen queue: ReadUnspent");
                if (balance.nHeight >= uint64_t(nPegStartHeight)) {
                    CFractions fractions(unspent.nAmount, CFractions::VALUE);
                    if (!pegdb.ReadFractions(txoutid, fractions, true /*must_have*/))
                        return error("DisconnectUtxo() : frozen queue: ReadFractions");
                    if (!txdb.DeductSpent(sAddress, fractions, true /*peg_on*/))
                        return error("DisconnectUtxo() : DeductSpent");
                }
                if (!txdb.EraseUnspent(sAddress, txoutid))
                    return error("DisconnectUtxo() : frozen queue: EraseFrozen");
                if (!txdb.AddFrozen(sAddress, txoutid, unspent))
                    return error("DisconnectUtxo() : frozen queue: AddUnspent");
            } else {
                done = true;
            }
        } while(!done);
    }
    
    bool isStakeFrozen = false;
    
    // remove utxos via outputs
    for(size_t j =0; j < vout.size(); j++) {
        const CTxOut & txout = vout[j];
        CTxDestination dst;
        if(!ExtractDestination(txout.scriptPubKey, dst))
            continue;
        string sAddress = CBitcoinAddress(dst).ToString();
        if (sAddress.size() != 34)
            continue;
        
        auto txoutid = uint320(txhash, j);
        CFractions frbase(txout.nValue, CFractions::VALUE);
        CFractions& fractions = frbase;
        if (mapOutputsFractions.count(txoutid)) {
            fractions = mapOutputsFractions[txoutid];
            if ((fractions.nFlags & CFractions::NOTARY_F) || 
                (fractions.nFlags & CFractions::NOTARY_V)) { 
                if (IsCoinStake()) {
                    CFrozenQueued skip;
                    isStakeFrozen = txdb.ReadFrozenQueued(fractions.nLockTime, txoutid, skip);
                }
                if (!txdb.EraseFromFrozenQueue(fractions.nLockTime, txoutid))
                    return error("DisconnectUtxo() : EraseFromFrozenQueue");
            }
        }
        CAddressUnspent unspent;
        if (txdb.ReadUnspent(sAddress, txoutid, unspent)) {
            if (!txdb.DeductSpent(sAddress, fractions, unspent.nHeight >= nPegStartHeight))
                return error("DisconnectUtxo() : DeductSpent");
            if (!txdb.EraseUnspent(sAddress, txoutid))
                return error("DisconnectUtxo() : EraseUnspent");
        } else {
            if (!txdb.EraseFrozen(sAddress, txoutid))
                return error("DisconnectUtxo() : EraseFrozen");
        }
    }
    // reappend spent utxos via inputs
    for(size_t j =0; j < vin.size(); j++) {
        if (IsCoinBase()) continue;
        const COutPoint & prevout = vin[j].prevout;
        const CTxIndex& prevtxindex = mapInputs[prevout.hash].first;
        const CTransaction& prev = mapInputs[prevout.hash].second;
        if (prev.vout.size() <= prevout.n)
            return error("DisconnectUtxo() : wrong prevout.n");
        const CTxOut & txout = prev.vout[prevout.n];
        CTxDestination dst;
        if(!ExtractDestination(txout.scriptPubKey, dst))
            continue;
        string sAddress = CBitcoinAddress(dst).ToString();
        if (sAddress.size() != 34)
            continue;
        
        CAddressUnspent unspent;
        unspent.nHeight = prevtxindex.nHeight;
        unspent.nIndex  = prevtxindex.nIndex;
        unspent.nAmount = txout.nValue;
        auto txoutid = uint320(prevout.hash, prevout.n);
        CFractions frbase(txout.nValue, CFractions::VALUE);
        CFractions& fractions = frbase;
        bool frozen = false;
        if (mapInputsFractions.count(txoutid)) {
            fractions = mapInputsFractions[txoutid];
            if (fractions.nFlags & CFractions::NOTARY_F) { 
                unspent.nFlags = CFractions::NOTARY_F;
                unspent.nLockTime = fractions.nLockTime;
                frozen = IsCoinStake() ? isStakeFrozen : true;
            }
            if (fractions.nFlags & CFractions::NOTARY_V) {
                unspent.nFlags = CFractions::NOTARY_V;
                unspent.nLockTime = fractions.nLockTime;
                frozen = IsCoinStake() ? isStakeFrozen : true;
            }
        }
        if (frozen) {
            if (!txdb.AddFrozen(sAddress, txoutid, unspent))
                return error("DisconnectUtxo() : AddFrozen");
            if (!txdb.AddToFrozenQueue(unspent.nLockTime, txoutid, CFrozenQueued(sAddress, unspent.nAmount)))
                return error("DisconnectUtxo() : AddToFrozenQueue");
        } else {
            if (!txdb.AddUnspent(sAddress, txoutid, unspent))
                return error("DisconnectUtxo() : AddUnspent");
            if (!txdb.AppendUnspent(sAddress, fractions, unspent.nHeight >= nPegStartHeight))
                return error("DisconnectUtxo() : AppendSpent");
        }
    }
    // remove balance history records
    for(string sAddress : setAddresses) {
        int64_t nLastIndex = -1;
        CAddressBalance balance;
        if (!txdb.ReadAddressLastBalance(sAddress, balance, nLastIndex)) 
            return error("DisconnectUtxo() : ReadAddressLastBalance not found last record for %s", sAddress);
        if (balance.txhash != txhash)
            return error("DisconnectUtxo() : ReadAddressLastBalance has invalid txhash %s, expected %s", 
                         balance.txhash.GetHex(), txhash.GetHex());
        if (!txdb.EraseBalance(sAddress, nLastIndex))
            return error("DisconnectUtxo() : EraseBalance");
    }
    return true;
}

bool CBlock::DisconnectBlock(CTxDB& txdb, CPegDB& pegdb, CBlockIndex* pindex)
{
    // Disconnect in reverse order
    for (int i = vtx.size()-1; i >= 0; i--) {
        if (!vtx[i].DisconnectInputs(txdb, pegdb)) {
            return false;
        }
        uint256 txhash = vtx[i].GetHash();
        for (unsigned int j = vtx[i].vout.size(); j-- > 0;) {
            auto fkey = uint320(txhash, j);
            pegdb.Erase(fkey);
        }
    }

    // Update block index on disk without changing it in memory.
    // The memory index structure will be changed after the db commits.
    if (pindex->pprev)
    {
        CDiskBlockIndex blockindexPrev(pindex->pprev);
        blockindexPrev.hashNext = 0;
        if (!txdb.WriteBlockIndex(blockindexPrev))
            return error("DisconnectBlock() : WriteBlockIndex failed");
    }

    // ppcoin: clean up wallet after disconnecting coinstake
    MapFractions mapFractionsSkip;
    for(const CTransaction& tx : vtx) {
        SyncWithWallets(tx, this, false, mapFractionsSkip);
    }
    
    // Track of exchange txs
    for(const CTransaction& tx : vtx) {
        uint256 wid;
        int nOut = -1;
        if (!tx.IsExchangeTx(nOut, wid)) continue;
        if (wid == 0) continue;
        pegdb.RemovePegTxId(wid);
    }

    return true;
}

bool CBlock::ConnectBlock(CTxDB& txdb, CPegDB& pegdb, CBlockIndex* pindex, bool fJustCheck)
{
    // Check it again in case a previous version let a bad block in, but skip BlockSig checking
    if (!CheckBlock(!fJustCheck, !fJustCheck, false))
        return false;

    unsigned int flags = SCRIPT_VERIFY_NOCACHE;

    if (IsProtocolV3(nTime))
    {
        flags |= SCRIPT_VERIFY_NULLDUMMY |
                 SCRIPT_VERIFY_STRICTENC |
                 SCRIPT_VERIFY_ALLOW_EMPTY_SIG |
                 SCRIPT_VERIFY_FIX_HASHTYPE |
                 SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }
    
    // track utxo db changes
    bool fUtxoDbEnabled = false;
    bool fUtxoDbIsReady = false;
    txdb.ReadUtxoDbEnabled(fUtxoDbEnabled);
    txdb.ReadUtxoDbIsReady(fUtxoDbIsReady);
    if (!fUtxoDbIsReady) fUtxoDbEnabled = false;

    //// issue here: it doesn't know the version
    unsigned int nTxPos;
    if (fJustCheck)
        // FetchInputs treats CDiskTxPos(1,1,1) as a special "refer to memorypool" indicator
        // Since we're just checking the block and not actually connecting it, it might not (and probably shouldn't) be on the disk to get the transaction from
        nTxPos = 1;
    else
        nTxPos = pindex->nBlockPos + ::GetSerializeSize(CBlock(), SER_DISK, CLIENT_VERSION) - (2 * GetSizeOfCompactSize(0)) + GetSizeOfCompactSize(vtx.size());

    // bitbay: prepare peg supply index information
    CalculateBlockPegIndex(pindex);

    map<size_t,MapPrevTx> mapInputs;
    map<size_t,MapFractions> mapInputsFractions;
    map<uint256, CTxIndex> mapQueuedChanges;
    MapFractions mapQueuedFractionsChanges;
    CFractions feesFractions;
    int64_t nFees = 0;
    int64_t nValueIn = 0;
    int64_t nValueOut = 0;
    int64_t nStakeReward = 0;
    unsigned int nSigOps = 0;
    for(size_t i=0; i< vtx.size(); i++)
    {
        CTransaction& tx = vtx[i];
        uint256 hashTx = tx.GetHash();

        // Do not allow blocks that contain transactions which 'overwrite' older transactions,
        // unless those are already completely spent.
        // If such overwrites are allowed, coinbases and transactions depending upon those
        // can be duplicated to remove the ability to spend the first instance -- even after
        // being sent to another address.
        // See BIP30 and http://r6.ca/blog/20120206T005236Z.html for more information.
        // This logic is not necessary for memory pool transactions, as AcceptToMemoryPool
        // already refuses previously-known transaction ids entirely.
        // This rule was originally applied all blocks whose timestamp was after March 15, 2012, 0:00 UTC.
        // Now that the whole chain is irreversibly beyond that time it is applied to all blocks except the
        // two in the chain that violate it. This prevents exploiting the issue against nodes in their
        // initial block download.
        CTxIndex txindexOld;
        if (txdb.ReadTxIndex(hashTx, txindexOld)) {
            for(CDiskTxPos &pos : txindexOld.vSpent) {
                if (pos.IsNull())
                    return DoS(100, error("ConnectBlock() : tried to overwrite transaction"));
            }
        }

        nSigOps += GetLegacySigOpCount(tx);
        if (nSigOps > MAX_BLOCK_SIGOPS)
            return DoS(100, error("ConnectBlock() : too many sigops"));

        CDiskTxPos posThisTx(pindex->nFile, pindex->nBlockPos, nTxPos);
        if (!fJustCheck)
            nTxPos += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);

        if (tx.IsCoinBase())
            nValueOut += tx.GetValueOut();
        else
        {
            bool fInvalid;
            if (!tx.FetchInputs(txdb, pegdb,
                                mapQueuedChanges, mapQueuedFractionsChanges,
                                true, false,
                                mapInputs[i], mapInputsFractions[i],
                                fInvalid))
                return false;

            // Add in sigops done by pay-to-script-hash inputs;
            // this is to prevent a "rogue miner" from creating
            // an incredibly-expensive-to-validate block.
            nSigOps += GetP2SHSigOpCount(tx, mapInputs[i]);
            if (nSigOps > MAX_BLOCK_SIGOPS)
                return DoS(100, error("ConnectBlock() : too many sigops"));

            int64_t nTxValueIn = tx.GetValueIn(mapInputs[i]);
            int64_t nTxValueOut = tx.GetValueOut();
            nValueIn += nTxValueIn;
            nValueOut += nTxValueOut;
            
            if (!tx.IsCoinStake())
                nFees += nTxValueIn - nTxValueOut;
            if (tx.IsCoinStake())
                nStakeReward = nTxValueOut - nTxValueIn;

            if (!tx.ConnectInputs(mapInputs[i], mapInputsFractions[i],
                                  mapQueuedChanges, mapQueuedFractionsChanges,
                                  feesFractions,
                                  posThisTx, pindex, true, false, flags))
                return false;
        }

        mapQueuedChanges[hashTx] = CTxIndex(posThisTx, tx.vout.size(), pindex->nHeight, i);
        
        if (!fUtxoDbEnabled) {
            mapInputs.clear();
            mapInputsFractions.clear();
        }
    }

    if (IsProofOfWork())
    {
        int64_t nReward = GetProofOfWorkReward(nFees);
        // Check coinbase reward
        if (vtx[0].GetValueOut() > nReward)
            return DoS(50, error("ConnectBlock() : coinbase reward exceeded (actual=%d vs calculated=%d)",
                   vtx[0].GetValueOut(),
                   nReward));
    }
    if (IsProofOfStake())
    {
        // ppcoin: coin stake tx earns reward instead of paying fee
        uint64_t nCoinAge;
        if (!vtx[1].GetCoinAge(txdb, pindex->pprev, nCoinAge))
            return error("ConnectBlock() : %s unable to get coin age for coinstake", vtx[1].GetHash().ToString());
        if (vtx[1].vin.size() == 0)
            return error("ConnectBlock() : no inputs for stake");
        const COutPoint & prevout = vtx[1].vin[0].prevout;
        
        bool fInvalid;
        if (!vtx[1].FetchInputs(txdb, pegdb,
                                mapQueuedChanges, mapQueuedFractionsChanges,
                                true, false,
                                mapInputs[1], mapInputsFractions[1],
                                fInvalid))
            return false;
        
        auto fkey = uint320(prevout.hash, prevout.n);
        if (mapInputsFractions[1].find(fkey) == mapInputsFractions[1].end()) {
            return error("ConnectBlock() : no input fractions found");
        }
        int64_t nCalculatedStakeReward = GetProofOfStakeReward(
                    pindex->pprev, nCoinAge, nFees, mapInputsFractions[1][fkey]);
        int64_t nCalculatedStakeRewardWithoutFee = GetProofOfStakeReward(
                    pindex->pprev, nCoinAge, 0 /*nFees*/, mapInputsFractions[1][fkey]);

        if (nStakeReward > nCalculatedStakeReward)
            return DoS(100, error("ConnectBlock() : coinstake pays too much(actual=%d vs calculated=%d)", nStakeReward, nCalculatedStakeReward));

        if (pindex->nHeight >= nPegStartHeight) {
            string sPegFailCause;
            if (!CalculateStakingFractions(vtx[1], pindex,
                                           mapInputs[1], mapInputsFractions[1],
                                           mapQueuedChanges, mapQueuedFractionsChanges,
                                           feesFractions, nCalculatedStakeRewardWithoutFee,
                                           sPegFailCause)) {
                return DoS(100, error("ConnectBlock() : fail on calculations of stake fractions (cause=%s)", sPegFailCause.c_str()));
            }
        }
        if (!fUtxoDbEnabled) {
            mapInputs.clear();
            mapInputsFractions.clear();
        }
    }
    
    // ppcoin: track money supply and mint amount info
    pindex->nMint = nValueOut - nValueIn + nFees;
    pindex->nMoneySupply = (pindex->pprev? pindex->pprev->nMoneySupply : 0) + nValueOut - nValueIn;
    // peg voting information
    if (!CalculateBlockPegVotes(*this, pindex, pegdb))
        throw std::runtime_error("CBlock::ConnectBlock() : CalculateBlockPegVotes failed due to pegdb fail");

    if (!txdb.WriteBlockIndex(CDiskBlockIndex(pindex)))
        return error("Connect() : WriteBlockIndex for pindex failed");

    if (fJustCheck)
        return true;

    // fractions in and out are ready
    if (fUtxoDbEnabled) {
        for(size_t i=0; i< vtx.size(); i++)
        {
            CTransaction& tx = vtx[i];
            if (!tx.ConnectUtxo(txdb, pindex, i, mapInputs[i], mapInputsFractions[i], mapQueuedFractionsChanges)) {
                // report, but it is not cause to stop/reject for now
                error("ConnectBlock() : ConnectUtxo failed");
            }
        }
        if (pindex->nHeight >= nPegStartHeight) {
            if (!ProcessFrozenQueue(txdb, pegdb, mapQueuedFractionsChanges, pindex, false /*fLoading*/)) {
                // report, but it is not cause to stop/reject for now
                error("ConnectBlock() : ConnectFrozenQueue failed");
            }
        }
    }
    
    // Write queued txindex changes
    for (map<uint256, CTxIndex>::iterator mi = mapQueuedChanges.begin(); mi != mapQueuedChanges.end(); ++mi)
    {
        if (!txdb.UpdateTxIndex((*mi).first, (*mi).second))
            return error("ConnectBlock() : UpdateTxIndex failed");
    }

    // Write queued fractions changes
    for (MapFractions::iterator mi = mapQueuedFractionsChanges.begin(); mi != mapQueuedFractionsChanges.end(); ++mi)
    {
        if (!pegdb.WriteFractions((*mi).first, (*mi).second))
            return error("ConnectBlock() : pegdb Write failed");
    }
    
    bool fPegPruneEnabled = true;
    if (!pegdb.ReadPegPruneEnabled(fPegPruneEnabled)) {
        fPegPruneEnabled = true;
    }
    if (fPegPruneEnabled) {
        // Prune old spent fractions, back to index
        int nHeightPrune = pindex->nHeight-PEG_PRUNE_INTERVAL;
        if (nHeightPrune >0 && nHeightPrune >= nPegStartHeight) {
            auto pindexprune = pindex;
            while (pindexprune && pindexprune->nHeight > nHeightPrune)
                pindexprune = pindexprune->pprev;
            if (pindexprune) {
                CBlock blockprune;
                if (blockprune.ReadFromDisk(pindexprune->nFile, 
                                            pindexprune->nBlockPos, 
                                            true /*vtx*/)) {
                    PrunePegForBlock(blockprune, pegdb);
                }
            }
        }
    }
    
    // Update block index on disk without changing it in memory.
    // The memory index structure will be changed after the db commits.
    if (pindex->pprev)
    {
        CDiskBlockIndex blockindexPrev(pindex->pprev);
        blockindexPrev.hashNext = pindex->GetBlockHash();
        if (!txdb.WriteBlockIndex(blockindexPrev))
            return error("ConnectBlock() : WriteBlockIndex failed");
    }

    // Watch for transactions paying to me
    for(const CTransaction& tx : vtx) {
        SyncWithWallets(tx, this, true, mapQueuedFractionsChanges);
    }

    // Watch for peg activation transaction
    if (!fPegIsActivatedViaTx && pindex->nHeight % 10 == 0) {
        CTxIndex txindex;
        if (txdb.ReadTxIndex(Params().PegActivationTxhash(), txindex)) {
            LogPrintf("ConnectBlock() : peg activation tx is found\n");
            unsigned int nTxNum = 0;
            uint256 blockhash;
            int nTxHeight = txindex.GetHeightInMainChain(&nTxNum, Params().PegActivationTxhash(), &blockhash);
            LogPrintf("ConnectBlock() : peg activation tx has height: %d\n", nTxHeight);
            if (nTxHeight >0) {
                if (nTxHeight < pindex->nHeight - 100) {
                    LogPrintf("ConnectBlock() : peg activation tx is deep: %d\n", pindex->nHeight - nTxHeight);
                    int nPegToStart = ((nTxHeight+500)/1000 +1) * 1000; 
                    nPegStartHeight = nPegToStart;
                    fPegIsActivatedViaTx = true;
                    LogPrintf("ConnectBlock() : peg to start:%d\n", nPegToStart);
                    if (!txdb.WritePegStartHeight(nPegStartHeight))
                        return error("ConnectBlock() : peg start write failed (txdb)");
                    if (!pegdb.WritePegStartHeight(nPegStartHeight))
                        return error("ConnectBlock() : peg start write failed (pegdb)");
                    if (!pegdb.WritePegTxActivated(fPegIsActivatedViaTx))
                        return error("ConnectBlock() : peg txactivated write failed");
                    if (nPegStartHeight > nBestHeight) {
                        strMiscWarning = "Warning : Peg system has activation at block: "+std::to_string(nPegStartHeight);
                    }
                }
            }
        }
    }
    
    // Track of exchange txs
    for(const CTransaction& tx : vtx) {
        uint256 hashTx = tx.GetHash();
        uint256 wid;
        int nOut = -1;
        if (!tx.IsExchangeTx(nOut, wid)) continue;
        if (wid == 0) continue;
        if (!pegdb.WritePegTxId(wid, hashTx))
            return error("ConnectBlock() : peg txid write failed");
    }
    
    return true;
}

bool static Reorganize(CTxDB& txdb, CPegDB& pegdb, CBlockIndex* pindexNew)
{
    LogPrintf("REORGANIZE\n");

    // Find the fork
    CBlockIndex* pfork = pindexBest;
    CBlockIndex* plonger = pindexNew;
    while (pfork != plonger)
    {
        while (plonger->nHeight > pfork->nHeight)
            if (!(plonger = plonger->pprev))
                return error("Reorganize() : plonger->pprev is null");
        if (pfork == plonger)
            break;
        if (!(pfork = pfork->pprev))
            return error("Reorganize() : pfork->pprev is null");
    }

    // List of what to disconnect
    vector<CBlockIndex*> vDisconnect;
    for (CBlockIndex* pindex = pindexBest; pindex != pfork; pindex = pindex->pprev)
        vDisconnect.push_back(pindex);

    // List of what to connect
    vector<CBlockIndex*> vConnect;
    for (CBlockIndex* pindex = pindexNew; pindex != pfork; pindex = pindex->pprev)
        vConnect.push_back(pindex);
    reverse(vConnect.begin(), vConnect.end());

    LogPrintf("REORGANIZE: Disconnect %u blocks; %s..%s\n", vDisconnect.size(), pfork->GetBlockHash().ToString(), pindexBest->GetBlockHash().ToString());
    LogPrintf("REORGANIZE: Connect    %u blocks; %s..%s\n", vConnect.size(), pfork->GetBlockHash().ToString(), pindexNew->GetBlockHash().ToString());

    // Disconnect shorter branch
    list<CTransaction> vResurrect;
    for(CBlockIndex* pindex : vDisconnect)
    {
        CBlock block;
        if (!block.ReadFromDisk(pindex))
            return error("Reorganize() : ReadFromDisk for disconnect failed");
        if (!block.DisconnectBlock(txdb, pegdb, pindex))
            return error("Reorganize() : DisconnectBlock %s failed", pindex->GetBlockHash().ToString());

        // Queue memory transactions to resurrect.
        // We only do this for blocks after the last checkpoint (reorganisation before that
        // point should only happen with -reindex/-loadblock, or a misbehaving peer.
        for (size_t i = block.vtx.size(); i--;) {
            const CTransaction& tx = block.vtx.at(i);
            if (!(tx.IsCoinBase() || tx.IsCoinStake()) && pindex->nHeight > Checkpoints::GetTotalBlocksEstimate()) {
                vResurrect.push_front(tx);
            }
        }
    }

    // Connect longer branch
    vector<CTransaction> vDelete;
    for (unsigned int i = 0; i < vConnect.size(); i++)
    {
        CBlockIndex* pindex = vConnect[i];
        CBlock block;
        if (!block.ReadFromDisk(pindex))
            return error("Reorganize() : ReadFromDisk for connect failed");
        if (!block.ConnectBlock(txdb, pegdb, pindex))
        {
            // Invalid block
            return error("Reorganize() : ConnectBlock %s failed", pindex->GetBlockHash().ToString());
        }

        // Queue memory transactions to delete
        for(const CTransaction& tx : block.vtx) {
            vDelete.push_back(tx);
        }
    }
    if (!txdb.WriteHashBestChain(pindexNew->GetBlockHash()))
        return error("Reorganize() : WriteHashBestChain failed");

    // Make sure it's successfully written to disk before changing memory structure
    if (!txdb.TxnCommit())
        return error("Reorganize() : TxnCommit failed");

    // Disconnect shorter branch
    for(const CBlockIndex* pindex : vDisconnect) {
        if (pindex->pprev) {
            pindex->pprev->pnext = NULL;
        }
    }

    // Connect longer branch
    for(CBlockIndex* pindex : vConnect) {
        if (pindex->pprev) {
            pindex->pprev->pnext = pindex;
        }
    }

    // Resurrect memory transactions that were in the disconnected branch
    for(CTransaction& tx : vResurrect) {
        AcceptToMemoryPool(mempool, tx, false, NULL);
    }

    // Delete redundant memory transactions that are in the connected branch
    for(const CTransaction& tx : vDelete) {
        mempool.remove(tx);
        mempool.removeConflicts(tx);
    }

    LogPrintf("REORGANIZE: done\n");

    return true;
}


// Called from inside SetBestChain: attaches a block to the new best chain being built
bool CBlock::SetBestChainInner(CTxDB& txdb, CPegDB& pegdb, CBlockIndex *pindexNew)
{
    uint256 hash = GetHash();

    // Adding to current best branch
    if (!ConnectBlock(txdb, pegdb, pindexNew) || !txdb.WriteHashBestChain(hash))
    {
        txdb.TxnAbort();
        InvalidChainFound(pindexNew);
        return false;
    }
    if (!txdb.TxnCommit())
        return error("SetBestChain() : TxnCommit failed");

    // Add to current best branch
    pindexNew->pprev->pnext = pindexNew;

    // Delete redundant memory transactions
    for(const CTransaction& tx : vtx) {
        mempool.remove(tx);
    }

    return true;
}

bool CBlock::SetBestChain(CTxDB& txdb, CPegDB& pegdb, CBlockIndex* pindexNew)
{
    uint256 hash = GetHash();

    if (!txdb.TxnBegin())
        return error("SetBestChain() : TxnBegin failed");

    if (pindexGenesisBlock == NULL && hash == Params().HashGenesisBlock())
    {
        txdb.WriteHashBestChain(hash);
        if (!txdb.TxnCommit())
            return error("SetBestChain() : TxnCommit failed");
        pindexGenesisBlock = pindexNew;
    }
    else if (hashPrevBlock == hashBestChain)
    {
        if (!SetBestChainInner(txdb, pegdb, pindexNew))
            return error("SetBestChain() : SetBestChainInner failed");
    }
    else
    {
        // the first block in the new chain that will cause it to become the new best chain
        CBlockIndex *pindexIntermediate = pindexNew;

        // list of blocks that need to be connected afterwards
        std::vector<CBlockIndex*> vpindexSecondary;

        // Reorganize is costly in terms of db load, as it works in a single db transaction.
        // Try to limit how much needs to be done inside
        while (pindexIntermediate->pprev && pindexIntermediate->pprev->nChainTrust > pindexBest->nChainTrust)
        {
            vpindexSecondary.push_back(pindexIntermediate);
            pindexIntermediate = pindexIntermediate->pprev;
        }

        if (!vpindexSecondary.empty())
            LogPrintf("Postponing %u reconnects\n", vpindexSecondary.size());

        // Switch to new best branch
        if (!Reorganize(txdb, pegdb, pindexIntermediate))
        {
            txdb.TxnAbort();
            InvalidChainFound(pindexNew);
            return error("SetBestChain() : Reorganize failed");
        }

        // Connect further blocks
        for (size_t i = vpindexSecondary.size(); i--;)
        {
            CBlockIndex *pindex = vpindexSecondary[i];
            
            CBlock block;
            if (!block.ReadFromDisk(pindex))
            {
                LogPrintf("SetBestChain() : ReadFromDisk failed\n");
                break;
            }
            if (!txdb.TxnBegin()) {
                LogPrintf("SetBestChain() : TxnBegin 2 failed\n");
                break;
            }
            // errors now are not fatal, we still did a reorganisation to a new chain in a valid way
            if (!block.SetBestChainInner(txdb, pegdb, pindex))
                break;
        }
    }

    // Update best block in wallet (so we can detect restored wallets)
    bool fIsInitialDownload = IsInitialBlockDownload();
    int nPegInterval = Params().PegInterval(pindexNew->nHeight);
    if ((pindexNew->nHeight % 20160) == 0 || 
            (!fIsInitialDownload && (pindexNew->nHeight % 144) == 0) || 
            (!fIsInitialDownload && (pindexNew->nHeight % nPegInterval) == 0))
    {
        const CBlockLocator locator(pindexNew);
        g_signals.SetBestChain(locator);
    }

    // New best block
    hashBestChain = hash;
    pindexBest = pindexNew;
    pblockindexFBBHLast = NULL;
    nBestHeight = pindexBest->nHeight;
    nBestChainTrust = pindexNew->nChainTrust;
    nTimeBestReceived = GetTime();
    mempool.AddTransactionsUpdated(1);

    uint256 nBestBlockTrust = pindexBest->nHeight != 0 ? (pindexBest->nChainTrust - pindexBest->pprev->nChainTrust) : pindexBest->nChainTrust;

    LogPrintf("SetBestChain: new best=%s  height=%d  trust=%s  blocktrust=%d  date=%s\n",
      hashBestChain.ToString(), nBestHeight,
      CBigNum(nBestChainTrust).ToString(),
      nBestBlockTrust.GetLow64(),
      DateTimeStrFormat("%x %H:%M:%S", pindexBest->GetBlockTime()));

    // Check the version of the last 100 blocks to see if we need to upgrade:
    if (!fIsInitialDownload)
    {
        int nUpgraded = 0;
        const CBlockIndex* pindex = pindexBest;
        for (int i = 0; i < 100 && pindex != NULL; i++)
        {
            if (pindex->nVersion > CBlock::CURRENT_VERSION)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            LogPrintf("SetBestChain: %d of last 100 blocks above version %d\n", nUpgraded, (int)CBlock::CURRENT_VERSION);
        if (nUpgraded > 100/2)
            // strMiscWarning is read by GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
            strMiscWarning = _("Warning: This version is obsolete, upgrade required!");
        
        if (pindexBest->nHeight > nPegStartHeight) {
            if (pindexBest->nHeight % nPegInterval == 0) {
                mempool.reviewOnPegChange();
            }
        }
    }

    std::string strCmd = GetArg("-blocknotify", "");

    if (!fIsInitialDownload && !strCmd.empty())
    {
        boost::replace_all(strCmd, "%s", hashBestChain.GetHex());
        boost::thread t(runCommand, strCmd); // thread runs free
    }

    return true;
}

// ppcoin: total coin age spent in transaction, in the unit of coin-days.
// Only those coins meeting minimum age requirement counts. As those
// transactions not in main chain are not currently indexed so we
// might not find out about their coin age. Older transactions are
// guaranteed to be in main chain by sync-checkpoint. This rule is
// introduced to help nodes establish a consistent view of the coin
// age (trust score) of competing branches.
bool CTransaction::GetCoinAge(CTxDB& txdb, const CBlockIndex* pindexPrev, uint64_t& nCoinAge) const
{
    CBigNum bnCentSecond = 0;  // coin age in the unit of cent-seconds
    nCoinAge = 0;

    if (IsCoinBase())
        return true;

    for(const CTxIn& txin : vin)
    {
        // First try finding the previous transaction in database
        CTransaction txPrev;
        CTxIndex txindex;
        if (!txPrev.ReadFromDisk(txdb, txin.prevout, txindex))
            continue;  // previous transaction not in main chain
        if (nTime < txPrev.nTime)
            return false;  // Transaction timestamp violation

        if (IsProtocolV3(nTime))
        {
            int nSpendDepth;
            if (IsConfirmedInNPrevBlocks(txindex, pindexPrev, nStakeMinConfirmations - 1, nSpendDepth))
            {
                LogPrint("coinage", "coin age skip nSpendDepth=%d\n", nSpendDepth + 1);
                continue; // only count coins meeting min confirmations requirement
            }
        }
        else
        {
            // Read block header
            CBlock block;
            if (!block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false))
                return false; // unable to read block of previous transaction
            if (block.GetBlockTime() + nStakeMinAge > nTime)
                continue; // only count coins meeting min age requirement
        }

        int64_t nValueIn = txPrev.vout[txin.prevout.n].nValue;
        bnCentSecond += CBigNum(nValueIn) * (nTime-txPrev.nTime) / CENT;

        LogPrint("coinage", "coin age nValueIn=%d nTimeDiff=%d bnCentSecond=%s\n", nValueIn, nTime - txPrev.nTime, bnCentSecond.ToString());
    }

    CBigNum bnCoinDay = bnCentSecond * CENT / COIN / (24 * 60 * 60);
    LogPrint("coinage", "coin age bnCoinDay=%s\n", bnCoinDay.ToString());
    nCoinAge = bnCoinDay.getuint64();
    return true;
}

bool CBlock::AddToBlockIndex(unsigned int nFile, unsigned int nBlockPos, const uint256& hashProof)
{
    AssertLockHeld(cs_main);

    // Check for duplicate
    uint256 hash = GetHash();
    if (mapBlockIndex.count(hash))
        return error("AddToBlockIndex() : %s already exists", hash.ToString());

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(nFile, nBlockPos, *this);
    if (!pindexNew)
        return error("AddToBlockIndex() : new CBlockIndex failed");
    pindexNew->phashBlock = &hash;
    map<uint256, CBlockIndex*>::iterator miPrev = mapBlockIndex.find(hashPrevBlock);
    if (miPrev != mapBlockIndex.end())
    {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
    }

    // ppcoin: compute chain trust score
    pindexNew->nChainTrust = (pindexNew->pprev ? pindexNew->pprev->nChainTrust : 0) + pindexNew->GetBlockTrust();

    // ppcoin: compute stake entropy bit for stake modifier
    if (!pindexNew->SetStakeEntropyBit(GetStakeEntropyBit()))
        return error("AddToBlockIndex() : SetStakeEntropyBit() failed");

    // Record proof hash value
    pindexNew->hashProof = hashProof;

    // ppcoin: compute stake modifier
    uint64_t nStakeModifier = 0;
    bool fGeneratedStakeModifier = false;
    if (!ComputeNextStakeModifier(pindexNew->pprev, nStakeModifier, fGeneratedStakeModifier))
        return error("AddToBlockIndex() : ComputeNextStakeModifier() failed");
    
    pindexNew->SetStakeModifier(nStakeModifier, fGeneratedStakeModifier);
    pindexNew->bnStakeModifierV2 = ComputeStakeModifierV2(pindexNew->pprev, IsProofOfWork() ? hash : vtx[1].vin[0].prevout.hash);

    // Add to mapBlockIndex
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.insert(hash, pindexNew).first;
    if (pindexNew->IsProofOfStake())
        setStakeSeen.insert(make_pair(pindexNew->prevoutStake, pindexNew->nStakeTime));
    pindexNew->phashBlock = &((*mi).first);

    // Set peg properties of block
    pindexNew->SetPeg(pindexNew->nHeight >= nPegStartHeight);

    // Write to disk block index
    CTxDB txdb;
    if (!txdb.TxnBegin())
        return error("AddToBlockIndex() : txdb.TxnBegin() failed");
    txdb.WriteBlockIndex(CDiskBlockIndex(pindexNew));
    if (!txdb.TxnCommit())
        return error("AddToBlockIndex() : txdb.TxnCommit() failed");

    CPegDB pegdb;

    // New best
    bool hasBetterStakerTrust = false;
    bool hasBetterChainTrust = pindexNew->nChainTrust > nBestChainTrust;
    if (!hasBetterChainTrust) {
        // find out if better trust by stakers of new chain
        // 1. find fork point
        CBlockIndex* pfork = pindexBest;
        CBlockIndex* plonger = pindexNew;
        while (pfork != plonger)
        {
            while (plonger->nHeight > pfork->nHeight)
                if (!(plonger = plonger->pprev))
                    return error("AddToBlockIndex() : plonger->pprev is null");
            if (pfork == plonger)
                break;
            if (!(pfork = pfork->pprev))
                return error("AddToBlockIndex() : pfork->pprev is null");
        }
        int nDepthToFork = pindexNew->nHeight - pfork->nHeight;
        if (nDepthToFork > 20) {
            // 2. check stakers of new chain
            for (CBlockIndex* pindex = pindexNew; pindex != pfork; pindex = pindex->pprev) {
                CBlock block;
                if (!block.ReadFromDisk(pindex, true))
                    return error("ReadFromDisk() : block read failed");
                if (block.vtx.size() >1 && block.vtx[1].IsCoinStake()) {
                    CTransaction& tx = block.vtx[1];
                    if (tx.vin.size() >1) {
                        const CTxIn & txin = tx.vin[0];
                        // Read txindex
                        CTxIndex txindex;
                        if (!txdb.ReadTxIndex(txin.prevout.hash, txindex)) {
                            continue;
                        }
                        // Read txPrev
                        CTransaction txPrev;
                        if (!txPrev.ReadFromDisk(txindex.pos)) {
                            continue;
                        }
                        if (txPrev.vout.size() > txin.prevout.n) {
                            int nRequired;
                            txnouttype type;
                            vector<CTxDestination> vAddresses;
                            if (ExtractDestinations(txPrev.vout[txin.prevout.n].scriptPubKey, type, vAddresses, nRequired)) {
                                if (vAddresses.size()==1) {
                                    string sAddress = CBitcoinAddress(vAddresses.front()).ToString();
                                    if (Params().sTrustedStakers.count(sAddress)) {
                                        hasBetterStakerTrust = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (hasBetterChainTrust || hasBetterStakerTrust)
        if (!SetBestChain(txdb, pegdb, pindexNew))
            return error("AddToBlockIndex() : SetBestChain() failed");

    if (pindexNew == pindexBest)
    {
        // Notify UI to display prev block's coinbase if it was ours
        static uint256 hashPrevBestCoinBase;
        g_signals.UpdatedTransaction(hashPrevBestCoinBase);
        hashPrevBestCoinBase = vtx[0].GetHash();
    }

    return true;
}




bool CBlock::CheckBlock(bool fCheckPOW, bool fCheckMerkleRoot, bool fCheckSig) const
{
    // These are checks that are independent of context
    // that can be verified before saving an orphan block.

    // Size limits
    if (vtx.empty() || vtx.size() > MAX_BLOCK_SIZE || ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
        return DoS(100, error("CheckBlock() : size limits failed"));

    // Check proof of work matches claimed amount
    if (fCheckPOW && IsProofOfWork() && !CheckProofOfWork(GetPoWHash(), nBits))
        return DoS(50, error("CheckBlock() : proof of work failed"));

    // Check timestamp
    if (GetBlockTime() > FutureDriftV2(GetAdjustedTime()))
        return error("CheckBlock() : block timestamp too far in the future");

    // First transaction must be coinbase, the rest must not be
    if (vtx.empty() || !vtx[0].IsCoinBase())
        return DoS(100, error("CheckBlock() : first tx is not coinbase"));
    for (unsigned int i = 1; i < vtx.size(); i++)
        if (vtx[i].IsCoinBase())
            return DoS(100, error("CheckBlock() : more than one coinbase"));

    if (IsProofOfStake())
    {
        // Coinbase output should be empty if proof-of-stake block
        if (vtx[0].vout.size() != 1 || !vtx[0].vout[0].IsEmpty())
            return DoS(100, error("CheckBlock() : coinbase output not empty for proof-of-stake block"));

        // Second transaction must be coinstake, the rest must not be
        if (vtx.empty() || !vtx[1].IsCoinStake())
            return DoS(100, error("CheckBlock() : second tx is not coinstake"));
        for (unsigned int i = 2; i < vtx.size(); i++)
            if (vtx[i].IsCoinStake())
                return DoS(100, error("CheckBlock() : more than one coinstake"));
    }

    // Check proof-of-stake block signature
    if (fCheckSig && !CheckBlockSignature())
        return DoS(100, error("CheckBlock() : bad proof-of-stake block signature"));

    // Check transactions
    for(const CTransaction& tx : vtx)
    {
        if (!tx.CheckTransaction())
            return DoS(tx.nDoS, error("CheckBlock() : CheckTransaction failed"));

        // ppcoin: check transaction timestamp
        if (GetBlockTime() < (int64_t)tx.nTime)
            return DoS(50, error("CheckBlock() : block timestamp earlier than transaction timestamp"));
    }

    // Check for duplicate txids. This is caught by ConnectInputs(),
    // but catching it earlier avoids a potential DoS attack:
    set<uint256> uniqueTx;
    for(const CTransaction& tx : vtx)
    {
        uniqueTx.insert(tx.GetHash());
    }
    if (uniqueTx.size() != vtx.size())
        return DoS(100, error("CheckBlock() : duplicate transaction"));

    unsigned int nSigOps = 0;
    for(const CTransaction& tx : vtx)
    {
        nSigOps += GetLegacySigOpCount(tx);
    }
    if (nSigOps > MAX_BLOCK_SIGOPS)
        return DoS(100, error("CheckBlock() : out-of-bounds SigOpCount"));

    // Check merkle root
    if (fCheckMerkleRoot && hashMerkleRoot != BuildMerkleTree())
        return DoS(100, error("CheckBlock() : hashMerkleRoot mismatch"));


    return true;
}

bool CBlock::AcceptBlock()
{
    AssertLockHeld(cs_main);

    if (!IsProtocolV3(nTime)) {
        if (nVersion > CURRENT_VERSION)
            return DoS(100, error("AcceptBlock() : reject unknown block version %d", nVersion));
    }

    // Check for duplicate
    uint256 hash = GetHash();
    if (mapBlockIndex.count(hash))
        return error("AcceptBlock() : block already in mapBlockIndex");

    // Get prev block index
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashPrevBlock);
    if (mi == mapBlockIndex.end())
        return DoS(10, error("AcceptBlock() : prev block not found"));
    CBlockIndex* pindexPrev = (*mi).second;
    int nHeight = pindexPrev->nHeight+1;

    // For reorg, check that it is not too deep, reject too old chain forks
    int nMaxReorgDepth = GetArg("-maxreorg", Params().MaxReorganizationDepth());
    if (nBestHeight - nHeight >= nMaxReorgDepth)
        return error("AcceptBlock(): forked chain older than max reorganization depth (height %d)", nHeight);
    
    if (IsProtocolV2(nHeight) && nVersion < 7)
        return DoS(100, error("AcceptBlock() : reject too old nVersion = %d", nVersion));
    else if (!IsProtocolV2(nHeight) && nVersion > 6)
        return DoS(100, error("AcceptBlock() : reject too new nVersion = %d", nVersion));

    if (IsProofOfWork() && nHeight > LAST_POW_TIME)
        return DoS(100, error("AcceptBlock() : reject proof-of-work at height %d", nHeight));

    // Check coinbase timestamp
    if (GetBlockTime() > FutureDrift((int64_t)vtx[0].nTime, nHeight))
        return DoS(50, error("AcceptBlock() : coinbase timestamp is too early"));

    // Check coinstake timestamp
    if (IsProofOfStake() && !CheckCoinStakeTimestamp(nHeight, GetBlockTime(), (int64_t)vtx[1].nTime))
        return DoS(50, error("AcceptBlock() : coinstake timestamp violation nTimeBlock=%d nTimeTx=%u", GetBlockTime(), vtx[1].nTime));

    // Check proof-of-work or proof-of-stake
    if (nBits != GetNextTargetRequired(pindexPrev, IsProofOfStake()))
        return DoS(100, error("AcceptBlock() : incorrect %s", IsProofOfWork() ? "proof-of-work" : "proof-of-stake"));

    // Check timestamp against prev
    if (GetBlockTime() <= pindexPrev->GetPastTimeLimit() || FutureDrift(GetBlockTime(), nHeight) < pindexPrev->GetBlockTime())
        return error("AcceptBlock() : block's timestamp is too early");

    // Check that all transactions are finalized
    for(const CTransaction& tx : vtx) {
        if (!IsFinalTx(tx, nHeight, GetBlockTime()))
            return DoS(10, error("AcceptBlock() : contains a non-final transaction"));
    }

    // Check that the block chain matches the known block chain up to a checkpoint
    if (!Checkpoints::CheckHardened(nHeight, hash))
        return DoS(100, error("AcceptBlock() : rejected by hardened checkpoint lock-in at %d", nHeight));

    uint256 hashProof;
    // Verify hash target and signature of coinstake tx
    if (IsProofOfStake())
    {
        uint256 targetProofOfStake;
        if (!CheckProofOfStake(pindexPrev, vtx[1], nBits, hashProof, targetProofOfStake))
        {
            return error("AcceptBlock() : check proof-of-stake failed for block %s", hash.ToString());
        }
    }
    // PoW is checked in CheckBlock()
    if (IsProofOfWork())
    {
        hashProof = GetPoWHash();
    }

    // Check that the block satisfies synchronized checkpoint
    if (!Checkpoints::CheckSync(nHeight))
        return error("AcceptBlock() : rejected by synchronized checkpoint");

    // Enforce rule that the coinbase starts with serialized block height
    CScript expect = CScript() << nHeight;
    if (vtx[0].vin[0].scriptSig.size() < expect.size() ||
        !std::equal(expect.begin(), expect.end(), vtx[0].vin[0].scriptSig.begin()))
        return DoS(100, error("AcceptBlock() : block height mismatch in coinbase"));

    // Write block to history file
    if (!CheckDiskSpace(::GetSerializeSize(*this, SER_DISK, CLIENT_VERSION)))
        return error("AcceptBlock() : out of disk space");
    
    unsigned int nFile = -1;
    unsigned int nBlockPos = 0;
    if (!WriteToDisk(nFile, nBlockPos))
        return error("AcceptBlock() : WriteToDisk failed");
    if (!AddToBlockIndex(nFile, nBlockPos, hashProof))
        return error("AcceptBlock() : AddToBlockIndex failed");

    // Relay inventory, but don't relay old inventory during initial block download
    int nBlockEstimate = Checkpoints::GetTotalBlocksEstimate();
    if (hashBestChain == hash)
    {
        LOCK(cs_vNodes);
        for(CNode* pnode : vNodes) {
            if (nBestHeight > (pnode->nStartingHeight != -1 ? pnode->nStartingHeight - 2000 : nBlockEstimate))
                pnode->PushInventory(CInv(MSG_BLOCK, hash));
        }
    }

    return true;
}

uint256 CBlockIndex::GetBlockTrust() const
{
    CBigNum bnTarget;
    bnTarget.SetCompact(nBits);

    if (bnTarget <= 0)
        return 0;

    return ((CBigNum(1)<<256) / (bnTarget+1)).getuint256();
}

bool CBlockIndex::IsSuperMajority(int minVersion, const CBlockIndex* pstart, unsigned int nRequired, unsigned int nToCheck)
{
    unsigned int nFound = 0;
    for (unsigned int i = 0; i < nToCheck && nFound < nRequired && pstart != NULL; i++)
    {
        if (pstart->nVersion >= minVersion)
            ++nFound;
        pstart = pstart->pprev;
    }
    return (nFound >= nRequired);
}

void PushGetBlocks(CNode* pnode, CBlockIndex* pindexBegin, uint256 hashEnd)
{
    // Filter out duplicate requests
    if (pindexBegin == pnode->pindexLastGetBlocksBegin && hashEnd == pnode->hashLastGetBlocksEnd)
        return;
    pnode->pindexLastGetBlocksBegin = pindexBegin;
    pnode->hashLastGetBlocksEnd = hashEnd;

    pnode->PushMessage("getblocks", CBlockLocator(pindexBegin), hashEnd);
}

bool static ReserealizeBlockSignature(CBlock* pblock)
{
    if (pblock->IsProofOfWork()) {
        pblock->vchBlockSig.clear();
        return true;
    }

    return CKey::ReserealizeSignature(pblock->vchBlockSig);
}

bool static IsCanonicalBlockSignature(CBlock* pblock)
{
    if (pblock->IsProofOfWork()) {
        return pblock->vchBlockSig.empty();
    }

    return IsDERSignature(pblock->vchBlockSig, false);
}

bool ProcessBlock(CNode* pfrom, CBlock* pblock)
{
    AssertLockHeld(cs_main);

    // Check for duplicate
    uint256 hash = pblock->GetHash();
    if (mapBlockIndex.count(hash))
        return error("ProcessBlock() : already have block %d %s", mapBlockIndex.ref(hash)->nHeight, hash.ToString());
    if (mapOrphanBlocks.count(hash))
        return error("ProcessBlock() : already have block (orphan) %s", hash.ToString());

    // ppcoin: check proof-of-stake
    // Limited duplicity on stake: prevents block flood attack
    // Duplicate stake allowed only when there is orphan child block
    if (!fReindex && !fImporting && pblock->IsProofOfStake() && setStakeSeen.count(pblock->GetProofOfStake()) && !mapOrphanBlocksByPrev.count(hash))
        return error("ProcessBlock() : duplicate proof-of-stake (%s, %d) for block %s", pblock->GetProofOfStake().first.ToString(), pblock->GetProofOfStake().second, hash.ToString());

    if (pblock->hashPrevBlock != hashBestChain)
    {
        // Extra checks to prevent "fill up memory by spamming with bogus blocks"
        const CBlockIndex* pcheckpoint = Checkpoints::AutoSelectSyncCheckpoint();
        int64_t deltaTime = pblock->GetBlockTime() - pcheckpoint->nTime;
        if (deltaTime < 0)
        {
            if (pfrom)
                pfrom->Misbehaving(1);
            return error("ProcessBlock() : block %s with timestamp before last checkpoint "
                         "(delta: %d, blocktime: %d, checkpointtime: %d)",
                         pblock->GetHash().ToString() ,deltaTime, pblock->GetBlockTime(), pcheckpoint->nTime);
        }
    }

    // Block signature can be malleated in such a way that it increases block size up to maximum allowed by protocol
    if (!IsCanonicalBlockSignature(pblock)) {
        if (pfrom && pfrom->nVersion >= CANONICAL_BLOCK_SIG_VERSION) {
            pfrom->Misbehaving(100);
            return error("ProcessBlock(): bad block signature encoding");
        } else if (!ReserealizeBlockSignature(pblock)) {
            LogPrintf("WARNING: ProcessBlock() : ReserealizeBlockSignature FAILED\n");
        }
    }

    // Preliminary checks
    if (!pblock->CheckBlock())
        return error("ProcessBlock() : CheckBlock FAILED");

    // If we don't already have its previous block, shunt it off to holding area until we get it
    if (!mapBlockIndex.count(pblock->hashPrevBlock))
    {
        LogPrintf("ProcessBlock: ORPHAN BLOCK %lu, prev=%s\n", (unsigned long)mapOrphanBlocks.size(), pblock->hashPrevBlock.ToString());

        // Accept orphans as long as there is a node to request its parents from
        if (pfrom) {
            // ppcoin: check proof-of-stake
            if (pblock->IsProofOfStake())
            {
                // Limited duplicity on stake: prevents block flood attack
                // Duplicate stake allowed only when there is orphan child block
                if (setStakeSeenOrphan.count(pblock->GetProofOfStake()) && !mapOrphanBlocksByPrev.count(hash))
                    return error("ProcessBlock() : duplicate proof-of-stake (%s, %d) for orphan block %s", pblock->GetProofOfStake().first.ToString(), pblock->GetProofOfStake().second, hash.ToString());
            }
            PruneOrphanBlocks();
            COrphanBlock* pblock2 = new COrphanBlock();
            {
                CDataStream ss(SER_DISK, CLIENT_VERSION);
                ss << *pblock;
                pblock2->vchBlock = std::vector<unsigned char>(ss.begin(), ss.end());
            }
            pblock2->hashBlock = hash;
            pblock2->hashPrev = pblock->hashPrevBlock;
            pblock2->stake = pblock->GetProofOfStake();
            mapOrphanBlocks.insert(make_pair(hash, pblock2));
            mapOrphanBlocksByPrev.insert(make_pair(pblock2->hashPrev, pblock2));
            if (pblock->IsProofOfStake())
                setStakeSeenOrphan.insert(pblock->GetProofOfStake());

            // Ask this guy to fill in what we're missing
            PushGetBlocks(pfrom, pindexBest, GetOrphanRoot(hash));
            // ppcoin: getblocks may not obtain the ancestor block rejected
            // earlier by duplicate-stake check so we ask for it again directly
            if (!IsInitialBlockDownload())
                pfrom->AskFor(CInv(MSG_BLOCK, WantedByOrphan(pblock2)));
        }
        return true;
    }

    // Store to disk
    if (!pblock->AcceptBlock())
        return error("ProcessBlock() : AcceptBlock FAILED");

    // Recursively process any orphan blocks that depended on this one
    vector<uint256> vWorkQueue;
    vWorkQueue.push_back(hash);
    for (unsigned int i = 0; i < vWorkQueue.size(); i++)
    {
        uint256 hashPrev = vWorkQueue[i];
        for (multimap<uint256, COrphanBlock*>::iterator mi = mapOrphanBlocksByPrev.lower_bound(hashPrev);
             mi != mapOrphanBlocksByPrev.upper_bound(hashPrev);
             ++mi)
        {
            CBlock block;
            {
                CDataStream ss(mi->second->vchBlock, SER_DISK, CLIENT_VERSION);
                ss >> block;
            }
            block.BuildMerkleTree();
            if (block.AcceptBlock())
                vWorkQueue.push_back(mi->second->hashBlock);
            mapOrphanBlocks.erase(mi->second->hashBlock);
            setStakeSeenOrphan.erase(block.GetProofOfStake());
            delete mi->second;
        }
        mapOrphanBlocksByPrev.erase(hashPrev);
    }

    LogPrintf("ProcessBlock: ACCEPTED\n");

    return true;
}

#ifdef ENABLE_WALLET
// novacoin: attempt to generate suitable proof-of-stake
bool CBlock::SignBlock(CWallet& wallet, int64_t nFees)
{
    // if we are trying to sign
    //    something except proof-of-stake block template
    if (!vtx[0].vout[0].IsEmpty())
        return false;

    // if we are trying to sign
    //    a complete proof-of-stake block
    if (IsProofOfStake())
        return true;

    static int64_t nLastCoinStakeSearchTime = GetAdjustedTime(); // startup timestamp

    CKey key;
    CTransaction txCoinStake;
    if (IsProtocolV2(nBestHeight+1))
        txCoinStake.nTime &= ~STAKE_TIMESTAMP_MASK;

    CTransaction txConsolidate;
    
    int64_t nSearchTime = txCoinStake.nTime; // search to current time

    if (nSearchTime > nLastCoinStakeSearchTime)
    {
        int64_t nSearchInterval = IsProtocolV2(nBestHeight+1) ? 1 : nSearchTime - nLastCoinStakeSearchTime;
        if (wallet.CreateCoinStake(wallet, nBits, nSearchInterval, nFees, txCoinStake, txConsolidate, key, wallet.GetPegVoteType()))
        {
            if (txCoinStake.nTime >= pindexBest->GetPastTimeLimit()+1)
            {
                // make sure coinstake would meet timestamp protocol
                //    as it would be the same as the block timestamp
                vtx[0].nTime = nTime = txCoinStake.nTime;

                // we have to make sure that we have no future timestamps in
                //    our transactions set
                for (vector<CTransaction>::iterator it = vtx.begin(); it != vtx.end();)
                    if (it->nTime > nTime) { it = vtx.erase(it); } else { ++it; }

                vtx.insert(vtx.begin() + 1, txCoinStake);
                
                if (!txConsolidate.vin.empty()) {
                    vtx.insert(vtx.begin() + 2, txConsolidate);
                }
                
                hashMerkleRoot = BuildMerkleTree();

                // append a signature to our block
                return key.Sign(GetHash(), vchBlockSig);
            }
        }
        nLastCoinStakeSearchInterval = nSearchTime - nLastCoinStakeSearchTime;
        nLastCoinStakeSearchTime = nSearchTime;
    }

    return false;
}
#endif

bool CBlock::CheckBlockSignature() const
{
    if (IsProofOfWork())
        return vchBlockSig.empty();

    if (vchBlockSig.empty())
        return false;

    vector<valtype> vSolutions;
    txnouttype whichType;

    const CTxOut& txout = vtx[1].vout[1];

    if (!Solver(txout.scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == TX_PUBKEY)
    {
        valtype& vchPubKey = vSolutions[0];
        return CPubKey(vchPubKey).Verify(GetHash(), vchBlockSig);
    }
    else if (IsProtocolV3(nTime))
    {
        // Block signing key also can be encoded in the nonspendable output
        // This allows to not pollute UTXO set with useless outputs e.g. in case of multisig staking

        const CScript& script = txout.scriptPubKey;
        CScript::const_iterator pc = script.begin();
        opcodetype opcode;
        valtype vchPushValue;

        if (!script.GetOp(pc, opcode, vchPushValue))
            return false;
        if (opcode != OP_RETURN)
            return false;
        if (!script.GetOp(pc, opcode, vchPushValue))
            return false;
        if (!IsCompressedOrUncompressedPubKey(vchPushValue))
            return false;
        return CPubKey(vchPushValue).Verify(GetHash(), vchBlockSig);
    }

    return false;
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = filesystem::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
    {
        string strMessage = _("Error: Disk space is low!");
        strMiscWarning = strMessage;
        LogPrintf("*** %s\n", strMessage);
        uiInterface.ThreadSafeMessageBox(strMessage, "", CClientUIInterface::MSG_ERROR);
        StartShutdown();
        return false;
    }
    return true;
}

static filesystem::path BlockFilePath(unsigned int nFile)
{
    string strBlockFn = strprintf("blk%04u.dat", nFile);
    return GetDataDir() / strBlockFn;
}

FILE* OpenBlockFile(unsigned int nFile, unsigned int nBlockPos, const char* pszMode)
{
    if ((nFile < 1) || (nFile == (unsigned int) -1))
        return NULL;
    FILE* file = fopen(BlockFilePath(nFile).string().c_str(), pszMode);
    if (!file)
        return NULL;
    if (nBlockPos != 0 && !strchr(pszMode, 'a') && !strchr(pszMode, 'w'))
    {
        if (fseek(file, nBlockPos, SEEK_SET) != 0)
        {
            fclose(file);
            return NULL;
        }
    }
    return file;
}

static unsigned int nCurrentBlockFile = 1;

FILE* AppendBlockFile(unsigned int& nFileRet)
{
    nFileRet = 0;
    while (true)
    {
        FILE* file = OpenBlockFile(nCurrentBlockFile, 0, "ab");
        if (!file)
            return NULL;
        if (fseek(file, 0, SEEK_END) != 0)
            return NULL;
        // FAT32 file size max 4GB, fseek and ftell max 2GB, so we must stay under 2GB
        if (ftell(file) < (long)(0x7F000000 - MAX_SIZE))
        {
            nFileRet = nCurrentBlockFile;
            return file;
        }
        fclose(file);
        nCurrentBlockFile++;
    }
}

bool LoadBlockIndex(LoadMsg load_msg, bool fAllowNew)
{
    LOCK(cs_main);

    if (TestNet())
    {
        nStakeMinConfirmations = 10;
        nCoinbaseMaturity = 10; // testnet maturity is 10 blocks
    }

    //
    // Load block index
    //
    CTxDB txdb("cr+");
    {
        // ensure pegdb is created
        bool fPegTxActivated = false;
        int nPegStartHeightStored = 0;
        {
            CPegDB pegdb("cr+");
            if (!pegdb.TxnBegin())
                return error("LoadBlockIndex() : peg TxnBegin failed");
            if (!pegdb.TxnCommit())
                return error("LoadBlockIndex() : peg TxnCommit failed");
            pegdb.ReadPegTxActivated(fPegTxActivated);
            pegdb.ReadPegStartHeight(nPegStartHeightStored);
            pegdb.Close();
        }
        // and peg start matches
        if (!fPegTxActivated && (
                nPegStartHeightStored != nPegStartHeight)) {
            // remove previous db
            {
                boost::filesystem::remove_all(GetDataDir() / "pegleveldb");
            }
            // recreate
            CPegDB pegdb("cr+");
            if (!pegdb.TxnBegin())
                return error("LoadBlockIndex() : peg TxnBegin failed");
            if (!pegdb.TxnCommit())
                return error("LoadBlockIndex() : peg TxnCommit failed");
        }
    }
    if (!txdb.LoadBlockIndex(load_msg))
        return false;
    
    CPegDB pegdb("cr+");
    if (!pegdb.LoadPegData(txdb, load_msg))
        return false;

    // pegdb to be ready for utxo db build
    if (!txdb.LoadUtxoData(load_msg))
        return false;
    
    //
    // Init with genesis block
    //
    if (mapBlockIndex.empty())
    {
        if (!fAllowNew)
            return false;

        CBlock &block = const_cast<CBlock&>(Params().GenesisBlock());
        // Start new block file
        unsigned int nFile;
        unsigned int nBlockPos;
        if (!block.WriteToDisk(nFile, nBlockPos))
            return error("LoadBlockIndex() : writing genesis block to disk failed");
        if (!block.AddToBlockIndex(nFile, nBlockPos, Params().HashGenesisBlock()))
            return error("LoadBlockIndex() : genesis block not accepted");
    }

    return true;
}



void PrintBlockTree()
{
    AssertLockHeld(cs_main);
    // pre-compute tree structure
    map<CBlockIndex*, vector<CBlockIndex*> > mapNext;
    for (map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.begin(); mi != mapBlockIndex.end(); ++mi)
    {
        CBlockIndex* pindex = (*mi).second;
        mapNext[pindex->pprev].push_back(pindex);
        // test
        //while (rand() % 3 == 0)
        //    mapNext[pindex->pprev].push_back(pindex);
    }

    vector<pair<int, CBlockIndex*> > vStack;
    vStack.push_back(make_pair(0, pindexGenesisBlock));

    int nPrevCol = 0;
    while (!vStack.empty())
    {
        int nCol = vStack.back().first;
        CBlockIndex* pindex = vStack.back().second;
        vStack.pop_back();

        // print split or gap
        if (nCol > nPrevCol)
        {
            for (int i = 0; i < nCol-1; i++)
                LogPrintf("| ");
            LogPrintf("|\\\n");
        }
        else if (nCol < nPrevCol)
        {
            for (int i = 0; i < nCol; i++)
                LogPrintf("| ");
            LogPrintf("|\n");
       }
        nPrevCol = nCol;

        // print columns
        for (int i = 0; i < nCol; i++)
            LogPrintf("| ");

        // print item
        CBlock block;
        block.ReadFromDisk(pindex);
        LogPrintf("%d (%u,%u) %s  %08x  %s  mint %7s  tx %u",
            pindex->nHeight,
            pindex->nFile,
            pindex->nBlockPos,
            block.GetHash().ToString(),
            block.nBits,
            DateTimeStrFormat("%x %H:%M:%S", block.GetBlockTime()),
            FormatMoney(pindex->nMint),
            block.vtx.size());

        // put the main time-chain first
        vector<CBlockIndex*>& vNext = mapNext[pindex];
        for (unsigned int i = 0; i < vNext.size(); i++)
        {
            if (vNext[i]->pnext)
            {
                swap(vNext[0], vNext[i]);
                break;
            }
        }

        // iterate children
        for (unsigned int i = 0; i < vNext.size(); i++)
            vStack.push_back(make_pair(nCol+i, vNext[i]));
    }
}

bool LoadExternalBlockFile(FILE* fileIn)
{
    int64_t nStart = GetTimeMillis();

    int nLoaded = 0;
    {
        try {
            CAutoFile blkdat(fileIn, SER_DISK, CLIENT_VERSION);
            unsigned int nPos = 0;
            while (nPos != (unsigned int)-1 && blkdat.good())
            {
                boost::this_thread::interruption_point();
                unsigned char pchData[65536];
                do {
                    fseek(blkdat, nPos, SEEK_SET);
                    int nRead = fread(pchData, 1, sizeof(pchData), blkdat);
                    if (nRead <= 8)
                    {
                        nPos = (unsigned int)-1;
                        break;
                    }
                    void* nFind = memchr(pchData, Params().MessageStart()[0], nRead+1-MESSAGE_START_SIZE);
                    if (nFind)
                    {
                        if (memcmp(nFind, Params().MessageStart(), MESSAGE_START_SIZE)==0)
                        {
                            nPos += ((unsigned char*)nFind - pchData) + MESSAGE_START_SIZE;
                            break;
                        }
                        nPos += ((unsigned char*)nFind - pchData) + 1;
                    }
                    else
                        nPos += sizeof(pchData) - MESSAGE_START_SIZE + 1;
                    boost::this_thread::interruption_point();
                } while(true);
                if (nPos == (unsigned int)-1)
                    break;
                fseek(blkdat, nPos, SEEK_SET);
                unsigned int nSize;
                blkdat >> nSize;
                if (nSize > 0 && nSize <= MAX_BLOCK_SIZE)
                {
                    CBlock block;
                    blkdat >> block;
                    LOCK(cs_main);
                    if (ProcessBlock(NULL,&block))
                    {
                        nLoaded++;
                        nPos += 4 + nSize;
                    }
                }
            }
        }
        catch (std::exception &e) {
            LogPrintf("%s() : Deserialize or I/O error caught during load\n",
                   __PRETTY_FUNCTION__);
        }
    }
    LogPrintf("Loaded %i blocks from external file in %dms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

struct CImportingNow
{
    CImportingNow() {
        assert(fImporting == false);
        fImporting = true;
    }

    ~CImportingNow() {
        assert(fImporting == true);
        fImporting = false;
    }
};

void ThreadImport(std::vector<boost::filesystem::path> vImportFiles)
{
    RenameThread("bitbay-loadblk");

    CImportingNow imp;

    // -loadblock=
    for(boost::filesystem::path &path : vImportFiles) {
        FILE *file = fopen(path.string().c_str(), "rb");
        if (file)
            LoadExternalBlockFile(file);
    }

    // hardcoded $DATADIR/bootstrap.dat
    filesystem::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (filesystem::exists(pathBootstrap)) {
        FILE *file = fopen(pathBootstrap.string().c_str(), "rb");
        if (file) {
            filesystem::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            LoadExternalBlockFile(file);
            RenameOver(pathBootstrap, pathBootstrapOld);
        }
    }
}










//////////////////////////////////////////////////////////////////////////////
//
// CAlert
//

extern map<uint256, CAlert> mapAlerts;
extern CCriticalSection cs_mapAlerts;

string GetWarnings(string strFor)
{
    int nPriority = 0;
    string strStatusBar;
    string strRPC;

    if (GetBoolArg("-testsafemode", false))
        strRPC = "test";

    if (!CLIENT_VERSION_IS_RELEASE)
        strStatusBar = _("This is a pre-release test build - use at your own risk - do not use for mining or merchant applications");

    // Misc warnings like out of disk space and clock is wrong
    if (strMiscWarning != "")
    {
        nPriority = 1000;
        strStatusBar = strMiscWarning;
    }

    // Alerts
    {
        LOCK(cs_mapAlerts);
        for(std::pair<const uint256, CAlert> & item : mapAlerts)
        {
            const CAlert& alert = item.second;
            if (alert.AppliesToMe() && alert.nPriority > nPriority)
            {
                nPriority = alert.nPriority;
                strStatusBar = alert.strStatusBar;
                if (nPriority > 1000)
                    strRPC = strStatusBar;
            }
        }
    }

    if (strFor == "statusbar")
        return strStatusBar;
    else if (strFor == "rpc")
        return strRPC;
    assert(!"GetWarnings() : invalid parameter");
    return "error";
}








//////////////////////////////////////////////////////////////////////////////
//
// Messages
//


bool static AlreadyHave(CTxDB& txdb, const CInv& inv)
{
    switch (inv.type)
    {
    case MSG_TX:
        {
        bool txInMap = false;
        txInMap = mempool.exists(inv.hash);
        return txInMap ||
               mapOrphanTransactions.count(inv.hash) ||
               txdb.ContainsTx(inv.hash);
        }

    case MSG_BLOCK:
        return mapBlockIndex.count(inv.hash) ||
               mapOrphanBlocks.count(inv.hash);
    }
    // Don't know what it is, just say we already got one
    return true;
}




void static ProcessGetData(CNode* pfrom)
{
    std::deque<CInv>::iterator it = pfrom->vRecvGetData.begin();

    vector<CInv> vNotFound;

    LOCK(cs_main);

    while (it != pfrom->vRecvGetData.end()) {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        const CInv &inv = *it;
        {
            boost::this_thread::interruption_point();
            it++;

            if (inv.type == MSG_BLOCK)
            {
                // Send block from disk
                map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(inv.hash);
                if (mi != mapBlockIndex.end())
                {
                    CBlock block;
                    block.ReadFromDisk((*mi).second);
                    pfrom->PushMessage("block", block);

                    // Trigger them to send a getblocks request for the next batch of inventory
                    if (inv.hash == pfrom->hashContinue)
                    {
                        // Bypass PushInventory, this must send even if redundant,
                        // and we want it right after the last block so they don't
                        // wait for other stuff first.
                        vector<CInv> vInv;
                        vInv.push_back(CInv(MSG_BLOCK, hashBestChain));
                        pfrom->PushMessage("inv", vInv);
                        pfrom->hashContinue = 0;
                    }
                }
            }
            else if (inv.IsKnownType())
            {
                // Send stream from relay memory
                bool pushed = false;
                {
                    LOCK(cs_mapRelay);
                    map<CInv, CDataStream>::iterator mi = mapRelay.find(inv);
                    if (mi != mapRelay.end()) {
                        pfrom->PushMessage(inv.GetCommand(), (*mi).second);
                        pushed = true;
                    }
                }
                if (!pushed && inv.type == MSG_TX) {
                    CTransaction tx;
                    MapFractions mf;
                    if (mempool.lookup(inv.hash, tx, mf)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << tx;
                        pfrom->PushMessage("tx", ss);
                        pushed = true;
                    }
                }
                if (!pushed) {
                    vNotFound.push_back(inv);
                }
            }

            // Track requests for our stuff.
            g_signals.Inventory(inv.hash);

            if (inv.type == MSG_BLOCK /* || inv.type == MSG_FILTERED_BLOCK */)
                break;
        }
    }

    pfrom->vRecvGetData.erase(pfrom->vRecvGetData.begin(), it);

    if (!vNotFound.empty()) {
        // Let the peer know that we didn't find what it asked for, so it doesn't
        // have to wait around forever. Currently only SPV clients actually care
        // about this message: it's needed when they are recursively walking the
        // dependencies of relevant unconfirmed transactions. SPV clients want to
        // do that because they want to know about (and store and rebroadcast and
        // risk analyze) the dependencies of transactions relevant to them, without
        // having to download the entire memory pool.
        pfrom->PushMessage("notfound", vNotFound);
    }
}

bool static ProcessMessage(CNode* pfrom, string strCommand, CDataStream& vRecv, int64_t nTimeReceived)
{
    RandAddSeedPerfmon();
    LogPrint("net", "received: %s (%u bytes)\n", strCommand, vRecv.size());
    if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0)
    {
        LogPrintf("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    if (strCommand == "version")
    {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0)
        {
            pfrom->Misbehaving(1);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        vRecv >> pfrom->nVersion >> pfrom->nServices >> nTime >> addrMe;
        if (pfrom->nVersion < MIN_PEER_PROTO_VERSION)
        {
            // disconnect from peers older than this proto version
            LogPrintf("partner %s using obsolete version %i; disconnecting\n", pfrom->addr.ToString(), pfrom->nVersion);
            pfrom->fDisconnect = true;
            return false;
        }

        if (pfrom->nVersion == 10300)
            pfrom->nVersion = 300;
        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty())
            vRecv >> pfrom->strSubVer;
        if (!vRecv.empty())
            vRecv >> pfrom->nStartingHeight;

        if (boost::starts_with(pfrom->strSubVer, "/BitBay:2.")) {
            LogPrintf("partner %s using obsolete version %s; disconnecting\n", pfrom->addr.ToString(), pfrom->strSubVer);
            pfrom->fDisconnect = true;
            return false;
        }
        
        // Disconnect if we connected to ourself
        if (nNonce == nLocalHostNonce && nNonce > 1)
        {
            LogPrintf("connected to self at %s, disconnecting\n", pfrom->addr.ToString());
            pfrom->fDisconnect = true;
            return true;
        }

        pfrom->addrLocal = addrMe;
        if (pfrom->fInbound && addrMe.IsRoutable())
        {
            SeenLocal(addrMe);
        }

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            pfrom->PushVersion();

        pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);

        // Change version
        pfrom->PushMessage("verack");
        pfrom->ssSend.SetVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        if (!pfrom->fInbound)
        {
            // Advertise our address
            if (!fNoListen && !IsInitialBlockDownload())
            {
                CAddress addr = GetLocalAddress(&pfrom->addr);
                if (addr.IsRoutable())
                {
                    pfrom->PushAddress(addr);
                } else if (IsPeerAddrLocalGood(pfrom)) {
                    addr.SetIP(pfrom->addrLocal);
                    pfrom->PushAddress(addr);
                }
            }

            // Get recent addresses
            if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || addrman.size() < 1000)
            {
                pfrom->PushMessage("getaddr");
                pfrom->fGetAddr = true;
            }
            addrman.Good(pfrom->addr);
        } else {
            if (((CNetAddr)pfrom->addr) == (CNetAddr)addrFrom)
            {
                addrman.Add(addrFrom, addrFrom);
                addrman.Good(addrFrom);
            }
        }

        // Relay alerts
        {
            LOCK(cs_mapAlerts);
            for(std::pair<const uint256, CAlert> & item : mapAlerts) {
                item.second.RelayTo(pfrom);
            }
        }

        pfrom->fSuccessfullyConnected = true;

        LogPrintf("receive version message: version %d, blocks=%d, us=%s, them=%s, peer=%s\n", pfrom->nVersion, pfrom->nStartingHeight, addrMe.ToString(), addrFrom.ToString(), pfrom->addr.ToString());

        int64_t nTimeOffset = nTime - GetTime();
        pfrom->nTimeOffset = nTimeOffset;
        if (GetBoolArg("-synctime", true))
            AddTimeData(pfrom->addr, nTimeOffset);
    }


    else if (pfrom->nVersion == 0)
    {
        // Must have a version message before anything else
        pfrom->Misbehaving(1);
        return false;
    }


    else if (strCommand == "verack")
    {
        pfrom->SetRecvVersion(min(pfrom->nVersion, PROTOCOL_VERSION));
    }


    else if (strCommand == "addr")
    {
        vector<CAddress> vAddr;
        vRecv >> vAddr;

        // Don't want addr from older versions unless seeding
        if (pfrom->nVersion < CADDR_TIME_VERSION && addrman.size() > 1000)
            return true;
        if (vAddr.size() > 1000)
        {
            pfrom->Misbehaving(20);
            return error("message addr size() = %u", vAddr.size());
        }

        // Store the new addresses
        vector<CAddress> vAddrOk;
        int64_t nNow = GetAdjustedTime();
        int64_t nSince = nNow - 10 * 60;
        for(CAddress& addr : vAddr)
        {
            boost::this_thread::interruption_point();

            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable())
            {
                // Relay to a limited number of other nodes
                {
                    LOCK(cs_vNodes);
                    // Use deterministic randomness to send to the same nodes for 24 hours
                    // at a time so the setAddrKnowns of the chosen nodes prevent repeats
                    static uint256 hashSalt;
                    if (hashSalt == 0)
                        hashSalt = GetRandHash();
                    uint64_t hashAddr = addr.GetHash();
                    uint256 hashRand = hashSalt ^ (hashAddr<<32) ^ ((GetTime()+hashAddr)/(24*60*60));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    multimap<uint256, CNode*> mapMix;
                    for(CNode* pnode : vNodes)
                    {
                        if (pnode->nVersion < CADDR_TIME_VERSION)
                            continue;
                        unsigned int nPointer;
                        memcpy(&nPointer, &pnode, sizeof(nPointer));
                        uint256 hashKey = hashRand ^ nPointer;
                        hashKey = Hash(BEGIN(hashKey), END(hashKey));
                        mapMix.insert(make_pair(hashKey, pnode));
                    }
                    int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)
                    for (multimap<uint256, CNode*>::iterator mi = mapMix.begin(); mi != mapMix.end() && nRelayNodes-- > 0; ++mi)
                        ((*mi).second)->PushAddress(addr);
                }
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
        }
        addrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);
        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        if (pfrom->fOneShot)
            pfrom->fDisconnect = true;
    }

    else if (strCommand == "inv")
    {
        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            pfrom->Misbehaving(20);
            return error("message inv size() = %u", vInv.size());
        }

        // find last block in inv vector
        unsigned int nLastBlock = (unsigned int)(-1);
        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++) {
            if (vInv[vInv.size() - 1 - nInv].type == MSG_BLOCK) {
                nLastBlock = vInv.size() - 1 - nInv;
                break;
            }
        }

        LOCK(cs_main);
        CTxDB txdb("r");

        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++)
        {
            const CInv &inv = vInv[nInv];

            boost::this_thread::interruption_point();
            pfrom->AddInventoryKnown(inv);

            bool fAlreadyHave = AlreadyHave(txdb, inv);
            LogPrint("net", "  got inventory: %s  %s\n", inv.ToString(), fAlreadyHave ? "have" : "new");

            if (!fAlreadyHave) {
                if (!fImporting)
                    pfrom->AskFor(inv);
            } else if (inv.type == MSG_BLOCK && mapOrphanBlocks.count(inv.hash)) {
                PushGetBlocks(pfrom, pindexBest, GetOrphanRoot(inv.hash));
            } else if (nInv == nLastBlock) {
                // In case we are on a very long side-chain, it is possible that we already have
                // the last block in an inv bundle sent in response to getblocks. Try to detect
                // this situation and push another getblocks to continue.
                PushGetBlocks(pfrom, mapBlockIndex.ref(inv.hash), uint256(0));
                if (fDebug)
                    LogPrintf("force request: %s\n", inv.ToString());
            }

            // Track requests for our stuff
            g_signals.Inventory(inv.hash);
        }
    }


    else if (strCommand == "getdata")
    {
        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            pfrom->Misbehaving(20);
            return error("message getdata size() = %u", vInv.size());
        }

        if (fDebug || (vInv.size() != 1))
            LogPrint("net", "received getdata (%u invsz)\n", vInv.size());

        if ((fDebug && vInv.size() > 0) || (vInv.size() == 1))
            LogPrint("net", "received getdata for: %s\n", vInv[0].ToString());

        pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), vInv.begin(), vInv.end());
        ProcessGetData(pfrom);
    }


    else if (strCommand == "getblocks")
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        // Find the last block the caller has in the main chain
        CBlockIndex* pindex = locator.GetBlockIndex();

        // Send the rest of the chain
        if (pindex)
            pindex = pindex->pnext;
        int nLimit = 500;
        LogPrint("net", "getblocks %d to %s limit %d\n", (pindex ? pindex->nHeight : -1), hashStop.ToString(), nLimit);
        for (; pindex; pindex = pindex->pnext)
        {
            if (pindex->GetBlockHash() == hashStop)
            {
                LogPrint("net", "  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                break;
            }
            pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            if (--nLimit <= 0)
            {
                // When this block is requested, we'll send an inv that'll make them
                // getblocks the next batch of inventory.
                LogPrint("net", "  getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                pfrom->hashContinue = pindex->GetBlockHash();
                break;
            }
        }
    }

    else if (strCommand == "getheaders")
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        CBlockIndex* pindex = NULL;
        if (locator.IsNull())
        {
            // If locator is null, return the hashStop block
            map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashStop);
            if (mi == mapBlockIndex.end())
                return true;
            pindex = (*mi).second;
        }
        else
        {
            // Find the last block the caller has in the main chain
            pindex = locator.GetBlockIndex();
            if (pindex)
                pindex = pindex->pnext;
        }

        vector<CBlock> vHeaders;
        int nLimit = 2000;
        LogPrint("net", "getheaders %d to %s\n", (pindex ? pindex->nHeight : -1), hashStop.ToString());
        for (; pindex; pindex = pindex->pnext)
        {
            vHeaders.push_back(pindex->GetBlockHeader());
            if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                break;
        }
        pfrom->PushMessage("headers", vHeaders);
    }


    else if (strCommand == "tx")
    {
        vector<uint256> vWorkQueue;
        vector<uint256> vEraseQueue;
        CTransaction tx;
        vRecv >> tx;

        CInv inv(MSG_TX, tx.GetHash());
        pfrom->AddInventoryKnown(inv);

        LOCK(cs_main);

        bool fMissingInputs = false;

        mapAlreadyAskedFor.erase(inv);

        if (AcceptToMemoryPool(mempool, tx, true, &fMissingInputs))
        {
            RelayTransaction(tx, inv.hash);
            vWorkQueue.push_back(inv.hash);
            vEraseQueue.push_back(inv.hash);

            // Recursively process any orphan transactions that depended on this one
            for (unsigned int i = 0; i < vWorkQueue.size(); i++)
            {
                map<uint256, set<uint256> >::iterator itByPrev = mapOrphanTransactionsByPrev.find(vWorkQueue[i]);
                if (itByPrev == mapOrphanTransactionsByPrev.end())
                    continue;
                for (set<uint256>::iterator mi = itByPrev->second.begin();
                     mi != itByPrev->second.end();
                     ++mi)
                {
                    const uint256& orphanTxHash = *mi;
                    CTransaction& orphanTx = mapOrphanTransactions[orphanTxHash];
                    bool fMissingInputs2 = false;

                    if (AcceptToMemoryPool(mempool, orphanTx, true, &fMissingInputs2))
                    {
                        LogPrint("mempool", "   accepted orphan tx %s\n", orphanTxHash.ToString());
                        RelayTransaction(orphanTx, orphanTxHash);
                        vWorkQueue.push_back(orphanTxHash);
                        vEraseQueue.push_back(orphanTxHash);
                    }
                    else if (!fMissingInputs2)
                    {
                        // invalid or too-little-fee orphan
                        vEraseQueue.push_back(orphanTxHash);
                        LogPrint("mempool", "   removed orphan tx %s\n", orphanTxHash.ToString());
                    }
                }
            }

            for(uint256 hash : vEraseQueue) {
                EraseOrphanTx(hash);
            }
        }
        else if (fMissingInputs)
        {
            AddOrphanTx(tx);

            // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
            unsigned int nEvicted = LimitOrphanTxSize(MAX_ORPHAN_TRANSACTIONS);
            if (nEvicted > 0)
                LogPrint("mempool", "mapOrphan overflow, removed %u tx\n", nEvicted);
        }
        if (tx.nDoS) pfrom->Misbehaving(tx.nDoS);
    }


    else if (strCommand == "block" && !fImporting && !fReindex) // Ignore blocks received while importing
    {
        CBlock block;
        vRecv >> block;
        uint256 hashBlock = block.GetHash();

        LogPrint("net", "received block %s\n", hashBlock.ToString());

        CInv inv(MSG_BLOCK, hashBlock);
        pfrom->AddInventoryKnown(inv);

        LOCK(cs_main);

        if (ProcessBlock(pfrom, &block))
            mapAlreadyAskedFor.erase(inv);
        if (block.nDoS) pfrom->Misbehaving(block.nDoS);
    }


    // This asymmetric behavior for inbound and outbound connections was introduced
    // to prevent a fingerprinting attack: an attacker can send specific fake addresses
    // to users' AddrMan and later request them by sending getaddr messages.
    // Making users (which are behind NAT and can only make outgoing connections) ignore
    // getaddr message mitigates the attack.
    else if ((strCommand == "getaddr") && (pfrom->fInbound))
    {
        // Don't return addresses older than nCutOff timestamp
        int64_t nCutOff = GetTime() - (nNodeLifespan * 24 * 60 * 60);
        pfrom->vAddrToSend.clear();
        vector<CAddress> vAddr = addrman.GetAddr();
        for(const CAddress &addr : vAddr) {
            if(addr.nTime > nCutOff)
                pfrom->PushAddress(addr);
        }
    }


    else if (strCommand == "mempool")
    {
        LOCK(cs_main);

        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);
        vector<CInv> vInv;
        for (unsigned int i = 0; i < vtxid.size(); i++) {
            CInv inv(MSG_TX, vtxid[i]);
            vInv.push_back(inv);
            if (i == (MAX_INV_SZ - 1))
                    break;
        }
        if (vInv.size() > 0)
            pfrom->PushMessage("inv", vInv);
    }


    else if (strCommand == "ping")
    {
        if (pfrom->nVersion > BIP0031_VERSION)
        {
            uint64_t nonce = 0;
            vRecv >> nonce;
            // Echo the message back with the nonce. This allows for two useful features:
            //
            // 1) A remote node can quickly check if the connection is operational
            // 2) Remote nodes can measure the latency of the network thread. If this node
            //    is overloaded it won't respond to pings quickly and the remote node can
            //    avoid sending us more work, like chain download requests.
            //
            // The nonce stops the remote getting confused between different pings: without
            // it, if the remote node sends a ping once per second and this node takes 5
            // seconds to respond to each, the 5th ping the remote sends would appear to
            // return very quickly.
            pfrom->PushMessage("pong", nonce);
        }
    }


    else if (strCommand == "pong")
    {
        int64_t pingUsecEnd = nTimeReceived;
        uint64_t nonce = 0;
        size_t nAvail = vRecv.in_avail();
        bool bPingFinished = false;
        std::string sProblem;

        if (nAvail >= sizeof(nonce)) {
            vRecv >> nonce;

            // Only process pong message if there is an outstanding ping (old ping without nonce should never pong)
            if (pfrom->nPingNonceSent != 0) {
                if (nonce == pfrom->nPingNonceSent) {
                    // Matching pong received, this ping is no longer outstanding
                    bPingFinished = true;
                    int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
                    if (pingUsecTime > 0) {
                        // Successful ping time measurement, replace previous
                        pfrom->nPingUsecTime = pingUsecTime;
                    } else {
                        // This should never happen
                        sProblem = "Timing mishap";
                    }
                } else {
                    // Nonce mismatches are normal when pings are overlapping
                    sProblem = "Nonce mismatch";
                    if (nonce == 0) {
                        // This is most likely a bug in another implementation somewhere, cancel this ping
                        bPingFinished = true;
                        sProblem = "Nonce zero";
                    }
                }
            } else {
                sProblem = "Unsolicited pong without ping";
            }
        } else {
            // This is most likely a bug in another implementation somewhere, cancel this ping
            bPingFinished = true;
            sProblem = "Short payload";
        }

        if (!(sProblem.empty())) {
            LogPrint("net", "pong %s %s: %s, %x expected, %x received, %zu bytes\n"
                , pfrom->addr.ToString()
                , pfrom->strSubVer
                , sProblem
                , pfrom->nPingNonceSent
                , nonce
                , nAvail);
        }
        if (bPingFinished) {
            pfrom->nPingNonceSent = 0;
        }
    }


    else if (strCommand == "alert")
    {
        CAlert alert;
        vRecv >> alert;

        uint256 alertHash = alert.GetHash();
        if (pfrom->setKnown.count(alertHash) == 0)
        {
            if (alert.ProcessAlert())
            {
                // Relay
                pfrom->setKnown.insert(alertHash);
                {
                    LOCK(cs_vNodes);
                    for(CNode* pnode : vNodes) {
                        alert.RelayTo(pnode);
                    }
                }
            }
            else {
                // Small DoS penalty so peers that send us lots of
                // duplicate/expired/invalid-signature/whatever alerts
                // eventually get banned.
                // This isn't a Misbehaving(100) (immediate ban) because the
                // peer might be an older or different implementation with
                // a different signature key, etc.
                pfrom->Misbehaving(10);
            }
        }
    }


    else
    {
        // Ignore unknown commands for extensibility
    }


    // Update the last seen time for this node's address
    if (pfrom->fNetworkNode)
        if (strCommand == "version" || strCommand == "addr" || strCommand == "inv" || strCommand == "getdata" || strCommand == "ping")
            AddressCurrentlyConnected(pfrom->addr);


    return true;
}

// requires LOCK(cs_vRecvMsg)
bool ProcessMessages(CNode* pfrom)
{
    //if (fDebug)
    //    LogPrintf("ProcessMessages(%zu messages)\n", pfrom->vRecvMsg.size());

    //
    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //
    bool fOk = true;

    if (!pfrom->vRecvGetData.empty())
        ProcessGetData(pfrom);

    // this maintains the order of responses
    if (!pfrom->vRecvGetData.empty()) return fOk;

    std::deque<CNetMessage>::iterator it = pfrom->vRecvMsg.begin();
    while (!pfrom->fDisconnect && it != pfrom->vRecvMsg.end()) {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        // get next message
        CNetMessage& msg = *it;

        //if (fDebug)
        //    LogPrintf("ProcessMessages(message %u msgsz, %zu bytes, complete:%s)\n",
        //            msg.hdr.nMessageSize, msg.vRecv.size(),
        //            msg.complete() ? "Y" : "N");

        // end, if an incomplete message is found
        if (!msg.complete())
            break;

        // at this point, any failure means we can delete the current message
        it++;

        // Scan for message start
        if (memcmp(msg.hdr.pchMessageStart, Params().MessageStart(), MESSAGE_START_SIZE) != 0) {
            LogPrintf("\n\nPROCESSMESSAGE: INVALID MESSAGESTART\n\n");
            fOk = false;
            break;
        }

        // Read header
        CMessageHeader& hdr = msg.hdr;
        if (!hdr.IsValid())
        {
            LogPrintf("\n\nPROCESSMESSAGE: ERRORS IN HEADER %s\n\n\n", hdr.GetCommand());
            continue;
        }
        string strCommand = hdr.GetCommand();

        // Message size
        unsigned int nMessageSize = hdr.nMessageSize;

        // Checksum
        CDataStream& vRecv = msg.vRecv;
        uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
        unsigned int nChecksum = 0;
        memcpy(&nChecksum, &hash, sizeof(nChecksum));
        if (nChecksum != hdr.nChecksum)
        {
            LogPrintf("ProcessMessages(%s, %u bytes) : CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n",
               strCommand, nMessageSize, nChecksum, hdr.nChecksum);
            continue;
        }

        // Process message
        bool fRet = false;
        try
        {
            fRet = ProcessMessage(pfrom, strCommand, vRecv, msg.nTime);
            boost::this_thread::interruption_point();
        }
        catch (std::ios_base::failure& e)
        {
            if (strstr(e.what(), "end of data"))
            {
                // Allow exceptions from under-length message on vRecv
                LogPrintf("ProcessMessages(%s, %u bytes) : Exception '%s' caught, normally caused by a message being shorter than its stated length\n", strCommand, nMessageSize, e.what());
            }
            else if (strstr(e.what(), "size too large"))
            {
                // Allow exceptions from over-long size
                LogPrintf("ProcessMessages(%s, %u bytes) : Exception '%s' caught\n", strCommand, nMessageSize, e.what());
            }
            else
            {
                PrintExceptionContinue(&e, "ProcessMessages()");
            }
        }
        catch (boost::thread_interrupted) {
            throw;
        }
        catch (std::exception& e) {
            PrintExceptionContinue(&e, "ProcessMessages()");
        } catch (...) {
            PrintExceptionContinue(NULL, "ProcessMessages()");
        }

        if (!fRet)
            LogPrintf("ProcessMessage(%s, %u bytes) FAILED\n", strCommand, nMessageSize);

        break;
    }

    // In case the connection got shut down, its receive buffer was wiped
    if (!pfrom->fDisconnect)
        pfrom->vRecvMsg.erase(pfrom->vRecvMsg.begin(), it);

    return fOk;
}


bool SendMessages(CNode* pto, bool fSendTrickle)
{
    TRY_LOCK(cs_main, lockMain);
    if (lockMain) {
        // Don't send anything until we get their version message
        if (pto->nVersion == 0)
            return true;

        //
        // Message: ping
        //
        bool pingSend = false;
        if (pto->fPingQueued) {
            // RPC ping request by user
            pingSend = true;
        }
        if (pto->nPingNonceSent == 0 && pto->nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros()) {
            // Ping automatically sent as a latency probe & keepalive.
            pingSend = true;
        }
        if (pingSend) {
            uint64_t nonce = 0;
            while (nonce == 0) {
                RAND_bytes((unsigned char*)&nonce, sizeof(nonce));
            }
            pto->fPingQueued = false;
            pto->nPingUsecStart = GetTimeMicros();
            if (pto->nVersion > BIP0031_VERSION) {
                pto->nPingNonceSent = nonce;
                pto->PushMessage("ping", nonce);
            } else {
                // Peer is too old to support ping command with nonce, pong will never arrive.
                pto->nPingNonceSent = 0;
                pto->PushMessage("ping");
            }
        }

        // Start block sync
        if (pto->fStartSync && !fImporting && !fReindex) {
            pto->fStartSync = false;
            PushGetBlocks(pto, pindexBest, uint256(0));
        }

        // Resend wallet transactions that haven't gotten in a block yet
        // Except during reindex, importing and IBD, when old wallet
        // transactions become unconfirmed and spams other nodes.
        if (!fReindex && !fImporting && !IsInitialBlockDownload())
        {
            ResendWalletTransactions();
        }

        // Address refresh broadcast
        static int64_t nLastRebroadcast;
        if (!IsInitialBlockDownload() && (GetTime() - nLastRebroadcast > 24 * 60 * 60))
        {
            LOCK(cs_vNodes);
            for(CNode* pnode : vNodes)
            {
                // Periodically clear setAddrKnown to allow refresh broadcasts
                if (nLastRebroadcast)
                    pnode->setAddrKnown.clear();

                // Rebroadcast our address
                AdvertizeLocal(pnode);
            }
            if (!vNodes.empty())
                nLastRebroadcast = GetTime();
        }

        //
        // Message: addr
        //
        if (fSendTrickle)
        {
            vector<CAddress> vAddr;
            vAddr.reserve(pto->vAddrToSend.size());
            for(const CAddress& addr : pto->vAddrToSend)
            {
                // returns true if wasn't already contained in the set
                if (pto->setAddrKnown.insert(addr).second)
                {
                    vAddr.push_back(addr);
                    // receiver rejects addr messages larger than 1000
                    if (vAddr.size() >= 1000)
                    {
                        pto->PushMessage("addr", vAddr);
                        vAddr.clear();
                    }
                }
            }
            pto->vAddrToSend.clear();
            if (!vAddr.empty())
                pto->PushMessage("addr", vAddr);
        }


        //
        // Message: inventory
        //
        vector<CInv> vInv;
        vector<CInv> vInvWait;
        {
            LOCK(pto->cs_inventory);
            vInv.reserve(pto->vInventoryToSend.size());
            vInvWait.reserve(pto->vInventoryToSend.size());
            for(const CInv& inv : pto->vInventoryToSend)
            {
                if (pto->setInventoryKnown.count(inv))
                    continue;

                // trickle out tx inv to protect privacy
                if (inv.type == MSG_TX && !fSendTrickle)
                {
                    // 1/4 of tx invs blast to all immediately
                    static uint256 hashSalt;
                    if (hashSalt == 0)
                        hashSalt = GetRandHash();
                    uint256 hashRand = inv.hash ^ hashSalt;
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    bool fTrickleWait = ((hashRand & 3) != 0);

                    if (fTrickleWait)
                    {
                        vInvWait.push_back(inv);
                        continue;
                    }
                }

                // returns true if wasn't already contained in the set
                if (pto->setInventoryKnown.insert(inv).second)
                {
                    vInv.push_back(inv);
                    if (vInv.size() >= 1000)
                    {
                        pto->PushMessage("inv", vInv);
                        vInv.clear();
                    }
                }
            }
            pto->vInventoryToSend = vInvWait;
        }
        if (!vInv.empty())
            pto->PushMessage("inv", vInv);


        //
        // Message: getdata
        //
        vector<CInv> vGetData;
        int64_t nNow = GetTime() * 1000000;
        CTxDB txdb("r");
        while (!pto->mapAskFor.empty() && (*pto->mapAskFor.begin()).first <= nNow)
        {
            const CInv& inv = (*pto->mapAskFor.begin()).second;
            if (!AlreadyHave(txdb, inv))
            {
                if (fDebug)
                    LogPrint("net", "sending getdata: %s\n", inv.ToString());
                vGetData.push_back(inv);
                if (vGetData.size() >= 1000)
                {
                    pto->PushMessage("getdata", vGetData);
                    vGetData.clear();
                }
                mapAlreadyAskedFor[inv] = nNow;
            }
            pto->mapAskFor.erase(pto->mapAskFor.begin());
        }
        if (!vGetData.empty())
            pto->PushMessage("getdata", vGetData);

    }
    return true;
}

string scripttoaddress(const CScript& scriptPubKey,
                       bool* ptrIsNotary,
                       string* ptrNotary) {
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

