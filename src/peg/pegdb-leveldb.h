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

class CPegDB {
public:
	CPegDB(const char* pszMode = "r+");
	~CPegDB() {
		// Note that this is not the same as Close() because it deletes only
		// data scoped to this TxDB object.
		delete activeBatch;
	}

	// Destroys the underlying shared global state accessed by this TxDB.
	void Close();

	bool ReadFractions(uint320 txout, CFractions&, bool must_have = false);
	bool WriteFractions(uint320 txout, const CFractions&);

private:
	leveldb::DB* pdb;  // Points to the global instance.

	// A batch stores up writes and deletes for atomic application. When this
	// field is non-NULL, writes/deletes go there instead of directly to disk.
	leveldb::WriteBatch* activeBatch;
	leveldb::Options     options;
	bool                 fReadOnly;
	int                  nVersion;

protected:
	// Returns true and sets (value,false) if activeBatch contains the given key
	// or leaves value alone and sets deleted = true if activeBatch contains a
	// delete for it.
	bool ScanBatch(const CDataStream& key, std::string* value, bool* deleted) const;

	template <typename K, typename T>
	bool Read(const K& key, T& value) {
		CDataStream ssKey(SER_DISK, CLIENT_VERSION);
		ssKey.reserve(1000);
		ssKey << key;
		std::string strValue;

		bool readFromDb = true;
		if (activeBatch) {
			// First we must search for it in the currently pending set of
			// changes to the db. If not found in the batch, go on to read disk.
			bool deleted = false;
			readFromDb   = ScanBatch(ssKey, &strValue, &deleted) == false;
			if (deleted) {
				return false;
			}
		}
		if (readFromDb) {
			leveldb::Status status = pdb->Get(leveldb::ReadOptions(), ssKey.str(), &strValue);
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
			CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK,
								CLIENT_VERSION);
			ssValue >> value;
		} catch (std::exception& e) {
			return false;
		}
		return true;
	}
	template <typename K>
	bool ReadStr(const K& key, std::string& strValue) {
		CDataStream ssKey(SER_DISK, CLIENT_VERSION);
		ssKey.reserve(1000);
		ssKey << key;

		bool readFromDb = true;
		if (activeBatch) {
			// First we must search for it in the currently pending set of
			// changes to the db. If not found in the batch, go on to read disk.
			bool deleted = false;
			readFromDb   = ScanBatch(ssKey, &strValue, &deleted) == false;
			if (deleted) {
				return false;
			}
		}
		if (readFromDb) {
			leveldb::Status status = pdb->Get(leveldb::ReadOptions(), ssKey.str(), &strValue);
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
	template <typename K, typename T>
	bool Write(const K& key, const T& value) {
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

	template <typename K>
	bool Erase(const K& key) {
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

	template <typename K>
	bool Exists(const K& key) {
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
	bool TxnAbort() {
		delete activeBatch;
		activeBatch = NULL;
		return true;
	}

	bool ReadVersion(int& nVersion) {
		nVersion = 0;
		return Read(std::string("version"), nVersion);
	}

	bool WriteVersion(int nVersion) { return Write(std::string("version"), nVersion); }

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

	// inits
	bool TrustedStakers1Init();
	bool TrustedStakers2Init();
	bool ProposalConsensusInit();
	bool BridgesInit();
	bool MerklesInit();
	bool TimeLockPassesInit();

	// not use "cycle" but blockhash
	bool ReadCycleStateHash(uint256                           bhash_cycle,
							CChainParams::AcceptedStatesTypes typ,
							uint256&                          hash);
	bool WriteCycleStateHash(uint256                           bhash_cycle,
							 CChainParams::AcceptedStatesTypes typ,
							 const uint256&                    hash);

	bool ReadCycleStateData1(const uint256& hash, std::set<string>& results);
	bool WriteCycleStateData1(const std::set<string>& datas, uint256& written_hash);

	bool ReadCycleStateData2(uint256& hash, std::map<int, CChainParams::ConsensusVotes>& datas);
	bool WriteCycleStateData2(const std::map<int, CChainParams::ConsensusVotes>& datas,
							  uint256&                                           written_hash);

	bool ReadCycleStateData3(uint256& hash, std::map<std::string, std::vector<string>>& datas);
	bool WriteCycleStateData3(const std::map<std::string, std::vector<string>>& datas,
							  uint256&                                          written_hash);

	bool AppendCycleStateMapItem1(const uint256&     block_hash,
								  const std::string& map_id,
								  uint256            map_end,
								  const std::string& map_item_key,
								  const std::string& map_item_data,
								  uint256&           written_hash);
	bool ReadMapItemBlockHash1(const std::string& map_id,
							   const std::string& map_item_key,
							   uint256&           block_hash);
	bool ReadMapItemData1(const std::string& map_id,
						  const std::string& map_item_key,
						  std::string&       data);
	bool ReadMapItem1(const uint256&     map_item_shash,
					  const std::string& map_id,
					  int&               map_item_index,
					  std::string&       map_item_key,
					  uint256&           map_prev_shash,
					  std::string&       map_item_data);

	bool ReadCycleProposals(const uint256& bhash_cycle, std::set<string>& datas);
	bool WriteCycleProposals(const uint256& bhash_cycle, const std::set<string>& datas);

	bool ReadProposal(const std::string& proposal_hash, std::vector<std::string>& datas);
	bool WriteProposal(const std::string& proposal_phash, const std::vector<std::string>& datas);

	bool ReadProposalBlockVotes(const uint256&         bhash,
								const std::string&     phash,
								std::set<std::string>& datas);
	bool WriteProposalBlockVotes(const uint256&               bhash,
								 const std::string&           phash,
								 const std::set<std::string>& datas);

	bool ReadProposalBlockVoteStaker(const std::string& txoutid, std::string& staker_addr);
	bool WriteProposalBlockVoteStaker(const std::string& txoutid, const std::string& staker_addr);

	bool ReadBlockBurnsToBridge(const uint256&         bhash,
								const std::string&     br_hash,
								std::set<std::string>& datas);
	bool WriteBlockBurnsToBridge(const uint256&               bhash,
								 const std::string&           br_hash,
								 const std::set<std::string>& datas);

	bool ReadBridgeCycleBridgeHashes(const uint256& bcycle_hash, std::set<std::string>& br_hashes);
	bool WriteBridgeCycleBridgeHashes(const uint256&               bcycle_hash,
									  const std::set<std::string>& br_hashes);

	bool ReadBridgeCycleBurnsToBridge(const uint256&         bcycle_hash,
									  const std::string&     br_hash,
									  std::set<std::string>& datas);
	bool WriteBridgeCycleBurnsToBridge(const uint256&               bcycle_hash,
									   const std::string&           br_hash,
									   const std::set<std::string>& datas);

	bool ReadBridgeCycleMerkle(const uint256&     bcycle_hash,
							   const std::string& br_hash,
							   std::string&       merkle_data);
	bool WriteBridgeCycleMerkle(const uint256&     bcycle_hash,
								const std::string& br_hash,
								const std::string& merkle_data);
};

#endif  // BITCOIN_PEG_LEVELDB_H
