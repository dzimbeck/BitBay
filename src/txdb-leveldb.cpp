// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2020 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <map>

#include <boost/version.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <leveldb/env.h>
#include <leveldb/cache.h>
#include <leveldb/filter_policy.h>

#include "kernel.h"
#include "txdb.h"
#include "util.h"
#include "main.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "base58.h"
#include "peg.h"
#include "script.h"

#include <iostream>
#include <fstream>

using namespace std;
using namespace boost;

leveldb::DB *txdb; // global pointer for LevelDB object instance

static leveldb::Options GetOptions() {
    leveldb::Options options;
    int nCacheSizeMB = GetArg("-dbcache", 50);
    options.block_cache = leveldb::NewLRUCache(nCacheSizeMB * 1048576);
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);
    return options;
}

static void init_blockindex(leveldb::Options& options, bool fRemoveOld = false, bool fCreateBootstrap = false) {
    // First time init.
    filesystem::path directory = GetDataDir() / "txleveldb";

    if (fRemoveOld) {
        filesystem::remove_all(directory); // remove directory
        unsigned int nFile = 1;
        filesystem::path bootstrap = GetDataDir() / "bootstrap.dat";

        while (true)
        {
            filesystem::path strBlockFile = GetDataDir() / strprintf("blk%04u.dat", nFile);

            // Break if no such file
            if( !filesystem::exists( strBlockFile ) )
                break;

            if (fCreateBootstrap && nFile == 1 && !filesystem::exists(bootstrap)) {
                filesystem::rename(strBlockFile, bootstrap);
            } else {
                filesystem::remove(strBlockFile);
            }

            nFile++;
        }
    }

    filesystem::create_directory(directory);
    LogPrintf("Opening LevelDB in %s\n", directory.string());
    leveldb::Status status = leveldb::DB::Open(options, directory.string(), &txdb);
    if (!status.ok()) {
        throw runtime_error(strprintf("init_blockindex(): error opening database environment %s", status.ToString()));
    }
}

