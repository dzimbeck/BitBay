// Copyright (c) 2009-2012 The Bitcoin Developers.
// Copyright (c) 2020 yshurik
// Authored by Google, Inc.
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LEVELDB_H
#define BITCOIN_LEVELDB_H

#include "main.h"

#include <map>
#include <string>
#include <vector>

#include <leveldb/db.h>
#include <leveldb/write_batch.h>

// Class that provides access to a LevelDB. Note that this class is frequently
// instantiated on the stack and then destroyed again, so instantiation has to
// be very cheap. Unfortunately that means, a CTxDB instance is actually just a
// wrapper around some global state.
//
// A LevelDB is a key/value store that is optimized for fast usage on hard
// disks. It prefers long read/writes to seeks and is based on a series of
// sorted key/value mapping files that are stacked on top of each other, with
// newer files overriding older files. A background thread compacts them
// together when too many files stack up.
//
// Learn more: http://code.google.com/p/leveldb/
class CTxDB
{
public:
    CTxDB(const char* pszMode="r+");
    ~CTxDB() {
        // Note that this is not the same as Close() because it deletes only
        // data scoped to this TxDB object.
        delete activeBatch;
    }

    // Destroys the underlying shared global state accessed by this TxDB.
    void Close();

    struct cmpBySlice {
        bool operator()(const std::string& a, const std::string& b) const {
            leveldb::Slice sa(a);
            leveldb::Slice sb(b);
            int cmp = sa.compare(sb);
            return cmp < 0;
        }
    };
    
private:
    leveldb::DB *pdb;  // Points to the global instance.

    // A batch stores up writes and deletes for atomic application. When this
    // field is non-NULL, writes/deletes go there instead of directly to disk.
    leveldb::WriteBatch *activeBatch;
    leveldb::Options options;
    bool fReadOnly;
    int nVersion;

protected:
    
    // Returns true and sets (value,false) if activeBatch contains the given key
    // or leaves value alone and sets deleted = true if activeBatch contains a
    // delete for it.
    bool ScanBatch(const CDataStream &key, std::string *value, bool *deleted) const;
    bool SeekBatch(const CDataStream &fromkey, 
                   std::string *key, std::string *value, 
                   std::map<std::string,std::string, CTxDB::cmpBySlice> *seekmap,
                   std::set<std::string> *erased) const;
    bool SeekBatch(const CDataStream &fromkey, 
                   const CDataStream &tokey, 
                   std::map<std::string,std::string, CTxDB::cmpBySlice> *seekmap,
                   std::set<std::string> *erased) const;

    template<typename K>
    bool Seek(const K& fromkey, std::string & rawkey, std::string & rawvalue)
    {
        CDataStream ssFromKey(SER_DISK, CLIENT_VERSION);
        ssFromKey.reserve(1000);
        ssFromKey << fromkey;
        
        std::string strBKey;
        std::string strBValue;
        bool foundInBatch = false;
        std::set<std::string> erasedKeys;
        std::map<std::string,std::string, CTxDB::cmpBySlice> seekmap;
        // First we must search for it in the currently pending set of
        // changes to the db. Then go on to read disk and compare which is to use.
        if (activeBatch) {
            foundInBatch = SeekBatch(ssFromKey, &strBKey, &strBValue, &seekmap, &erasedKeys);
        }

        std::string strDKey;
        std::string strDValue;
        bool foundOnDisk = false;
        leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
        iterator->Seek(ssFromKey.str());
        if (!iterator->Valid()) {
            if (!foundInBatch) {
                delete iterator;
                return false; // not found
            }
        }
        while(iterator->Valid()) {
            strDKey = iterator->key().ToString();
            strDValue = iterator->value().ToString();
            if (!erasedKeys.count(strDKey)) {
                foundOnDisk = true;
                break;
            }
            iterator->Next();
        }
        delete iterator;
        
        if (foundInBatch && !foundOnDisk) {
            rawkey = strBKey;
            rawvalue = strBValue;
        } else if (!foundInBatch && foundOnDisk) {
            rawkey = strDKey;
            rawvalue = strDValue;
        } else if (!foundInBatch && !foundOnDisk) {
            return false; // not found
        } else if (foundInBatch && foundOnDisk) {
            leveldb::Slice sbkey(strBKey);
            leveldb::Slice sdkey(strDKey);
            if (sbkey.compare(sdkey) <= 0) {
                // ==, batch has fresh value
                rawkey = strBKey;
                rawvalue = strBValue;
            } else {
                rawkey = strDKey;
                rawvalue = strDValue;
            }
        }
        
        return true;
    }
    
