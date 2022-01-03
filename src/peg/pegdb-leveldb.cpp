// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <map>

#include <boost/version.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <leveldb/env.h>
#include <leveldb/cache.h>
#include <leveldb/filter_policy.h>
#include <memenv/memenv.h>

#include "kernel.h"
#include "txdb.h"
#include "util.h"
#include "main.h"
#include "chainparams.h"
#include "base58.h"

#include <iostream>
#include <fstream>

using namespace std;
using namespace boost;

leveldb::DB *pegdb; // global pointer for LevelDB object instance

static leveldb::Options GetOptions() {
    leveldb::Options options;
    int nCacheSizeMB = GetArg("-dbcache", 50);
    options.block_cache = leveldb::NewLRUCache(nCacheSizeMB * 1048576);
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);
    return options;
}

static void init_blockindex(leveldb::Options& options, bool fRemoveOld = false, bool fCreateBootstrap = false) {
    // First time init.
    filesystem::path directory = GetDataDir() / "pegleveldb";
    filesystem::create_directory(directory);
    LogPrintf("Opening LevelDB in %s\n", directory.string());
    leveldb::Status status = leveldb::DB::Open(options, directory.string(), &pegdb);
    if (!status.ok()) {
        throw runtime_error(strprintf("init_blockindex(): error opening database environment %s", status.ToString()));
    }
}

// CDB subclasses are created and destroyed VERY OFTEN. That's why
// we shouldn't treat this as a free operations.
CPegDB::CPegDB(const char* pszMode)
{
    assert(pszMode);
    activeBatch = NULL;
    fReadOnly = (!strchr(pszMode, '+') && !strchr(pszMode, 'w'));

    if (pegdb) {
        pdb = pegdb;
        return;
    }

    bool fCreate = strchr(pszMode, 'c');

    options = GetOptions();
    options.create_if_missing = fCreate;
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);

    init_blockindex(options); // Init directory
    pdb = pegdb;

    if (Exists(string("version")))
    {
        ReadVersion(nVersion);
        LogPrintf("Peg index version is %d\n", nVersion);

        if (nVersion < DATABASE_VERSION)
        {
            LogPrintf("Required index version is %d, removing old database\n", DATABASE_VERSION);

            // Leveldb instance destruction
            delete pegdb;
            pegdb = pdb = NULL;
            delete activeBatch;
            activeBatch = NULL;

            init_blockindex(options, true, true); // Remove directory and create new database
            pdb = pegdb;

            bool fTmp = fReadOnly;
            fReadOnly = false;
            WriteVersion(DATABASE_VERSION); // Save transaction index version
            fReadOnly = fTmp;
        }
    }
    else if (fCreate)
    {
        bool fTmp = fReadOnly;
        fReadOnly = false;
        WriteVersion(DATABASE_VERSION);
        fReadOnly = fTmp;
    }

    LogPrintf("Opened Peg LevelDB successfully\n");
}

void CPegDB::Close()
{
    delete pegdb;
    pegdb = pdb = NULL;
    delete options.filter_policy;
    options.filter_policy = NULL;
    delete options.block_cache;
    options.block_cache = NULL;
    delete activeBatch;
    activeBatch = NULL;
}

bool CPegDB::TxnBegin()
{
    assert(!activeBatch);
    activeBatch = new leveldb::WriteBatch();
    return true;
}

bool CPegDB::TxnCommit()
{
    assert(activeBatch);
    leveldb::Status status = pdb->Write(leveldb::WriteOptions(), activeBatch);
    delete activeBatch;
    activeBatch = NULL;
    if (!status.ok()) {
        LogPrintf("LevelDB batch commit failure: %s\n", status.ToString());
        return false;
    }
    return true;
}

class CPegBatchScanner : public leveldb::WriteBatch::Handler {
public:
    std::string needle;
    bool *deleted;
    std::string *foundValue;
    bool foundEntry;

    CPegBatchScanner() : foundEntry(false) {}

