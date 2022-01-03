// Copyright (c) 2018 yshurik
//
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PEG_LEVELDB_H
#define BITCOIN_PEG_LEVELDB_H

#include "main.h"
#include "peg.h"

#include <map>
#include <string>
#include <vector>

#include <leveldb/db.h>
#include <leveldb/write_batch.h>

class CPegDB
{
public:
    CPegDB(const char* pszMode="r+");
    ~CPegDB() {
        // Note that this is not the same as Close() because it deletes only
        // data scoped to this TxDB object.
        delete activeBatch;
    }

    // Destroys the underlying shared global state accessed by this TxDB.
    void Close();

    bool ReadFractions(uint320 txout, CFractions &, bool must_have =false);
    bool WriteFractions(uint320 txout, const CFractions &);

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

public:
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

    bool ReadPegStartHeight(int& nHeight);
    bool WritePegStartHeight(int nHeight);

    bool ReadPegPruneEnabled(bool& fEnabled);
    bool WritePegPruneEnabled(bool fEnabled);

    bool ReadPegTxActivated(bool& fActivated);
    bool WritePegTxActivated(bool fActivated);

    bool ReadPegBayPeakRate(double& dRate);
    bool WritePegBayPeakRate(double dRate);

    bool ReadPegTxId(uint256 txid, uint256& txhash);
    bool WritePegTxId(uint256 txid, uint256 txhash);
    bool RemovePegTxId(uint256 txid);

    // load routine
    bool LoadPegData(CTxDB& txdb, LoadMsg load_msg);
};


#endif // BITCOIN_PEG_LEVELDB_H