    template<typename K, typename T>
    bool Range(const K& fromkey, const K& tokey, std::vector<std::pair<K,T>>& values)
    {
        values.clear();
        CDataStream ssFromKey(SER_DISK, CLIENT_VERSION);
        ssFromKey.reserve(1000);
        ssFromKey << fromkey;
        CDataStream ssToKey(SER_DISK, CLIENT_VERSION);
        ssToKey.reserve(1000);
        ssToKey << tokey;
        
        auto push_back = [&](std::string rawkey, std::string rawvalue) {
            values.resize(values.size()+1);
            CDataStream ssKey(rawkey.data(), rawkey.data() + rawkey.size(),
                                SER_DISK, CLIENT_VERSION);
            ssKey >> values.back().first;
            CDataStream ssValue(rawvalue.data(), rawvalue.data() + rawvalue.size(),
                                SER_DISK, CLIENT_VERSION);
            ssValue >> values.back().second;
        };
        
        bool foundInBatch = false;
        std::set<std::string> erasedKeys;
        std::map<std::string,std::string, CTxDB::cmpBySlice> seekmap;
        // First we must search for it in the currently pending set of
        // changes to the db. Then go on to read disk and compare which is to use.
        if (activeBatch) {
            foundInBatch = SeekBatch(ssFromKey, ssToKey, &seekmap, &erasedKeys);
        }
        
        leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
        iterator->Seek(ssFromKey.str());
        if (!iterator->Valid()) {
            if (!foundInBatch) {
                delete iterator;
                return false; // not found
            }
        }
        // to merge with batch
        while(iterator->Valid()) {
            string strDKey = iterator->key().ToString();
            if (erasedKeys.count(strDKey)) {
                iterator->Next();
                continue;
            }
            if (leveldb::Slice(strDKey).compare(leveldb::Slice(ssToKey.str())) > 0) {
                break;
            }
            // first add lower keys from batch
            bool skipDValue = false;
            if (!seekmap.empty()) {
                string strBKey = seekmap.begin()->first;
                while (leveldb::Slice(strBKey).compare(leveldb::Slice(strDKey)) <=0) {
                    if (strBKey == strDKey)
                        skipDValue = true;
                    string strBValue = seekmap.begin()->second;
                    push_back(strBKey, strBValue);
                    // next
                    seekmap.erase(strBKey);
                    if (seekmap.empty())
                        break;
                    strBKey = seekmap.begin()->first;
                }
            }
            // add disk value if not in batch
            if (!skipDValue) {
                string strDValue = iterator->value().ToString();
                push_back(strDKey, strDValue);
            }
            iterator->Next();
        }
        delete iterator;
        //remains upper keys in batch
        while(!seekmap.empty()) {
            string strBKey = seekmap.begin()->first;
            string strBValue = seekmap.begin()->second;
            push_back(strBKey, strBValue);
            seekmap.erase(strBKey);
        }
        
        return !values.empty();
    }
    