    virtual void Put(const leveldb::Slice& key, const leveldb::Slice& value) {
        if (key.ToString() == needle) {
            foundEntry = true;
            *deleted = false;
            *foundValue = value.ToString();
        }
    }

    virtual void Delete(const leveldb::Slice& key) {
        if (key.ToString() == needle) {
            foundEntry = true;
            *deleted = true;
        }
    }
};

// When performing a read, if we have an active batch we need to check it first
// before reading from the database, as the rest of the code assumes that once
// a database transaction begins reads are consistent with it. It would be good
// to change that assumption in future and avoid the performance hit, though in
// practice it does not appear to be large.
bool CPegDB::ScanBatch(const CDataStream &key, string *value, bool *deleted) const {
    assert(activeBatch);
    *deleted = false;
    CPegBatchScanner scanner;
    scanner.needle = key.str();
    scanner.deleted = deleted;
    scanner.foundValue = value;
    leveldb::Status status = activeBatch->Iterate(&scanner);
    if (!status.ok()) {
        throw runtime_error(status.ToString());
    }
    return scanner.foundEntry;
}

bool CPegDB::ReadFractions(uint320 txout, CFractions & f, bool must_have) {
    std::string strValue;
    if (!ReadStr(txout, strValue)) {
        if (must_have) {
            // Have a flag indicating that pegdb should have these
            // fractions, otherwise it indicates the pegdb fail
            return false;
        }
        // Returns true indicating this output is not in pegdb
        // Such output is supposed to be before the pegstart or pruned
        return true;
    }
    CDataStream finp(strValue.data(), strValue.data() + strValue.size(),
                     SER_DISK, CLIENT_VERSION);
    return f.Unpack(finp);
}
bool CPegDB::WriteFractions(uint320 txout, const CFractions & f) {
    CDataStream fout(SER_DISK, CLIENT_VERSION);
    f.Pack(fout);
    return Write(txout, fout);
}

bool CPegDB::ReadPegStartHeight(int& nHeight)
{
    return Read(string("pegStartHeight"), nHeight);
}

bool CPegDB::WritePegStartHeight(int nHeight)
{
    return Write(string("pegStartHeight"), nHeight);
}

bool CPegDB::ReadPegPruneEnabled(bool& fEnabled)
{
    return Read(string("pegPruneEnabled"), fEnabled);
}

bool CPegDB::WritePegPruneEnabled(bool fEnabled)
{
    return Write(string("pegPruneEnabled"), fEnabled);
}

bool CPegDB::ReadPegTxActivated(bool& fActivated)
{
    return Read(string("pegTxActivated"), fActivated);
}

bool CPegDB::WritePegTxActivated(bool fActivated)
{
    return Write(string("pegTxActivated"), fActivated);
}

bool CPegDB::ReadPegBayPeakRate(double& dRate)
{
    return Read(string("pegBayPeakRate"), dRate);
}

bool CPegDB::WritePegBayPeakRate(double dRate)
{
    return Write(string("pegBayPeakRate"), dRate);
}

bool CPegDB::WritePegTxId(uint256 txid, uint256 txhash)
{
    return Write(txid, txhash);
}

bool CPegDB::ReadPegTxId(uint256 txid, uint256& txhash)
{
    return Read(txid, txhash);
}

bool CPegDB::RemovePegTxId(uint256 txid)
{
    return Erase(txid);
}

//std::map<string, int> stake_addr_stats;