// CDB subclasses are created and destroyed VERY OFTEN. That's why
// we shouldn't treat this as a free operations.
CTxDB::CTxDB(const char* pszMode)
{
    assert(pszMode);
    activeBatch = NULL;
    fReadOnly = (!strchr(pszMode, '+') && !strchr(pszMode, 'w'));

    if (txdb) {
        pdb = txdb;
        return;
    }

    bool fCreate = strchr(pszMode, 'c');

    options = GetOptions();
    options.create_if_missing = fCreate;
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);

    init_blockindex(options); // Init directory
    pdb = txdb;

    if (Exists(string("version")))
    {
        ReadVersion(nVersion);
        LogPrintf("Transaction index version is %d\n", nVersion);

        if (nVersion < DATABASE_VERSION)
        {
            LogPrintf("Required index version is %d, removing old database\n", DATABASE_VERSION);

            // Leveldb instance destruction
            delete txdb;
            txdb = pdb = NULL;
            delete activeBatch;
            activeBatch = NULL;

            init_blockindex(options, true, true); // Remove directory and create new database
            pdb = txdb;

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

    LogPrintf("Opened LevelDB successfully\n");
}

void CTxDB::Close()
{
    delete txdb;
    txdb = pdb = NULL;
    delete options.filter_policy;
    options.filter_policy = NULL;
    delete options.block_cache;
    options.block_cache = NULL;
    delete activeBatch;
    activeBatch = NULL;
}

bool CTxDB::TxnBegin()
{
    assert(!activeBatch);
    activeBatch = new leveldb::WriteBatch();
    return true;
}

bool CTxDB::TxnCommit()
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

class CBatchScanner : public leveldb::WriteBatch::Handler {
public:
    std::string needle;
    bool *deleted;
    std::string *foundValue;
    bool foundEntry;

    CBatchScanner() : foundEntry(false) {}

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

class CBatchSeeker : public leveldb::WriteBatch::Handler {
public:
    std::string needle;
    std::string stople;
    std::map<std::string,std::string, CTxDB::cmpBySlice> *seekmap;
    std::set<std::string> *erased;

    CBatchSeeker() {}

    virtual void Put(const leveldb::Slice& key, const leveldb::Slice& value) {
        leveldb::Slice sneedle(needle);
        leveldb::Slice sstople(stople);
        if (key.compare(sneedle) >= 0) {
            if (stople.empty() || key.compare(sstople) <= 0) {
                (*seekmap)[key.ToString()] = value.ToString();
            }
        }
        erased->erase(key.ToString());
    }

    virtual void Delete(const leveldb::Slice& key) {
        seekmap->erase(key.ToString());
        erased->insert(key.ToString());
    }
};

// When performing a read, if we have an active batch we need to check it first
// before reading from the database, as the rest of the code assumes that once
// a database transaction begins reads are consistent with it. It would be good
// to change that assumption in future and avoid the performance hit, though in
// practice it does not appear to be large.
bool CTxDB::ScanBatch(const CDataStream &key, string *value, bool *deleted) const {
    assert(activeBatch);
    *deleted = false;
    CBatchScanner scanner;
    scanner.needle = key.str();
    scanner.deleted = deleted;
    scanner.foundValue = value;
    leveldb::Status status = activeBatch->Iterate(&scanner);
    if (!status.ok()) {
        throw runtime_error(status.ToString());
    }
    return scanner.foundEntry;
}

// When performing a seek, if we have an active batch we need to check it first
// before reading from the database, as the rest of the code assumes that once
// a database transaction begins reads are consistent with it. It would be good
// to change that assumption in future and avoid the performance hit, though in
// practice it does not appear to be large.
bool CTxDB::SeekBatch(const CDataStream &fromkey, 
                      string *key, string *value, 
                      std::map<std::string,std::string, CTxDB::cmpBySlice> *seekmap,
                      std::set<std::string> *erased) const {
    assert(activeBatch);
    CBatchSeeker scanner;
    scanner.needle = fromkey.str();
    scanner.erased = erased;
    scanner.seekmap = seekmap;
    leveldb::Status status = activeBatch->Iterate(&scanner);
    if (!status.ok()) {
        throw runtime_error(status.ToString());
    }
    if (scanner.seekmap->empty())
        return false;
    *key = scanner.seekmap->begin()->first;
    *value = scanner.seekmap->begin()->second;
    return true;
}

// When performing a seek, if we have an active batch we need to check it first
// before reading from the database, as the rest of the code assumes that once
// a database transaction begins reads are consistent with it. It would be good
// to change that assumption in future and avoid the performance hit, though in
// practice it does not appear to be large.
bool CTxDB::SeekBatch(const CDataStream &fromkey, 
                      const CDataStream &tokey, 
                      std::map<std::string,std::string, CTxDB::cmpBySlice> *seekmap,
                      std::set<std::string> *erased) const {
    assert(activeBatch);
    CBatchSeeker scanner;
    scanner.needle = fromkey.str();
    scanner.stople = tokey.str();
    scanner.erased = erased;
    scanner.seekmap = seekmap;
    leveldb::Status status = activeBatch->Iterate(&scanner);
    if (!status.ok()) {
        throw runtime_error(status.ToString());
    }
    return !scanner.seekmap->empty();
}

bool CTxDB::ReadTxIndex(uint256 hash, CTxIndex& txindex)
{
    txindex.SetNull();
    return Read(make_pair(string("tx"), hash), txindex);
}

bool CTxDB::UpdateTxIndex(uint256 hash, const CTxIndex& txindex)
{
    return Write(make_pair(string("tx"), hash), txindex);
}

bool CTxDB::AddTxIndex(const CTransaction& tx, const CDiskTxPos& pos, int64_t nHeight, uint16_t nTxIndex)
{
    // Add to tx index
    uint256 hash = tx.GetHash();
    CTxIndex txindex(pos, tx.vout.size(), nHeight, nTxIndex);
    return Write(make_pair(string("tx"), hash), txindex);
}

bool CTxDB::EraseTxIndex(const CTransaction& tx)
{
    uint256 hash = tx.GetHash();

    return Erase(make_pair(string("tx"), hash));
}

//bool SetTxIndexesV1(CTxDB & ctxdb,
//                    LoadMsg load_msg);

static bool SetTxIndexesV1(CTxDB & ctxdb, LoadMsg load_msg) {
    if (!ctxdb.TxnBegin())
        return error("SetTxIndexesV1() : TxnBegin failed");

    leveldb::Iterator *iterator = txdb->NewIterator(leveldb::ReadOptions());
    // Seek to start key.
    CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
    ssStartKey << make_pair(string("tx"), uint256(0));
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
        if (strType != "tx")
            break;
        
        uint256 txhash;
        auto keypair = make_pair(string("tx"), txhash);
        CDataStream ssKeyRead(SER_DISK, CLIENT_VERSION);
        ssKeyRead.write(iterator->key().data(), iterator->key().size());
        ssKeyRead >> keypair;
        
        CTxIndex txindex;
        if (!ctxdb.ReadTxIndex(keypair.second, txindex)) {
            return error("SetTxIndexesV1() : ReadTxIndex failed, txhash %s", keypair.second.GetHex());
        }
        
        uint256 blockhash;
        unsigned int nTxIndex = 0;
        txindex.nHeight = txindex.GetHeightInMainChain(&nTxIndex, keypair.second, &blockhash);
        if (txindex.nHeight == 0) {
            return error("SetTxIndexesV1() : GetHeightInMainChain failed, txhash %s", keypair.second.GetHex());
        }
        txindex.nIndex = uint16_t(nTxIndex);
        txindex.nVersion = 1;
        
        if (!ctxdb.UpdateTxIndex(keypair.second, txindex)) {
            return error("SetTxIndexesV1() : UpdateTxIndex failed");
        }

        iterator->Next();

        indexCount++;
        if (indexCount % 10000 == 0) {
            load_msg(std::string(" update tx indexes: ")+std::to_string(indexCount));
            // commit on every 10k
            if (!ctxdb.TxnCommit())
                return error("SetTxIndexesV1() : TxnCommit failed");
            if (!ctxdb.TxnBegin())
                return error("SetTxIndexesV1() : TxnBegin failed");
        }
    }
    delete iterator;

    if (!ctxdb.WriteTxIndexIsV1Ready(true))
        return error("SetTxIndexesV1() : flag write failed");

    if (!ctxdb.TxnCommit())
        return error("SetTxIndexesV1() : TxnCommit failed");

    return true;
}

bool CTxDB::ContainsTx(uint256 hash)
{
    return Exists(make_pair(string("tx"), hash));
}

bool CTxDB::ReadDiskTx(uint256 hash, CTransaction& tx, CTxIndex& txindex)
{
    tx.SetNull();
    if (!ReadTxIndex(hash, txindex))
        return false;
    return (tx.ReadFromDisk(txindex.pos));
}

bool CTxDB::ReadDiskTx(uint256 hash, CTransaction& tx)
{
    CTxIndex txindex;
    return ReadDiskTx(hash, tx, txindex);
}

bool CTxDB::ReadDiskTx(COutPoint outpoint, CTransaction& tx, CTxIndex& txindex)
{
    return ReadDiskTx(outpoint.hash, tx, txindex);
}

bool CTxDB::ReadDiskTx(COutPoint outpoint, CTransaction& tx)
{
    CTxIndex txindex;
    return ReadDiskTx(outpoint.hash, tx, txindex);
}

bool CTxDB::WriteBlockIndex(const CDiskBlockIndex& blockindex)
{
    return Write(make_pair(string("blockindex"), blockindex.GetBlockHash()), blockindex);
}

bool CTxDB::ReadHashBestChain(uint256& hashBestChain)
{
    return Read(string("hashBestChain"), hashBestChain);
}

bool CTxDB::WriteHashBestChain(uint256 hashBestChain)
{
    return Write(string("hashBestChain"), hashBestChain);
}

bool CTxDB::ReadPegStartHeight(int& nHeight)
{
    return Read(string("pegStartHeight"), nHeight);
}

bool CTxDB::WritePegStartHeight(int nHeight)
{
    return Write(string("pegStartHeight"), nHeight);
}

bool CTxDB::ReadTxIndexIsV1Ready(bool& bReady)
{
    return Read(string("txIndexIsV1Ready"), bReady);
}

bool CTxDB::WriteTxIndexIsV1Ready(bool bReady)
{
    return Write(string("txIndexIsV1Ready"), bReady);
}

bool CTxDB::ReadBlockIndexIsPegReady(bool& bReady)
{
    return Read(string("blockIndexIsPegReady"), bReady);
}

bool CTxDB::WriteBlockIndexIsPegReady(bool bReady)
{
    return Write(string("blockIndexIsPegReady"), bReady);
}

bool CTxDB::ReadPegCheck(int nCheck, bool& bReady)
{
    return Read(string("pegCheck")+std::to_string(nCheck), bReady);
}

bool CTxDB::WritePegCheck(int nCheck, bool bReady)
{
    return Write(string("pegCheck")+std::to_string(nCheck), bReady);
}

bool CTxDB::ReadPegPruneEnabled(bool& fEnabled)
{
#ifdef ENABLE_EXCHANGE
    fEnabled = false;
    return true;
#elif ENABLE_EXPLORER
    fEnabled = false;
    return true;
#else
    return Read(string("pegPruneEnabled"), fEnabled);
#endif
}

bool CTxDB::WritePegPruneEnabled(bool fEnabled)
{
    return Write(string("pegPruneEnabled"), fEnabled);
}

bool CTxDB::ReadBestInvalidTrust(CBigNum& bnBestInvalidTrust)
{
    return Read(string("bnBestInvalidTrust"), bnBestInvalidTrust);
}

bool CTxDB::WriteBestInvalidTrust(CBigNum bnBestInvalidTrust)
{
    return Write(string("bnBestInvalidTrust"), bnBestInvalidTrust);
}

CBlockIndex *CTxDB::InsertBlockIndex(uint256 hash)
{
    if (hash == 0)
        return NULL;

    // Return existing
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end())
        return (*mi).second;

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    if (!pindexNew)
        throw runtime_error("LoadBlockIndex() : new CBlockIndex failed");
    mi = mapBlockIndex.insert(hash, pindexNew).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool CTxDB::LoadBlockIndex(LoadMsg load_msg)
{
    if (mapBlockIndex.size() > 0) {
        // Already loaded once in this session. It can happen during migration
        // from BDB.
        return true;
    }
    // The block index is an in-memory structure that maps hashes to on-disk
    // locations where the contents of the block can be found. Here, we scan it
    // out of the DB and into mapBlockIndex.
    leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
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
        // Construct block index object
        CBlockIndex* pindexNew    = InsertBlockIndex(blockHash);
        pindexNew->pprev          = InsertBlockIndex(diskindex.hashPrev);
        pindexNew->pnext          = InsertBlockIndex(diskindex.hashNext);
        pindexNew->nFile          = diskindex.nFile;
        pindexNew->nBlockPos      = diskindex.nBlockPos;
        pindexNew->nHeight        = diskindex.nHeight;
        pindexNew->nMint          = diskindex.nMint;
        pindexNew->nMoneySupply   = diskindex.nMoneySupply;
        pindexNew->nPegSupplyIndex  = diskindex.nPegSupplyIndex;
        pindexNew->nPegVotesInflate = diskindex.nPegVotesInflate;
        pindexNew->nPegVotesDeflate = diskindex.nPegVotesDeflate;
        pindexNew->nPegVotesNochange= diskindex.nPegVotesNochange;
        pindexNew->nFlags         = diskindex.nFlags;
        pindexNew->nStakeModifier = diskindex.nStakeModifier;
        pindexNew->bnStakeModifierV2 = diskindex.bnStakeModifierV2;
        pindexNew->prevoutStake   = diskindex.prevoutStake;
        pindexNew->nStakeTime     = diskindex.nStakeTime;
        pindexNew->hashProof      = diskindex.hashProof;
        pindexNew->nVersion       = diskindex.nVersion;
        pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
        pindexNew->nTime          = diskindex.nTime;
        pindexNew->nBits          = diskindex.nBits;
        pindexNew->nNonce         = diskindex.nNonce;

        // Watch for genesis block
        if (pindexGenesisBlock == NULL && blockHash == Params().HashGenesisBlock())
            pindexGenesisBlock = pindexNew;

        if (!pindexNew->CheckIndex()) {
            delete iterator;
            return error("LoadBlockIndex() : CheckIndex failed at %d", pindexNew->nHeight);
        }

        // NovaCoin: build setStakeSeen
        if (pindexNew->IsProofOfStake())
            setStakeSeen.insert(make_pair(pindexNew->prevoutStake, pindexNew->nStakeTime));

        iterator->Next();
        indexCount++;
        if (indexCount % 10000 == 0) {
            load_msg(std::to_string(indexCount));
        }
    }
    delete iterator;

    boost::this_thread::interruption_point();
    
    // Calculate nChainTrust
    vector<pair<int, CBlockIndex*> > vSortedByHeight;
    vSortedByHeight.reserve(mapBlockIndex.size());
    for(const std::pair<uint256, CBlockIndex*> & item : mapBlockIndex)
    {
        CBlockIndex* pindex = item.second;
        vSortedByHeight.push_back(make_pair(pindex->nHeight, pindex));
    }
    sort(vSortedByHeight.begin(), vSortedByHeight.end());
    for(const std::pair<int, CBlockIndex*> & item : vSortedByHeight)
    {
        CBlockIndex* pindex = item.second;
        pindex->nChainTrust = (pindex->pprev ? pindex->pprev->nChainTrust : 0) + pindex->GetBlockTrust();
    }
    
    // Load hashBestChain pointer to end of best chain
    if (!ReadHashBestChain(hashBestChain))
    {
        if (pindexGenesisBlock == NULL) {
            // empty blockchain on disk
            // prepare pegdb marks
            WriteBlockIndexIsPegReady(true);
            WriteTxIndexIsV1Ready(true);
            return true;
        }
        return error("CTxDB::LoadBlockIndex() : hashBestChain not loaded");
    }
    if (!mapBlockIndex.count(hashBestChain))
        return error("CTxDB::LoadBlockIndex() : hashBestChain not found in the block index");
    pindexBest = mapBlockIndex.ref(hashBestChain);
    nBestHeight = pindexBest->nHeight;
    nBestChainTrust = pindexBest->nChainTrust;

    LogPrintf("LoadBlockIndex(): hashBestChain=%s  height=%d  trust=%s  date=%s\n",
      hashBestChain.ToString(), nBestHeight, CBigNum(nBestChainTrust).ToString(),
      DateTimeStrFormat("%x %H:%M:%S", pindexBest->GetBlockTime()));

    // Load bnBestInvalidTrust, OK if it doesn't exist
    CBigNum bnBestInvalidTrust;
    ReadBestInvalidTrust(bnBestInvalidTrust);
    nBestInvalidTrust = bnBestInvalidTrust.getuint256();

    // Check if need to update txindexes
    bool fTxIndexIsV1Ready = false;
    if (!ReadTxIndexIsV1Ready(fTxIndexIsV1Ready)) {
        fTxIndexIsV1Ready = false;
    }
    if (!fTxIndexIsV1Ready) {
        if (!SetTxIndexesV1(*this, load_msg))
            return error("LoadBlockIndex() : SetTxIndexesReadyForPeg failed");
    }
    
    // Verify blocks in the best chain
    int nCheckLevel = GetArg("-checklevel", 1);
    int nCheckDepth = GetArg( "-checkblocks", 500);
    if (nCheckDepth == 0)
        nCheckDepth = 1000000000; // suffices until the year 19000
    if (nCheckDepth > nBestHeight)
        nCheckDepth = nBestHeight;
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CBlockIndex* pindexFork = NULL;
    map<pair<unsigned int, unsigned int>, CBlockIndex*> mapBlockPos;
    for (CBlockIndex* pindex = pindexBest; pindex && pindex->pprev; pindex = pindex->pprev)
    {
        boost::this_thread::interruption_point();
        if (pindex->nHeight < nBestHeight-nCheckDepth)
            break;
        CBlock block;
        if (!block.ReadFromDisk(pindex))
            return error("LoadBlockIndex() : block.ReadFromDisk failed");
        // check level 1: verify block validity
        // check level 7: verify block signature too
        if (nCheckLevel>0 && !block.CheckBlock(true, true, (nCheckLevel>6)))
        {
            LogPrintf("LoadBlockIndex() : *** found bad block at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
            pindexFork = pindex->pprev;
        }
        // check level 2: verify transaction index validity
        if (nCheckLevel>1)
        {
            pair<unsigned int, unsigned int> pos = make_pair(pindex->nFile, pindex->nBlockPos);
            mapBlockPos[pos] = pindex;
            for(const CTransaction &tx : block.vtx)
            {
                uint256 hashTx = tx.GetHash();
                CTxIndex txindex;
                if (ReadTxIndex(hashTx, txindex))
                {
                    // check level 3: checker transaction hashes
                    if (nCheckLevel>2 || pindex->nFile != txindex.pos.nFile || pindex->nBlockPos != txindex.pos.nBlockPos)
                    {
                        // either an error or a duplicate transaction
                        CTransaction txFound;
                        if (!txFound.ReadFromDisk(txindex.pos))
                        {
                            LogPrintf("LoadBlockIndex() : *** cannot read mislocated transaction %s\n", hashTx.ToString());
                            pindexFork = pindex->pprev;
                        }
                        else
                            if (txFound.GetHash() != hashTx) // not a duplicate tx
                            {
                                LogPrintf("LoadBlockIndex(): *** invalid tx position for %s\n", hashTx.ToString());
                                pindexFork = pindex->pprev;
                            }
                    }
                    // check level 4: check whether spent txouts were spent within the main chain
                    unsigned int nOutput = 0;
                    if (nCheckLevel>3)
                    {
                        for(const CDiskTxPos &txpos : txindex.vSpent)
                        {
                            if (!txpos.IsNull())
                            {
                                pair<unsigned int, unsigned int> posFind = make_pair(txpos.nFile, txpos.nBlockPos);
                                if (!mapBlockPos.count(posFind))
                                {
                                    LogPrintf("LoadBlockIndex(): *** found bad spend at %d, hashBlock=%s, hashTx=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString(), hashTx.ToString());
                                    pindexFork = pindex->pprev;
                                }
                                // check level 6: check whether spent txouts were spent by a valid transaction that consume them
                                if (nCheckLevel>5)
                                {
                                    CTransaction txSpend;
                                    if (!txSpend.ReadFromDisk(txpos))
                                    {
                                        LogPrintf("LoadBlockIndex(): *** cannot read spending transaction of %s:%i from disk\n", hashTx.ToString(), nOutput);
                                        pindexFork = pindex->pprev;
                                    }
                                    else if (!txSpend.CheckTransaction())
                                    {
                                        LogPrintf("LoadBlockIndex(): *** spending transaction of %s:%i is invalid\n", hashTx.ToString(), nOutput);
                                        pindexFork = pindex->pprev;
                                    }
                                    else
                                    {
                                        bool fFound = false;
                                        for(const CTxIn &txin : txSpend.vin) {
                                            if (txin.prevout.hash == hashTx && txin.prevout.n == nOutput)
                                                fFound = true;
                                        }
                                        if (!fFound)
                                        {
                                            LogPrintf("LoadBlockIndex(): *** spending transaction of %s:%i does not spend it\n", hashTx.ToString(), nOutput);
                                            pindexFork = pindex->pprev;
                                        }
                                    }
                                }
                            }
                            nOutput++;
                        }
                    }
                }
                // check level 5: check whether all prevouts are marked spent
                if (nCheckLevel>4)
                {
                     for(const CTxIn &txin : tx.vin)
                     {
                          CTxIndex txindex;
                          if (ReadTxIndex(txin.prevout.hash, txindex))
                              if (txindex.vSpent.size()-1 < txin.prevout.n || txindex.vSpent[txin.prevout.n].IsNull())
                              {
                                  LogPrintf("LoadBlockIndex(): *** found unspent prevout %s:%i in %s\n", txin.prevout.hash.ToString(), txin.prevout.n, hashTx.ToString());
                                  pindexFork = pindex->pprev;
                              }
                     }
                }
            }
        }
    }
    { // check hard checkpoints
        CBlockIndex* pindexLast = NULL;
        vector<int> vHeights = Checkpoints::GetCheckpointsHeights();
        for(int nHeight : vHeights) {
            CBlockIndex* pindex = FindBlockByHeight(nHeight);
            if (!pindex) {
                break;
            }
            bool ok = Checkpoints::CheckHardened(nHeight, pindex->GetBlockHash());
            if (!ok) {
                pindexFork = pindexLast;
                // trigger peg rebuild
                if (!WritePegCheck(PEG_DB_CHECK_ON_FORK, false))
                    return error("WritePegCheck() : flag3 write failed");
                break;
            }
            pindexLast = pindex;
            load_msg(" checkpoint: "+std::to_string(pindex->nHeight));
        }
    }
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
    
    return true;
}

bool CTxDB::ReadUtxoDbEnabled(bool& fEnabled)
{
#ifdef ENABLE_EXPLORER
    fEnabled = true;
    return true;
#else
    return Read(string("utxoDbEnabled"), fEnabled);
#endif
}

bool CTxDB::WriteUtxoDbEnabled(bool fEnabled)
{
    return Write(string("utxoDbEnabled"), fEnabled);
}

bool CTxDB::ReadUtxoDbIsReady(bool& bReady)
{
    return Read(string("utxoDbIsReady"), bReady);
}

bool CTxDB::WriteUtxoDbIsReady(bool bReady)
{
    return Write(string("utxoDbIsReady"), bReady);
}

bool CTxDB::ReadAddressLastBalance(string sAddress, CAddressBalance & balance, int64_t & nIdx)
{
    nIdx = -1;
    string sNum = strprintf("%016x", 0);
    string sStartKey = "addr"+sAddress+sNum;
    string sRawKey;
    string sRawValue;
    if (!Seek(sStartKey, sRawKey, sRawValue))
        return false;
    
    bool fFound = false;
    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.write(sRawKey.data(), sRawKey.size());
    string sKey;
    ssKey >> sKey;
    if (boost::starts_with(sKey, "addr"+sAddress)) {
        sNum = sKey.substr(4+34);
        std::istringstream(sNum) >> std::hex >> nIdx;
        nIdx = INT64_MAX-nIdx;
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        ssValue.write(sRawValue.data(), sRawValue.size());
        ssValue >> balance;
        fFound = true;
    }
    return fFound;
}

// warning: this method use disk Seek and ignores current batch
bool CTxDB::ReadAddressBalanceRecords(string sAddress, vector<CAddressBalance> & vRecords)
{
    bool fFound = false;
    leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
    string sNum = strprintf("%016x", 0);
    CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
    ssStartKey << "addr"+sAddress+sNum;
    iterator->Seek(ssStartKey.str());
    while (iterator->Valid()) {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.write(iterator->key().data(), iterator->key().size());
        string sKey;
        ssKey >> sKey;
        if (boost::starts_with(sKey, "addr"+sAddress)) {
            CAddressBalance balance;
            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
            ssValue.write(iterator->value().data(), iterator->value().size());
            ssValue >> balance;
            vRecords.push_back(balance);
            fFound = true;
        } 
        else {
            break;
        }
        iterator->Next();
    }
    delete iterator;
    return fFound;
}

// warning: this method use disk Seek and ignores current batch
bool CTxDB::ReadAddressUnspent(string sAddress, vector<CAddressUnspent> & vRecords)
{
    bool fFound = false;
    leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
    string sNum = strprintf("%080x", 0);
    CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
    ssStartKey << "utxo"+sAddress+sNum;
    iterator->Seek(ssStartKey.str());
    while (iterator->Valid()) {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.write(iterator->key().data(), iterator->key().size());
        string sKey;
        ssKey >> sKey;
        if (boost::starts_with(sKey, "utxo"+sAddress)) {
            CAddressUnspent utxo;
            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
            ssValue.write(iterator->value().data(), iterator->value().size());
            ssValue >> utxo;
            string txoutidhex = sKey.substr(4+34, 80);
            utxo.txoutid = uint320(txoutidhex);
            vRecords.push_back(utxo);
            fFound = true;
        } 
        else {
            break;
        }
        iterator->Next();
    }
    delete iterator;
    return fFound;
}

// warning: this method use disk Seek and ignores current batch
bool CTxDB::ReadAddressFrozen(string sAddress, vector<CAddressUnspent> & vRecords)
{
    bool fFound = false;
    leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
    string sNum = strprintf("%080x", 0);
    CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
    ssStartKey << "ftxo"+sAddress+sNum;
    iterator->Seek(ssStartKey.str());
    while (iterator->Valid()) {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.write(iterator->key().data(), iterator->key().size());
        string sKey;
        ssKey >> sKey;
        if (boost::starts_with(sKey, "ftxo"+sAddress)) {
            CAddressUnspent utxo;
            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
            ssValue.write(iterator->value().data(), iterator->value().size());
            ssValue >> utxo;
            string txoutidhex = sKey.substr(4+34, 80);
            utxo.txoutid = uint320(txoutidhex);
            vRecords.push_back(utxo);
            fFound = true;
        } 
        else {
            break;
        }
        iterator->Next();
    }
    delete iterator;
    return fFound;
}

bool CTxDB::ReadFrozenQueue(uint64_t nLockTime, vector<CFrozenQueued> & records)
{
    string sMinTime = strprintf("%016x", 0);
    string sMaxTime = strprintf("%016x", nLockTime);
    string sMinTxout = uint320().GetHex();
    string sMaxTxout = uint320_MAX.GetHex();
    vector<std::pair<string, CFrozenQueued> > values;
    string sMinKey = "fqueue"+sMinTime+sMinTxout;
    string sMaxKey = "fqueue"+sMaxTime+sMaxTxout;
    bool fFound = Range(sMinKey, sMaxKey, values);
    records.resize(values.size());
    for(size_t i=0; i< values.size(); i++) {
        string sLockTimeHex = values[i].first.substr(6,16);
        string sTxoutidHex = values[i].first.substr(6+16,80);
        records[i].sAddress = values[i].second.sAddress;
        records[i].nAmount = values[i].second.nAmount;
        records[i].txoutid = uint320(sTxoutidHex);
        std::istringstream(sLockTimeHex) >> std::hex >> records[i].nLockTime;
    }
    return fFound;
}

bool CTxDB::ReadFrozenQueued(uint64_t nLockTime, uint320 txoutid, CFrozenQueued & record)
{
    string sTime = strprintf("%016x", nLockTime);
    string sTxout = txoutid.GetHex();
    string sKey = "fqueue"+sTime+sTxout;
    return Read(sKey, record);
}

bool CTxDB::CleanupUtxoData(LoadMsg load_msg)
{
    // remove old balance records
    {
        leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
        string sNum = strprintf("%016x", 0);
        CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
        string sStart = strprintf("%034x", 0);
        ssStartKey << "addr"+sStart+sNum;
        iterator->Seek(ssStartKey.str());
        int n =0;
        while (iterator->Valid()) {
            if (n % 10000 == 0) {
                load_msg(std::string(" cleanup #1: ")+std::to_string(n));
            }
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            ssKey.write(iterator->key().data(), iterator->key().size());
            string sKey;
            ssKey >> sKey;
            if (boost::starts_with(sKey, "addr")) {
                string sDeleteKey = iterator->key().ToString();
                iterator->Next();
                pdb->Delete(leveldb::WriteOptions(), sDeleteKey);
                n++;
                continue;
            }
            else {
                break;
            }
            iterator->Next();
            n++;
        }
        delete iterator;
    }
    // remove old utxo records
    {
        leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
        string sTxout = strprintf("%080x", 0); // 256+64
        CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
        string sStart = strprintf("%034x", 0);
        ssStartKey << "utxo"+sStart+sTxout;
        iterator->Seek(ssStartKey.str());
        int n =0;
        while (iterator->Valid()) {
            if (n % 10000 == 0) {
                load_msg(std::string(" cleanup #2: ")+std::to_string(n));
            }
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            ssKey.write(iterator->key().data(), iterator->key().size());
            string sKey;
            ssKey >> sKey;
            if (boost::starts_with(sKey, "utxo")) {
                string sDeleteKey = iterator->key().ToString();
                iterator->Next();
                pdb->Delete(leveldb::WriteOptions(), sDeleteKey);
                n++;
                continue;
            }
            else {
                break;
            }
            iterator->Next();
            n++;
        }
        delete iterator;
    }
    // remove old frozen records
    {
        leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
        string sTxout = strprintf("%080x", 0); // 256+64
        CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
        string sStart = strprintf("%034x", 0);
        ssStartKey << "ftxo"+sStart+sTxout;
        iterator->Seek(ssStartKey.str());
        int n =0;
        while (iterator->Valid()) {
            if (n % 10000 == 0) {
                load_msg(std::string(" cleanup #3: ")+std::to_string(n));
            }
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            ssKey.write(iterator->key().data(), iterator->key().size());
            string sKey;
            ssKey >> sKey;
            if (boost::starts_with(sKey, "ftxo")) {
                string sDeleteKey = iterator->key().ToString();
                iterator->Next();
                pdb->Delete(leveldb::WriteOptions(), sDeleteKey);
                n++;
                continue;
            }
            else {
                break;
            }
            iterator->Next();
            n++;
        }
        delete iterator;
    }
    // remove old frozen queue records
    {
        leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
        string sTime = strprintf("%016x", 0);
        string sTxout = strprintf("%080x", 0); // 256+64
        CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
        ssStartKey << "fqueue"+sTime+sTxout;
        iterator->Seek(ssStartKey.str());
        int n =0;
        while (iterator->Valid()) {
            if (n % 10000 == 0) {
                load_msg(std::string(" cleanup #4: ")+std::to_string(n));
            }
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            ssKey.write(iterator->key().data(), iterator->key().size());
            string sKey;
            ssKey >> sKey;
            if (boost::starts_with(sKey, "fqueue")) {
                string sDeleteKey = iterator->key().ToString();
                iterator->Next();
                pdb->Delete(leveldb::WriteOptions(), sDeleteKey);
                n++;
                continue;
            }
            else {
                break;
            }
            iterator->Next();
            n++;
        }
        delete iterator;
    }
    CleanupPegBalances(load_msg);
    return true;
}

bool CTxDB::CleanupPegBalances(LoadMsg load_msg)
{
    // remove old pegbalance records
    {
        leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
        string sStart = strprintf("%034x", 0);
        CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
        ssStartKey << "pegbalance"+sStart;
        iterator->Seek(ssStartKey.str());
        int n =0;
        while (iterator->Valid()) {
            if (n % 10000 == 0) {
                load_msg(std::string(" cleanup #5: ")+std::to_string(n));
            }
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            ssKey.write(iterator->key().data(), iterator->key().size());
            string sKey;
            ssKey >> sKey;
            if (boost::starts_with(sKey, "pegbalance")) {
                string sDeleteKey = iterator->key().ToString();
                iterator->Next();
                pdb->Delete(leveldb::WriteOptions(), sDeleteKey);
                n++;
                continue;
            }
            else {
                break;
            }
            iterator->Next();
            n++;
        }
        delete iterator;
    }
    return true;
}

bool CTxDB::LoadUtxoData(LoadMsg load_msg)
{
    bool fIsReady = false;
    bool fEnabled = false; // default
     
    ReadUtxoDbIsReady(fIsReady);
    ReadUtxoDbEnabled(fEnabled);

//    fIsReady = false;
//    fEnabled = true;
    
    set<string> setSkipAddresses;
    setSkipAddresses.insert(Params().PegInflateAddr());
    setSkipAddresses.insert(Params().PegDeflateAddr());
    setSkipAddresses.insert(Params().PegNochangeAddr());
    
    if (!fIsReady && fEnabled) {
        // remove all first
        CleanupUtxoData(load_msg);
        // pegdb is ready
        CPegDB pegdb("r");
        // over all blocks
        
        CBlockIndex* pindex = pindexGenesisBlock;
        while (pindex)
        {
            if (pindex->nHeight % 1000 == 0) {
                load_msg(std::string(" balances changes: ")+std::to_string(pindex->nHeight));
            }
            CBlock block;
            if (!block.ReadFromDisk(pindex, true)) 
                return error("LoadUtxoData() : block ReadFromDisk failed");
            
            // fill address map
            for(size_t i=0; i < block.vtx.size(); i++)
            {
                const CTransaction& tx = block.vtx[i];
                MapPrevTx mapInputs;
                MapFractions mapInputsFractions;
                for(size_t j =0; j < tx.vin.size(); j++) {
                    if (tx.IsCoinBase()) continue;
                    const COutPoint & prevout = tx.vin[j].prevout;
                    if (prevout.hash == uint256(0)) continue;
                    CTxIndex& prevtxindex = mapInputs[prevout.hash].first;
                    if (!ReadTxIndex(prevout.hash, prevtxindex))
                        return error("LoadUtxoData() : ReadTxIndex failed");
                    CTransaction& prev = mapInputs[prevout.hash].second;
                    if(!ReadDiskTx(prevout.hash, prev))
                        return error("LoadUtxoData() : ReadDiskTx failed");
                    // Read input fractions
                    auto txoutid = uint320(prevout.hash, prevout.n);
                    CFractions& fractions = mapInputsFractions[txoutid];
                    fractions = CFractions(0, CFractions::VALUE);
                    if (!pegdb.ReadFractions(txoutid, fractions, true)) { // must_have
                        mapInputsFractions.erase(txoutid);
                    }
                }
                MapFractions mapOutputsFractions;
                for(size_t j =0; j < tx.vout.size(); j++) {
                    // Read output fractions
                    auto txoutid = uint320(tx.GetHash(), j);
                    CFractions& fractions = mapOutputsFractions[txoutid];
                    fractions = CFractions(0, CFractions::VALUE);
                    if (!pegdb.ReadFractions(txoutid, fractions, true)) { // must_have
                        mapOutputsFractions.erase(txoutid);
                    }
                }
                if (!tx.ConnectUtxo(*this, pindex, i, mapInputs, mapInputsFractions, mapOutputsFractions))
                    return error("LoadUtxoData() : tx.ConnectUtxo failed");
            }
            MapFractions mapFractionsEmpty; // empty as all is in pegdb already
            if (!block.ProcessFrozenQueue(*this, pegdb, mapFractionsEmpty, pindex, true /*fLoading*/))
                return error("LoadUtxoData() : ConnectFrozenQueue failed");
            
            pindex = pindex->pnext;
        }
        
        // when it is ready we recalc pegbalances as if prune enabled
        // we do not have pegdatas of those txouts which were pruned
        // so all pegbalances are recalculated from unspent pegdata
        CleanupPegBalances(load_msg);
        {
            // two passes:
            // first pass to collect and add all non-peg unspents without counting peg fractions
            // secod pass to collect and add all peg-based unspent with peg append/deduct
            {
                leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
                string sTxout = strprintf("%080x", 0); // 256+64
                CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
                string sStart = strprintf("%034x", 0);
                ssStartKey << "utxo"+sStart+sTxout;
                iterator->Seek(ssStartKey.str());
                int n =0;
                while (iterator->Valid()) {
                    if (n % 10000 == 0) {
                        load_msg(std::string(" balances: ")+std::to_string(n));
                    }
                    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
                    ssKey.write(iterator->key().data(), iterator->key().size());
                    string sKey;
                    ssKey >> sKey;
                    if (boost::starts_with(sKey, "utxo")) {
                        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
                        ssValue.write(iterator->value().data(), iterator->value().size());
                        CAddressUnspent unspent;
                        ssValue >> unspent;
                        string sAddress = sKey.substr(4,34);
                        
                        CFractions fractions(unspent.nAmount, CFractions::VALUE);
                        bool peg_on = unspent.nHeight >= nPegStartHeight;
                        if (!peg_on) {
                            if (!AppendUnspent(sAddress, fractions, true /*still sumup as pegbased*/))
                                return error("LoadUtxoData() : AppendUnspent failed");
                        }
                        
                        iterator->Next();
                        n++;
                        continue;
                    }
                    else {
                        break;
                    }
                    iterator->Next();
                    n++;
                }
                delete iterator;
            }
            // peg-based
            {
                leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
                string sTxout = strprintf("%080x", 0); // 256+64
                CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
                string sStart = strprintf("%034x", 0);
                ssStartKey << "utxo"+sStart+sTxout;
                iterator->Seek(ssStartKey.str());
                int n =0;
                while (iterator->Valid()) {
                    if (n % 10000 == 0) {
                        load_msg(std::string(" balances: ")+std::to_string(n));
                    }
                    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
                    ssKey.write(iterator->key().data(), iterator->key().size());
                    string sKey;
                    ssKey >> sKey;
                    if (boost::starts_with(sKey, "utxo")) {
                        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
                        ssValue.write(iterator->value().data(), iterator->value().size());
                        CAddressUnspent unspent;
                        ssValue >> unspent;
                        string sAddress = sKey.substr(4,34);
                        string sTxoutidHex = sKey.substr(4+34,80);
                        uint320 txoutid(sTxoutidHex);
                        
                        CFractions fractions(unspent.nAmount, CFractions::VALUE);
                        bool peg_on = unspent.nHeight >= nPegStartHeight;
                        if (peg_on) {
                            if (!setSkipAddresses.count(sAddress))
                                if (!pegdb.ReadFractions(txoutid, fractions, true /*must_have*/))
                                    return error("LoadUtxoData() : ReadFractions failed");
                            if (!AppendUnspent(sAddress, fractions, peg_on))
                                return error("LoadUtxoData() : AppendUnspent failed");
                        }
                        
                        iterator->Next();
                        n++;
                        continue;
                    }
                    else {
                        break;
                    }
                    iterator->Next();
                    n++;
                }
                delete iterator;
            }
        }
        
        boost::this_thread::interruption_point();
        
        // utxo db is ready for use
        WriteUtxoDbIsReady(true);
    }
    
    if (fIsReady && !fEnabled) {
        CleanupUtxoData(load_msg);
        // turn off as ready, new blocks are not processed
        WriteUtxoDbIsReady(false);
    }
    
    return true;
}

bool CTxDB::DeductSpent(std::string sAddress, const CFractions & fractions, bool peg_on) {
    CFractions base(0, CFractions::VALUE);
    std::string strValue;
    if (ReadStr("pegbalance"+sAddress, strValue)) {
        CDataStream finp(strValue.data(), strValue.data() + strValue.size(),
                         SER_DISK, CLIENT_VERSION);
        if (!base.Unpack(finp))
            return false;
    }
    if (!peg_on) {
        base = CFractions(base.Total() - fractions.Total(), CFractions::VALUE);
    } else {
        base -= fractions;
    }
    CDataStream fout(SER_DISK, CLIENT_VERSION);
    base.Pack(fout, nullptr, false /*compress*/);
    return Write("pegbalance"+sAddress, fout);
}

bool CTxDB::AppendUnspent(std::string sAddress, const CFractions & fractions, bool peg_on) {
    CFractions base(0, CFractions::VALUE);
    std::string strValue;
    if (ReadStr("pegbalance"+sAddress, strValue)) {
        CDataStream finp(strValue.data(), strValue.data() + strValue.size(),
                         SER_DISK, CLIENT_VERSION);
        if (!base.Unpack(finp))
            return false;
    }
    if (!peg_on) {
        base = CFractions(base.Total() + fractions.Total(), CFractions::VALUE);
    } else {
        base += fractions;
    }
    CDataStream fout(SER_DISK, CLIENT_VERSION);
    base.Pack(fout, nullptr, false /*compress*/);
    return Write("pegbalance"+sAddress, fout);
}

bool CTxDB::ReadPegBalance(std::string sAddress, CFractions & fractions)
{
    fractions = CFractions(0, CFractions::VALUE);
    std::string strValue;
    if (ReadStr("pegbalance"+sAddress, strValue)) {
        CDataStream finp(strValue.data(), strValue.data() + strValue.size(),
                         SER_DISK, CLIENT_VERSION);
        if (!fractions.Unpack(finp))
            return false;
    }
    return true;
}