    template<typename K, typename T>
    bool Read(const K& key, T& value)
    {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        std::string strValue;

        bool readFromDb = true;
        if (activeBatch) {
            // First we must search for it in the currently pending set of
            // changes to the db. If not found in the batch, go on to read disk.
            bool deleted = false;
            readFromDb = ScanBatch(ssKey, &strValue, &deleted) == false;
            if (deleted) {
                return false;
            }
        }
        if (readFromDb) {
            leveldb::Status status = pdb->Get(leveldb::ReadOptions(),
                                              ssKey.str(), &strValue);
            if (!status.ok()) {
                if (status.IsNotFound())
                    return false;
                // Some unexpected error.
                LogPrintf("LevelDB read failure: %s\n", status.ToString());
                return false;
            }
        }
        // Unserialize value
        try {
            CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(),
                                SER_DISK, CLIENT_VERSION);
            ssValue >> value;
        }
        catch (std::exception &e) {
            return false;
        }
        return true;
    }
    template<typename K>
    bool ReadStr(const K& key, std::string& strValue)
    {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;

        bool readFromDb = true;
        if (activeBatch) {
            // First we must search for it in the currently pending set of
            // changes to the db. If not found in the batch, go on to read disk.
            bool deleted = false;
            readFromDb = ScanBatch(ssKey, &strValue, &deleted) == false;
            if (deleted) {
                return false;
            }
        }
        if (readFromDb) {
            leveldb::Status status = pdb->Get(leveldb::ReadOptions(),
                                              ssKey.str(), &strValue);
            if (!status.ok()) {
                if (status.IsNotFound())
                    return false;
                // Some unexpected error.
                LogPrintf("LevelDB read failure: %s\n", status.ToString());
                return false;
            }
        }
        return true;
    }

    template<typename K, typename T>
    bool Write(const K& key, const T& value)
    {
        if (fReadOnly)
            assert(!"Write called on database in read-only mode");

        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        ssValue.reserve(10000);
        ssValue << value;

        if (activeBatch) {
            activeBatch->Put(ssKey.str(), ssValue.str());
            return true;
        }
        leveldb::Status status = pdb->Put(leveldb::WriteOptions(), ssKey.str(), ssValue.str());
        if (!status.ok()) {
            LogPrintf("LevelDB write failure: %s\n", status.ToString());
            return false;
        }
        return true;
    }

    template<typename K>
    bool Erase(const K& key)
    {
        if (!pdb)
            return false;
        if (fReadOnly)
            assert(!"Erase called on database in read-only mode");

        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        if (activeBatch) {
            activeBatch->Delete(ssKey.str());
            return true;
        }
        leveldb::Status status = pdb->Delete(leveldb::WriteOptions(), ssKey.str());
        return (status.ok() || status.IsNotFound());
    }

    template<typename K>
    bool Exists(const K& key)
    {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        std::string unused;

        if (activeBatch) {
            bool deleted;
            if (ScanBatch(ssKey, &unused, &deleted) && !deleted) {
                return true;
            }
        }


        leveldb::Status status = pdb->Get(leveldb::ReadOptions(), ssKey.str(), &unused);
        return status.IsNotFound() == false;
    }