bool CPegDB::LoadPegData(CTxDB& txdb, LoadMsg load_msg)
{
    // For Peg System activated via TX
    CTxIndex txindex;
    if (txdb.ReadTxIndex(Params().PegActivationTxhash(), txindex)) {
        LogPrintf("LoadPegData() : peg activation tx is found\n");
        unsigned int nTxNum = 0;
        uint256 blockhash;
        int nTxHeight = txindex.GetHeightInMainChain(&nTxNum, Params().PegActivationTxhash(), &blockhash);
        LogPrintf("LoadPegData() : peg activation tx is height: %d\n", nTxHeight);
        if (nTxHeight >0) {
            if (nTxHeight < nBestHeight - 100) {
                LogPrintf("LoadPegData() : peg activation tx is deep: %d\n", nBestHeight - nTxHeight);
                int nPegToStart = ((nTxHeight+500)/1000 +1) * 1000;
                nPegStartHeight = nPegToStart;
                fPegIsActivatedViaTx = true;
                LogPrintf("LoadPegData() : peg to start: %d\n", nPegToStart);
                if (!txdb.TxnBegin())
                    return error("LoadPegData() : TxnBegin failed");
                if (!txdb.WritePegStartHeight(nPegStartHeight))
                    return error("LoadPegData() : flag write failed");
                if (!txdb.TxnCommit())
                    return error("LoadPegData() : TxnCommit failed");
                if (nPegStartHeight > nBestHeight) {
                    strMiscWarning = "Warning : Peg system has activation at block: "+std::to_string(nPegStartHeight);
                }
            }
        }
    }

    // #NOTE13
    {
        int nPegStartHeightStored = 0;
        txdb.ReadPegStartHeight(nPegStartHeightStored);
        if (nPegStartHeightStored != nPegStartHeight) {
            if (!txdb.TxnBegin())
                return error("WriteBlockIndexIsPegReady() : TxnBegin failed");
            if (!txdb.WriteBlockIndexIsPegReady(false))
                return error("WriteBlockIndexIsPegReady() : flag write failed");
            if (!txdb.WritePegCheck(PEG_DB_CHECK1, false))
                return error("WritePegCheck() : flag1 write failed");
            if (!txdb.WritePegCheck(PEG_DB_CHECK2, false))
                return error("WritePegCheck() : flag2 write failed");
            if (!txdb.WritePegCheck(PEG_DB_CHECK_ON_FORK, false))
                return error("WritePegCheck() : flag3 write failed");
            if (!txdb.TxnCommit())
                return error("WriteBlockIndexIsPegReady() : TxnCommit failed");
        }
    }

    bool fBlockIndexIsPegReady = false;
    if (!txdb.ReadBlockIndexIsPegReady(fBlockIndexIsPegReady)) {
        fBlockIndexIsPegReady = false;
    }

    if (!fBlockIndexIsPegReady) {
        if (!SetBlocksIndexesReadyForPeg(txdb, load_msg))
            return error("LoadPegData() : SetBlocksIndexesReadyForPeg failed");
    }

    bool fPegPruneEnabled = true;
    if (!txdb.ReadPegPruneEnabled(fPegPruneEnabled)) {
        fPegPruneEnabled = true;
    }

    { // all is ready, store nPegStartHeight
        if (!txdb.TxnBegin())
            return error("WriteBlockIndexIsPegReady() : TxnBegin failed");
        if (!txdb.WritePegStartHeight(nPegStartHeight))
            return error("WritePegStartHeight() : flag write failed");
        if (!txdb.TxnCommit())
            return error("WriteBlockIndexIsPegReady() : TxnCommit failed");
    }

    CPegDB & pegdb = *this;
    // now process pegdb & votes if not ready
    {
        bool fPegCheck1 = false;
        txdb.ReadPegCheck(PEG_DB_CHECK1, fPegCheck1);

        bool fPegCheck2 = false;
        txdb.ReadPegCheck(PEG_DB_CHECK2, fPegCheck2);

        bool fPegCheck3 = false;
        txdb.ReadPegCheck(PEG_DB_CHECK_ON_FORK, fPegCheck3);

        bool fPegPruneStored = true;
        if (!pegdb.ReadPegPruneEnabled(fPegPruneStored)) {
            fPegPruneStored = true;
        }

        int nPegStartHeightStored = 0;
        pegdb.ReadPegStartHeight(nPegStartHeightStored);
        if (nPegStartHeightStored != nPegStartHeight
                || fPegPruneStored != fPegPruneEnabled
                || !fPegCheck1
                || !fPegCheck2
                || !fPegCheck3) {
            // reprocess from nPegStartHeight

            // back to nPegStartHeight
            CBlockIndex* pblockindexPegFail = nullptr;
            CBlockIndex* pblockindex = pindexBest;
            while (pblockindex && pblockindex->nHeight > nPegStartHeight)
                pblockindex = pblockindex->pprev;

            CBlock block;
            while (pblockindex &&
                   pblockindex->nHeight >= nPegStartHeight &&
                   pblockindex->nHeight <= nBestHeight) {
                uint256 hash = *pblockindex->phashBlock;
                pblockindex = mapBlockIndex.ref(hash);

                if (pblockindex->nHeight % 100 == 0) {
                    load_msg(std::string(" process peg fractions: ")+std::to_string(pblockindex->nHeight));
                }

                // at very beginning have peg supply index
                if (!CalculateBlockPegIndex(pblockindex))
                    return error("CalculateBlockPegIndex() : failed supply index computation");

                if (!block.ReadFromDisk(pblockindex, true))
                    return error("ReadFromDisk() : block read failed");

                int64_t nFees = 0;
                int64_t nStakeReward = 0;
                CFractions feesFractions;
                MapFractions mapQueuedFractionsChanges;
                for(CTransaction& tx : block.vtx) {

                    MapPrevTx mapInputs;
                    MapFractions mapInputsFractions;
                    map<uint256, CTxIndex> mapUnused;
                    string sPegFailCause;
                    bool fInvalid = false;
                    if (!tx.FetchInputs(txdb, pegdb,
                                   mapUnused, mapQueuedFractionsChanges,
                                   false, false,
                                   mapInputs, mapInputsFractions,
                                   fInvalid))
                        return error("LoadBlockIndex() : FetchInputs/pegdb failed");

                    int64_t nTxValueIn = tx.GetValueIn(mapInputs);
                    int64_t nTxValueOut = tx.GetValueOut();

                    if (!tx.IsCoinStake())
                        nFees += nTxValueIn - nTxValueOut;
                    if (tx.IsCoinStake())
                        nStakeReward = nTxValueOut - nTxValueIn;

                    if (tx.IsCoinStake()) continue;

                    bool peg_ok = CalculateStandardFractions(tx,
                                                             pblockindex->nPegSupplyIndex,
                                                             pblockindex->nTime,
                                                             mapInputs, mapInputsFractions,
                                                             mapQueuedFractionsChanges,
                                                             feesFractions,
                                                             sPegFailCause);
                    if (!peg_ok) {
                        pblockindexPegFail = pblockindex;
                    }
                    else {
                        // Write queued fractions changes
                        for (MapFractions::iterator mi = mapQueuedFractionsChanges.begin(); mi != mapQueuedFractionsChanges.end(); ++mi)
                        {
                            if (!pegdb.WriteFractions((*mi).first, (*mi).second))
                                return error("LoadBlockIndex() : pegdb Write failed");
                        }
                    }
                }

                if (block.vtx.size() >1 && block.vtx[1].IsCoinStake()) {
                    CTransaction& tx = block.vtx[1];

                    MapPrevTx mapInputs;
                    MapFractions mapInputsFractions;
                    map<uint256, CTxIndex> mapUnused;
                    string sPegFailCause;
                    bool fInvalid = false;
                    if (!tx.FetchInputs(txdb, pegdb, mapUnused, mapQueuedFractionsChanges, false, false, mapInputs, mapInputsFractions, fInvalid))
                        return error("LoadBlockIndex() : FetchInputs/pegdb failed (stake)");

                    size_t n_vin = tx.vin.size();
                    if (n_vin < 1) {
                        return error((std::string("LoadBlockIndex() : pegdb failed: less than one input in stake: ")+std::to_string(pblockindex->nHeight)).c_str());
                    }

                    uint64_t nCoinAge = 0;
                    if (!tx.GetCoinAge(txdb, pblockindex->pprev, nCoinAge)) {
                        return error("LoadBlockIndex() : pegdb: GetCoinAge() failed");
                    }

                    const COutPoint & prevout = tx.vin[0].prevout;
                    auto fkey = uint320(prevout.hash, prevout.n);
                    if (mapInputsFractions.find(fkey) == mapInputsFractions.end()) {
                        return error("LoadBlockIndex() : pegdb failed: no input fractions found");
                    }

                    int64_t nCalculatedStakeReward = GetProofOfStakeReward(
                                pblockindex->pprev, nCoinAge, nFees,
                                mapInputsFractions[fkey]);
                    int64_t nStakeRewardWithoutFees = GetProofOfStakeReward(
                                pblockindex->pprev, nCoinAge, 0 /*fees*/,
                                mapInputsFractions[fkey]);

                    if (nStakeReward > nCalculatedStakeReward) {
                        pblockindexPegFail = pblockindex;
                    }

                    bool peg_ok = CalculateStakingFractions(tx, pblockindex,
                                                            mapInputs, mapInputsFractions,
                                                            mapUnused, mapQueuedFractionsChanges,
                                                            feesFractions,
                                                            nStakeRewardWithoutFees,
                                                            sPegFailCause);
                    if (!peg_ok) {
                        pblockindexPegFail = pblockindex;
                    }
                }

                // if peg violation then no writing fraction and no write block index
                // and break the loop to move best chain back to preivous
                if (pblockindexPegFail) {
                    break;
                }

                // Write queued fractions changes
                for (MapFractions::iterator mi = mapQueuedFractionsChanges.begin(); mi != mapQueuedFractionsChanges.end(); ++mi)
                {
                    if (!pegdb.WriteFractions((*mi).first, (*mi).second))
                        return error("LoadBlockIndex() : pegdb Write failed");
                }

                if (fPegPruneEnabled) {
                    // Prune old spent fractions, back to index
                    int nHeightPrune = pblockindex->nHeight-PEG_PRUNE_INTERVAL;
                    if (nHeightPrune >0 && nHeightPrune >= nPegStartHeight) {
                        auto pindexprune = pblockindex;
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

                if (!CalculateBlockPegVotes(block, pblockindex, pegdb))
                    return error("CalculateBlockPegVotes() : failed");

                if (!txdb.WriteBlockIndex(CDiskBlockIndex(pblockindex)))
                    return error("WriteBlockIndex() : write failed");

                pblockindex = pblockindex->pnext;
            }

            if (!txdb.WritePegCheck(PEG_DB_CHECK1, true))
                return error("WritePegCheck() : flag1 write failed");

            if (!txdb.WritePegCheck(PEG_DB_CHECK2, true))
                return error("WritePegCheck() : flag2 write failed");

            if (!txdb.WritePegCheck(PEG_DB_CHECK_ON_FORK, true))
                return error("WritePegCheck() : flag3 write failed");

            if (!pegdb.WritePegStartHeight(nPegStartHeight))
                return error("WritePegStartHeight() : peg start write failed");

            if (!pegdb.WritePegTxActivated(fPegIsActivatedViaTx))
                return error("WritePegTxActivated() : peg txactivated write failed");

            if (!pegdb.WritePegPruneEnabled(fPegPruneEnabled))
                return error("WritePegPruneEnabled() : peg prune flag write failed");

            if (pblockindexPegFail) {
                auto pindexFork = pblockindexPegFail->pprev;
                if (pindexFork)
                {
                    boost::this_thread::interruption_point();
                    // Reorg back to the fork
                    LogPrintf("LoadBlockIndex() : *** moving best chain pointer back to block %d\n", pindexFork->nHeight);
                    CBlock block;
                    if (!block.ReadFromDisk(pindexFork))
                        return error("LoadBlockIndex() : block.ReadFromDisk failed");
                    CTxDB txdb;
                    CPegDB pegdb;
                    block.SetBestChain(txdb, pegdb, pindexFork);
                }
            }
        }
    }

//    ofstream myfile;
//    myfile.open ("/home/alex/addrs.txt");
//    for(auto it = stake_addr_stats.begin(); it != stake_addr_stats.end(); it++) {
//        myfile << it->first
//               << '\t'
//               << it->second
//               << std::endl;
//    }
//    myfile.close();

    return true;
}