public:
    bool TxnBegin();
    bool TxnCommit();
    bool TxnAbort()
    {
        delete activeBatch;
        activeBatch = NULL;
        return true;
    }

    bool ReadVersion(int& nVersion)
    {
        nVersion = 0;
        return Read(std::string("version"), nVersion);
    }

    bool WriteVersion(int nVersion)
    {
        return Write(std::string("version"), nVersion);
    }

    static CBlockIndex *InsertBlockIndex(uint256 hash);

    bool ReadTxIndex(uint256 hash, CTxIndex& txindex);
    bool UpdateTxIndex(uint256 hash, const CTxIndex& txindex);
    bool AddTxIndex(const CTransaction& tx, const CDiskTxPos& pos, int64_t nHeight, uint16_t nTxIndex);
    bool EraseTxIndex(const CTransaction& tx);
    bool ContainsTx(uint256 hash);
    bool ReadDiskTx(uint256 hash, CTransaction& tx, CTxIndex& txindex);
    bool ReadDiskTx(uint256 hash, CTransaction& tx);
    bool ReadDiskTx(COutPoint outpoint, CTransaction& tx, CTxIndex& txindex);
    bool ReadDiskTx(COutPoint outpoint, CTransaction& tx);
    bool WriteBlockIndex(const CDiskBlockIndex& blockindex);
    bool ReadHashBestChain(uint256& hashBestChain);
    bool WriteHashBestChain(uint256 hashBestChain);
    bool ReadBestInvalidTrust(CBigNum& bnBestInvalidTrust);
    bool WriteBestInvalidTrust(CBigNum bnBestInvalidTrust);
    bool LoadBlockIndex(LoadMsg load_msg);
    bool LoadUtxoData(LoadMsg load_msg);
    bool CleanupUtxoData(LoadMsg load_msg);
    bool CleanupPegBalances(LoadMsg load_msg);
    
    // flags for peg system peg
    bool ReadPegStartHeight(int& nHeight);
    bool WritePegStartHeight(int nHeight);

    bool ReadTxIndexIsV1Ready(bool& bReady);
    bool WriteTxIndexIsV1Ready(bool bReady);
    
    bool ReadBlockIndexIsPegReady(bool& bReady);
    bool WriteBlockIndexIsPegReady(bool bReady);
    
    bool ReadPegCheck(int nCheck, bool& bReady);
    bool WritePegCheck(int nCheck, bool bReady);
    
    bool ReadPegPruneEnabled(bool& fEnabled);
    bool WritePegPruneEnabled(bool fEnabled);

    bool ReadUtxoDbIsReady(bool& bReady);
    bool WriteUtxoDbIsReady(bool bReady);

    bool ReadUtxoDbEnabled(bool& fEnabled);
    bool WriteUtxoDbEnabled(bool fEnabled);
    
    bool ReadAddressLastBalance(string addr, CAddressBalance & balance, int64_t & nIdx);
    bool ReadFrozenQueue(uint64_t nLockTime, std::vector<CFrozenQueued> &);
    bool ReadFrozenQueued(uint64_t nLockTime, uint320 txoutid, CFrozenQueued &);

    bool AddUnspent(std::string sAddress, uint320 txoutid, const CAddressUnspent & utxo) {
        string sTxout = txoutid.GetHex();
        return Write("utxo"+sAddress+sTxout, utxo);
    }
    bool ReadUnspent(std::string sAddress, uint320 txoutid, CAddressUnspent & utxo) {
        string sTxout = txoutid.GetHex();
        return Read("utxo"+sAddress+sTxout, utxo);
    }
    bool EraseUnspent(std::string sAddress, uint320 txoutid) {
        string sTxout = txoutid.GetHex();
        return Erase("utxo"+sAddress+sTxout);
    }
    bool AddFrozen(std::string sAddress, uint320 txoutid, const CAddressUnspent & ftxo) {
        string sTxout = txoutid.GetHex();
        return Write("ftxo"+sAddress+sTxout, ftxo);
    }
    bool ReadFrozen(std::string sAddress, uint320 txoutid, CAddressUnspent & ftxo) {
        string sTxout = txoutid.GetHex();
        return Read("ftxo"+sAddress+sTxout, ftxo);
    }
    bool EraseFrozen(std::string sAddress, uint320 txoutid) {
        string sTxout = txoutid.GetHex();
        return Erase("ftxo"+sAddress+sTxout);
    }
    bool AddBalance(std::string sAddress, int64_t nIndex, const CAddressBalance & balance) {
        string sRIndex = strprintf("%016x", INT64_MAX-nIndex);
        return Write("addr"+sAddress+sRIndex, balance);
    }
    bool EraseBalance(std::string sAddress, int64_t nIndex) {
        string sRIndex = strprintf("%016x", INT64_MAX-nIndex);
        return Erase("addr"+sAddress+sRIndex);
    }
    bool AddToFrozenQueue(uint64_t nLockTime, uint320 txoutid, const CFrozenQueued & record) {
        string sTime = strprintf("%016x", nLockTime);
        string sTxout = txoutid.GetHex();
        return Write("fqueue"+sTime+sTxout, record);
    }
    bool EraseFromFrozenQueue(uint64_t nLockTime, uint320 txoutid) {
        string sTime = strprintf("%016x", nLockTime);
        string sTxout = txoutid.GetHex();
        return Erase("fqueue"+sTime+sTxout);
    }
    bool DeductSpent(std::string sAddress, const CFractions & fractions, bool peg_on);
    bool AppendUnspent(std::string sAddress, const CFractions & fractions, bool peg_on);
    bool ReadPegBalance(std::string sAddress, CFractions & fractions);
    
    // warning: this method use disk Seek and ignores current batch
    bool ReadAddressBalanceRecords(string addr, vector<CAddressBalance> & records);
    // warning: this method use disk Seek and ignores current batch
    bool ReadAddressUnspent(string addr, vector<CAddressUnspent> & records);
    // warning: this method use disk Seek and ignores current batch
    bool ReadAddressFrozen(string addr, vector<CAddressUnspent> & records);
    
};

extern leveldb::DB *txdb; // global pointer for LevelDB object instance

#endif // BITCOIN_DB_H
