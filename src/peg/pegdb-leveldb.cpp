// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <map>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/version.hpp>

#include <leveldb/cache.h>
#include <leveldb/env.h>
#include <leveldb/filter_policy.h>
#include <memenv/memenv.h>

#include "base58.h"
#include "chainparams.h"
#include "kernel.h"
#include "main.h"
#include "txdb.h"
#include "util.h"

#include <fstream>
#include <iostream>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;
using namespace boost;
namespace fs = boost::filesystem;

leveldb::DB* pegdb;  // global pointer for LevelDB object instance

static leveldb::Options GetOptions() {
	leveldb::Options options;
	int              nCacheSizeMB = GetArg("-dbcache", 50);
	options.block_cache           = leveldb::NewLRUCache(nCacheSizeMB * 1048576);
	options.filter_policy         = leveldb::NewBloomFilterPolicy(10);
	return options;
}

static void init_blockindex(leveldb::Options& options,
                            bool              fRemoveOld       = false,
                            bool              fCreateBootstrap = false) {
	// First time init.
	fs::path directory = GetDataDir() / "pegleveldb";
	fs::create_directory(directory);
	LogPrintf("Opening LevelDB in %s\n", directory.string());
	leveldb::Status status = leveldb::DB::Open(options, directory.string(), &pegdb);
	if (!status.ok()) {
		throw runtime_error(strprintf("init_blockindex(): error opening database environment %s",
		                              status.ToString()));
	}
}

// CDB subclasses are created and destroyed VERY OFTEN. That's why
// we shouldn't treat this as a free operations.
CPegDB::CPegDB(const char* pszMode) {
	assert(pszMode);
	activeBatch = NULL;
	fReadOnly   = (!strchr(pszMode, '+') && !strchr(pszMode, 'w'));

	if (pegdb) {
		pdb = pegdb;
		return;
	}

	bool fCreate = strchr(pszMode, 'c');

	options                   = GetOptions();
	options.create_if_missing = fCreate;
	options.filter_policy     = leveldb::NewBloomFilterPolicy(10);

	init_blockindex(options);  // Init directory
	pdb = pegdb;

	if (Exists(string("version"))) {
		ReadVersion(nVersion);
		LogPrintf("Peg index version is %d\n", nVersion);

		if (nVersion < DATABASE_VERSION) {
			LogPrintf("Required index version is %d, removing old database\n", DATABASE_VERSION);

			// Leveldb instance destruction
			delete pegdb;
			pegdb = pdb = NULL;
			delete activeBatch;
			activeBatch = NULL;

			init_blockindex(options, true, true);  // Remove directory and create new database
			pdb = pegdb;

			bool fTmp = fReadOnly;
			fReadOnly = false;
			WriteVersion(DATABASE_VERSION);  // Save transaction index version
			fReadOnly = fTmp;
		}
	} else if (fCreate) {
		bool fTmp = fReadOnly;
		fReadOnly = false;
		WriteVersion(DATABASE_VERSION);
		fReadOnly = fTmp;
	}

	LogPrintf("Opened Peg LevelDB successfully\n");
}

void CPegDB::Close() {
	delete pegdb;
	pegdb = pdb = NULL;
	delete options.filter_policy;
	options.filter_policy = NULL;
	delete options.block_cache;
	options.block_cache = NULL;
	delete activeBatch;
	activeBatch = NULL;
}

bool CPegDB::TxnBegin() {
	assert(!activeBatch);
	activeBatch = new leveldb::WriteBatch();
	return true;
}

bool CPegDB::TxnCommit() {
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
	std::string  needle;
	bool*        deleted;
	std::string* foundValue;
	bool         foundEntry;

	CPegBatchScanner() : foundEntry(false) {}

	virtual void Put(const leveldb::Slice& key, const leveldb::Slice& value) {
		if (key.ToString() == needle) {
			foundEntry  = true;
			*deleted    = false;
			*foundValue = value.ToString();
		}
	}

	virtual void Delete(const leveldb::Slice& key) {
		if (key.ToString() == needle) {
			foundEntry = true;
			*deleted   = true;
		}
	}
};

// When performing a read, if we have an active batch we need to check it first
// before reading from the database, as the rest of the code assumes that once
// a database transaction begins reads are consistent with it. It would be good
// to change that assumption in future and avoid the performance hit, though in
// practice it does not appear to be large.
bool CPegDB::ScanBatch(const CDataStream& key, string* value, bool* deleted) const {
	assert(activeBatch);
	*deleted = false;
	CPegBatchScanner scanner;
	scanner.needle         = key.str();
	scanner.deleted        = deleted;
	scanner.foundValue     = value;
	leveldb::Status status = activeBatch->Iterate(&scanner);
	if (!status.ok()) {
		throw runtime_error(status.ToString());
	}
	return scanner.foundEntry;
}

bool CPegDB::ReadFractions(uint320 txout, CFractions& f, bool must_have) {
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
	CDataStream finp(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
	return f.Unpack(finp);
}
bool CPegDB::WriteFractions(uint320 txout, const CFractions& f) {
	CDataStream fout(SER_DISK, CLIENT_VERSION);
	f.Pack(fout);
	return Write(txout, fout);
}

bool CPegDB::ReadPegStartHeight(int& nHeight) {
	return Read(string("pegStartHeight"), nHeight);
}

bool CPegDB::WritePegStartHeight(int nHeight) {
	return Write(string("pegStartHeight"), nHeight);
}

bool CPegDB::ReadPegPruneEnabled(bool& fEnabled) {
	return Read(string("pegPruneEnabled"), fEnabled);
}

bool CPegDB::WritePegPruneEnabled(bool fEnabled) {
	return Write(string("pegPruneEnabled"), fEnabled);
}

bool CPegDB::ReadPegTxActivated(bool& fActivated) {
	return Read(string("pegTxActivated"), fActivated);
}

bool CPegDB::WritePegTxActivated(bool fActivated) {
	return Write(string("pegTxActivated"), fActivated);
}

bool CPegDB::ReadPegBayPeakRate(double& dRate) {
	return Read(string("pegBayPeakRate"), dRate);
}

bool CPegDB::WritePegBayPeakRate(double dRate) {
	return Write(string("pegBayPeakRate"), dRate);
}

bool CPegDB::WritePegTxId(uint256 txid, uint256 txhash) {
	return Write(txid, txhash);
}

bool CPegDB::ReadPegTxId(uint256 txid, uint256& txhash) {
	return Read(txid, txhash);
}

bool CPegDB::RemovePegTxId(uint256 txid) {
	return Erase(txid);
}

// std::map<string, int> stake_addr_stats;

bool CPegDB::TrustedStakers1Init() {
	// to init for cycle ref = nPegStartHeight -1
	if (pindexBest == NULL)
		return true;  // cold start

	if (!TxnBegin())
		return error("TrustedStakers1Init() : TxnBegin failed");

	// block just before peg start
	int          nHeight     = nPegStartHeight - 1;
	CBlockIndex* pblockindex = pindexBest;
	while (pblockindex && pblockindex->nHeight > nHeight)
		pblockindex = pblockindex->Prev();
	uint256 bhash = pblockindex->PegCycleBlock()->GetBlockHash();
	uint256 shash;
	if (!WriteCycleStateData1(Params().sTrustedStakers1Init, shash))
		return error("TrustedStakers1Init() : WriteCycleStateData1 failed");
	if (!WriteCycleStateHash(bhash, CChainParams::ACCEPTED_TSTAKERS1, shash))
		return error("TrustedStakers1Init() : WriteCycleStateHash failed");

	if (!TxnCommit())
		return error("TrustedStakers1Init() : TxnCommit failed");

	return true;
}

bool CPegDB::TrustedStakers2Init() {
	if (pindexBest == NULL)
		return true;  // cold start

	if (!TxnBegin())
		return error("TrustedStakers2Init() : TxnBegin failed");

	// block just before peg start
	int          nHeight     = nPegStartHeight - 1;
	CBlockIndex* pblockindex = pindexBest;
	while (pblockindex && pblockindex->nHeight > nHeight)
		pblockindex = pblockindex->Prev();
	uint256 bhash = pblockindex->PegCycleBlock()->GetBlockHash();
	uint256 shash;
	if (!WriteCycleStateData1(Params().sTrustedStakers2Init, shash))
		return error("TrustedStakers2Init() : WriteCycleStateData1 failed");
	if (!WriteCycleStateHash(bhash, CChainParams::ACCEPTED_TSTAKERS2, shash))
		return error("TrustedStakers2Init() : WriteCycleStateHash failed");

	if (!TxnCommit())
		return error("TrustedStakers2Init() : TxnCommit failed");

	return true;
}

bool CPegDB::ProposalConsensusInit() {
	if (pindexBest == NULL)
		return true;  // cold start

	if (!TxnBegin())
		return error("ProposalConsensusInit() : TxnBegin failed");

	// block just before peg start
	int          nHeight     = nPegStartHeight - 1;
	CBlockIndex* pblockindex = pindexBest;
	while (pblockindex && pblockindex->nHeight > nHeight)
		pblockindex = pblockindex->Prev();
	uint256 bhash = pblockindex->PegCycleBlock()->GetBlockHash();
	uint256 shash;
	if (!WriteCycleStateData2(Params().mapProposalConsensusInit, shash))
		return error("ProposalConsensusInit() : WriteCycleStateData2 failed");
	if (!WriteCycleStateHash(bhash, CChainParams::ACCEPTED_CONSENSUS, shash))
		return error("ProposalConsensusInit() : WriteCycleStateHash failed");

	if (!TxnCommit())
		return error("ProposalConsensusInit() : TxnCommit failed");

	return true;
}

bool CPegDB::BridgesInit() {
	if (pindexBest == NULL)
		return true;  // cold start

	if (!TxnBegin())
		return error("BridgesInit() : TxnBegin failed");

	// block just before peg start
	int          nHeight     = nPegStartHeight - 1;
	CBlockIndex* pblockindex = pindexBest;
	while (pblockindex && pblockindex->nHeight > nHeight)
		pblockindex = pblockindex->Prev();
	uint256                     bhash = pblockindex->PegCycleBlock()->GetBlockHash();
	uint256                     shash;
	map<string, vector<string>> empty;
	if (!WriteCycleStateData3(empty, shash))
		return error("BridgesInit() : WriteCycleStateData3 failed");
	;
	if (!WriteCycleStateHash(bhash, CChainParams::ACCEPTED_BRIDGES, shash))
		return error("BridgesInit() : WriteCycleStateHash failed");
	;

	if (!TxnCommit())
		return error("BridgesInit() : TxnCommit failed");

	return true;
}

bool CPegDB::MerklesInit() {
	if (pindexBest == NULL)
		return true;  // cold start

	if (!TxnBegin())
		return error("MerklesInit() : TxnBegin failed");
	// block just before peg start
	int          nHeight     = nPegStartHeight - 1;
	CBlockIndex* pblockindex = pindexBest;
	while (pblockindex && pblockindex->nHeight > nHeight)
		pblockindex = pblockindex->Prev();
	uint256 bhash = pblockindex->PegCycleBlock()->GetBlockHash();
	uint256 shash;
	if (!WriteCycleStateHash(bhash, CChainParams::ACCEPTED_MERKLES, shash))
		return error("MerklesInit() : WriteCycleStateHash failed");
	if (!WriteCycleStateHash(bhash, CChainParams::ACCEPTED_BRIDGES_PAUSE, shash))
		return error("MerklesInit() : WriteCycleStateHash failed2");
	if (!TxnCommit())
		return error("MerklesInit() : TxnCommit failed");

	return true;
}

bool CPegDB::TimeLockPassesInit() {
	if (pindexBest == NULL)
		return true;  // cold start

	if (!TxnBegin())
		return error("TimeLockPassesInit() : TxnBegin failed");

	// block just before peg start
	int          nHeight     = nPegStartHeight - 1;
	CBlockIndex* pblockindex = pindexBest;
	while (pblockindex && pblockindex->nHeight > nHeight)
		pblockindex = pblockindex->Prev();
	uint256 bhash = pblockindex->PegCycleBlock()->GetBlockHash();
	uint256 shash;
	if (!WriteCycleStateData1(Params().setTimeLockPassesInit, shash))
		return error("TimeLockPassesInit() : WriteCycleStateData1 failed");
	;
	if (!WriteCycleStateHash(bhash, CChainParams::ACCEPTED_TIMELOCKPASSES, shash))
		return error("TimeLockPassesInit() : WriteCycleStateHash failed");
	;

	if (!TxnCommit())
		return error("TimeLockPassesInit() : TxnCommit failed");

	return true;
}

bool CPegDB::LoadPegData(CTxDB& txdb, LoadMsg load_msg) {
	// For Peg System activated via TX
	CTxIndex txindex;
	if (txdb.ReadTxIndex(Params().PegActivationTxhash(), txindex)) {
		LogPrintf("LoadPegData() : peg activation tx is found\n");
		uint32_t nTxNum = 0;
		uint256  blockhash;
		int      nTxHeight =
		    txindex.GetHeightInMainChain(&nTxNum, Params().PegActivationTxhash(), &blockhash);
		LogPrintf("LoadPegData() : peg activation tx is height: %d\n", nTxHeight);
		if (nTxHeight > 0) {
			if (nTxHeight < nBestHeight - 100) {
				LogPrintf("LoadPegData() : peg activation tx is deep: %d\n",
				          nBestHeight - nTxHeight);
				int nPegToStart      = ((nTxHeight + 500) / 1000 + 1) * 1000;
				nPegStartHeight      = nPegToStart;
				fPegIsActivatedViaTx = true;
				LogPrintf("LoadPegData() : peg to start: %d\n", nPegToStart);
				if (!txdb.TxnBegin())
					return error("LoadPegData() : TxnBegin failed");
				if (!txdb.WritePegStartHeight(nPegStartHeight))
					return error("LoadPegData() : flag write failed");
				if (!txdb.TxnCommit())
					return error("LoadPegData() : TxnCommit failed");
				if (nPegStartHeight > nBestHeight) {
					strMiscWarning = "Warning : Peg system has activation at block: " +
					                 std::to_string(nPegStartHeight);
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
			if (!txdb.WritePegCheck(PEG_DB_CHECK_BRIDGE1, false))
				return error("WritePegCheck() : flag4 write failed");
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

	{  // all is ready, store nPegStartHeight
		if (!txdb.TxnBegin())
			return error("WriteBlockIndexIsPegReady() : TxnBegin failed");
		if (!txdb.WritePegStartHeight(nPegStartHeight))
			return error("WritePegStartHeight() : flag write failed");
		if (!txdb.TxnCommit())
			return error("WriteBlockIndexIsPegReady() : TxnCommit failed");
	}

	CPegDB& pegdb = *this;
	// now process pegdb & votes if not ready
	{
		bool fPegCheck1 = false;
		txdb.ReadPegCheck(PEG_DB_CHECK1, fPegCheck1);

		bool fPegCheck2 = false;
		txdb.ReadPegCheck(PEG_DB_CHECK2, fPegCheck2);

		bool fPegCheck3 = false;
		txdb.ReadPegCheck(PEG_DB_CHECK_ON_FORK, fPegCheck3);

		bool fPegCheck4 = false;
		txdb.ReadPegCheck(PEG_DB_CHECK_BRIDGE1, fPegCheck4);

		bool fPegPruneStored = true;
		if (!pegdb.ReadPegPruneEnabled(fPegPruneStored)) {
			fPegPruneStored = true;
		}

		int nPegStartHeightStored = 0;
		pegdb.ReadPegStartHeight(nPegStartHeightStored);
		if (nPegStartHeightStored != nPegStartHeight || fPegPruneStored != fPegPruneEnabled ||
		    !fPegCheck1 || !fPegCheck2 || !fPegCheck3 || !fPegCheck4) {
			// reprocess from nPegStartHeight

			if (!TrustedStakers1Init())
				return error("TrustedStakers1Init() : init failed");
			if (!TrustedStakers2Init())
				return error("TrustedStakers2Init() : init failed");
			if (!ProposalConsensusInit())
				return error("ProposalConsensusInit() : init failed");
			if (!BridgesInit())
				return error("BridgesInit() : init failed");
			if (!MerklesInit())
				return error("MerklesInit() : init failed");
			if (!TimeLockPassesInit())
				return error("TimeLockPassesInit() : init failed");

			// back to nPegStartHeight
			string       sBlockindexPegFailCause;
			CBlockIndex* pBlockindexPegFail = nullptr;
			CBlockIndex* pBlockindex        = pindexBest;
			while (pBlockindex && pBlockindex->nHeight > nPegStartHeight)
				pBlockindex = pBlockindex->Prev();

			CBlock block;
			while (pBlockindex && pBlockindex->nHeight >= nPegStartHeight &&
			       pBlockindex->nHeight <= nBestHeight) {
				uint256 hash = *pBlockindex->phashBlock;
				pBlockindex  = mapBlockIndex.ref(hash);

				if (pBlockindex->nHeight % 100 == 0) {
					load_msg(std::string(" process peg fractions: ") +
					         std::to_string(pBlockindex->nHeight));
				}

				// at very beginning have peg supply index and accepted proposals datas
				if (!CalculateBlockPegIndex(*this, pBlockindex))
					return error("CalculateBlockPegIndex() : failed supply index computation");
				if (!ConnectConsensusStates(*this, pBlockindex))
					return error("ConnectConsensusStates() : fail to connect consensus states");

				if (!block.ReadFromDisk(pBlockindex, true))
					return error("ReadFromDisk() : block read failed");

				map<string, CBridgeInfo> bridges;
				if (!pBlockindex->ReadBridges(*this, bridges))
					return error("ReadBridges() : bridges read failed");
				auto fnMerkleIn = [&](string hash) {
					CMerkleInfo m = pBlockindex->ReadMerkleIn(pegdb, hash);
					return m;
				};
				set<string> sTimeLockPassesPubkeys;
				if (!pBlockindex->ReadTimeLockPasses(*this, sTimeLockPassesPubkeys))
					return error("ReadTimeLockPasses() : timelockpasses read failed");

				int64_t      nFees        = 0;
				int64_t      nStakeReward = 0;
				CFractions   feesFractions;
				MapFractions mapQueuedFractionsChanges;

				if (!FetchBridgePoolsFractions(*this, pBlockindex, mapQueuedFractionsChanges))
					return error("ConnectBridgeFractions() : fail to connect bridge fractions");
				if (!ConnectBridgeCycleBurns(*this, pBlockindex, mapQueuedFractionsChanges))
					return error(
					    "ConnectBridgeCycleBurns() : fail to connect bridge burn fractions");

				int  nBridgePoolNout        = pBlockindex->nHeight;
				bool fBridgePoolFromChanges = true;

				for (CTransaction& tx : block.vtx) {
					MapPrevTx              mapInputs;
					MapFractions           mapInputsFractions;
					map<uint256, CTxIndex> mapUnused;
					string                 sPegFailCause;
					bool                   fInvalid = false;
					// fetch inputs only to calculate peg fractions, apply (is block) to read the
					// previous transactions only from disk but no lookup into the mempool
					if (!tx.FetchInputs(txdb, pegdb, nBridgePoolNout, fBridgePoolFromChanges,
					                    bridges, fnMerkleIn, mapUnused, mapQueuedFractionsChanges,
					                    true /*is block*/, false /*is miner*/, block.nTime,
					                    false /*skip pruned*/, mapInputs, mapInputsFractions,
					                    fInvalid))
						return error("LoadBlockIndex() : FetchInputs/pegdb failed");

					int64_t nTxValueIn  = tx.GetValueIn(mapInputs);
					int64_t nTxValueOut = tx.GetValueOut();

					if (!tx.IsCoinStake())
						nFees += nTxValueIn - nTxValueOut;
					if (tx.IsCoinStake())
						nStakeReward = nTxValueOut - nTxValueIn;

					if (tx.IsCoinStake())
						continue;
					if (tx.IsCoinBase())
						continue;

					bool peg_ok = true;
					if (tx.IsCoinMint()) {
						peg_ok = CalculateCoinMintFractions(
						    tx, pBlockindex->nPegSupplyIndex, pBlockindex->nTime, bridges,
						    fnMerkleIn, nBridgePoolNout, mapInputs, mapInputsFractions,
						    mapQueuedFractionsChanges, feesFractions, sPegFailCause);
						if (!peg_ok) {
							pBlockindexPegFail      = pBlockindex;
							sBlockindexPegFailCause = sPegFailCause;
						}
					}

					if (!tx.IsCoinMint()) {
						set<uint32_t> sTimeLockPassInputs;
						size_t        n_vin = tx.vin.size();
						for (uint32_t i = 0; i < n_vin; i++) {
							// check sigs to collect pubkeys
							const COutPoint& prevout = tx.vin[i].prevout;
							CTransaction&    txPrev  = mapInputs[prevout.hash].second;
							set<vchtype>     sSignedPubks;
							if (!VerifySignature(txPrev, tx, i, STANDARD_SCRIPT_VERIFY_FLAGS, 0,
							                     sSignedPubks)) {
								return error("LoadBlockIndex() : %s VerifySignature failed",
								             tx.GetHash().ToString());
							}
							// Get whitelisted timelocks
							for (const vchtype& pubkey_vch : sSignedPubks) {
								CPubKey pubkey(pubkey_vch);
								string  pubkey_txt = HexStr(pubkey.begin(), pubkey.end());
								if (sTimeLockPassesPubkeys.count(pubkey_txt)) {
									sTimeLockPassInputs.insert(i);
								}
							}
						}
						peg_ok = CalculateStandardFractions(
						    tx, pBlockindex->nPegSupplyIndex, pBlockindex->nTime, mapInputs,
						    mapInputsFractions, sTimeLockPassInputs, mapQueuedFractionsChanges,
						    feesFractions, sPegFailCause);
					}
					if (!peg_ok) {
						pBlockindexPegFail      = pBlockindex;
						sBlockindexPegFailCause = sPegFailCause;
					} else {
						// Write queued fractions changes
						for (MapFractions::iterator mi = mapQueuedFractionsChanges.begin();
						     mi != mapQueuedFractionsChanges.end(); ++mi) {
							if (!pegdb.WriteFractions((*mi).first, (*mi).second))
								return error("LoadBlockIndex() : pegdb Write failed");
						}
					}
				}

				if (block.vtx.size() > 1 && block.vtx[1].IsCoinStake()) {
					CTransaction& tx_coin_stake = block.vtx[1];

					MapPrevTx              mapInputs;
					MapFractions           mapInputsFractions;
					map<uint256, CTxIndex> mapUnused;
					string                 sPegFailCause;
					bool                   fInvalid = false;
					// fetch inputs only to calculate peg fractions, apply (is block) to read the
					// previous transactions only from disk but no lookup into the mempool
					if (!tx_coin_stake.FetchInputs(txdb, pegdb, 0, false, bridges, fnMerkleIn,
					                               mapUnused, mapQueuedFractionsChanges,
					                               true /*is block*/, false /*is miner*/,
					                               block.nTime, false /*skip pruned*/, mapInputs,
					                               mapInputsFractions, fInvalid))
						return error("LoadBlockIndex() : FetchInputs/pegdb failed (stake)");

					size_t n_vin = tx_coin_stake.vin.size();
					if (n_vin < 1) {
						return error((std::string("LoadBlockIndex() : pegdb failed: less than one "
						                          "input in stake: ") +
						              std::to_string(pBlockindex->nHeight))
						                 .c_str());
					}

					const COutPoint& prevout = tx_coin_stake.vin[0].prevout;
					auto             fkey    = uint320(prevout.hash, prevout.n);
					if (mapInputsFractions.find(fkey) == mapInputsFractions.end()) {
						return error("LoadBlockIndex() : pegdb failed: no input fractions found");
					}

					int64_t nCalculatedStakeReward = 0;
					if (!GetProofOfStakeReward(txdb, tx_coin_stake, pBlockindex->Prev(), nFees,
					                           mapInputsFractions[fkey], nCalculatedStakeReward)) {
						return error("LoadBlockIndex() : pegdb: GetProofOfStakeReward() failed");
					}
					int64_t nStakeRewardWithoutFees = 0;
					if (!GetProofOfStakeReward(txdb, tx_coin_stake, pBlockindex->Prev(), 0 /*fees*/,
					                           mapInputsFractions[fkey], nStakeRewardWithoutFees)) {
						return error("LoadBlockIndex() : pegdb: GetProofOfStakeReward() failed");
					}

					if (nStakeReward > nCalculatedStakeReward) {
						pBlockindexPegFail      = pBlockindex;
						sBlockindexPegFailCause = "Stake reward mismatch";
						LogPrintf(
						    "LoadBlockIndex() : *** Stake reward mismatch at block %d: %d vs %d\n",
						    pBlockindex->nHeight, nStakeReward, nCalculatedStakeReward);
					}

					bool peg_ok = CalculateStakingFractions(
					    tx_coin_stake, pBlockindex, mapInputs, mapInputsFractions, mapUnused,
					    mapQueuedFractionsChanges, feesFractions, nStakeRewardWithoutFees,
					    sPegFailCause);
					if (!peg_ok) {
						pBlockindexPegFail      = pBlockindex;
						sBlockindexPegFailCause = sPegFailCause;
					}
				}

				// if peg violation then no writing fraction and no write block index
				// and break the loop to move best chain back to preivous
				if (pBlockindexPegFail) {
					break;
				}

				// Write queued fractions changes
				for (MapFractions::iterator mi = mapQueuedFractionsChanges.begin();
				     mi != mapQueuedFractionsChanges.end(); ++mi) {
					if (!pegdb.WriteFractions((*mi).first, (*mi).second))
						return error("LoadBlockIndex() : pegdb Write failed");
				}

				if (fPegPruneEnabled) {
					// Prune old spent fractions, back to index
					int nHeightPrune = pBlockindex->nHeight - PEG_PRUNE_INTERVAL;
					if (nHeightPrune > 0 && nHeightPrune >= nPegStartHeight) {
						auto pindexprune = pBlockindex;
						while (pindexprune && pindexprune->nHeight > nHeightPrune)
							pindexprune = pindexprune->Prev();
						if (pindexprune) {
							CBlock blockprune;
							if (blockprune.ReadFromDisk(pindexprune->nFile, pindexprune->nBlockPos,
							                            true /*vtx*/)) {
								PrunePegForBlock(blockprune, pegdb);
							}
						}
					}
				}

				string staker_addr;

				if (!CalculateBlockPegVotes(block, pBlockindex, txdb, pegdb, staker_addr))
					return error("CalculateBlockPegVotes() : failed");

				if (!CalculateBlockBridgeVotes(block, pBlockindex, txdb, pegdb, staker_addr))
					return error("CalculateBlockBridgeVotes() : failed");

				if (!txdb.WriteBlockIndex(CDiskBlockIndex(pBlockindex)))
					return error("WriteBlockIndex() : write failed");

				pBlockindex = pBlockindex->Next();
			}

			if (!txdb.WritePegCheck(PEG_DB_CHECK1, true))
				return error("WritePegCheck() : flag1 write failed");

			if (!txdb.WritePegCheck(PEG_DB_CHECK2, true))
				return error("WritePegCheck() : flag2 write failed");

			if (!txdb.WritePegCheck(PEG_DB_CHECK_ON_FORK, true))
				return error("WritePegCheck() : flag3 write failed");

			if (!txdb.WritePegCheck(PEG_DB_CHECK_BRIDGE1, true))
				return error("WritePegCheck() : flag4 write failed");

			if (!pegdb.WritePegStartHeight(nPegStartHeight))
				return error("WritePegStartHeight() : peg start write failed");

			if (!pegdb.WritePegTxActivated(fPegIsActivatedViaTx))
				return error("WritePegTxActivated() : peg txactivated write failed");

			if (!pegdb.WritePegPruneEnabled(fPegPruneEnabled))
				return error("WritePegPruneEnabled() : peg prune flag write failed");

			if (pBlockindexPegFail) {
				auto pindexFork = pBlockindexPegFail->Prev();
				if (pindexFork) {
					boost::this_thread::interruption_point();
					// Reorg back to the fork
					LogPrintf(
					    "LoadBlockIndex() : *** moving best chain pointer back to block %d due to: "
					    "%s\n",
					    pindexFork->nHeight, sBlockindexPegFailCause);
					CBlock block;
					if (!block.ReadFromDisk(pindexFork))
						return error("LoadBlockIndex() : block.ReadFromDisk failed");
					CTxDB  txdb;
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

// trusted stakers, consensus, etc

bool CPegDB::ReadCycleStateHash(uint256                           bhash_cycle,
                                CChainParams::AcceptedStatesTypes typ,
                                uint256&                          hash) {
	bool ok = Read("intervalStateHash_" + bhash_cycle.ToString() + strprintf("%016x", typ), hash);
	if (!ok)
		hash = uint256(0);
	return ok;
}

bool CPegDB::WriteCycleStateHash(uint256                           bhash_cycle,
                                 CChainParams::AcceptedStatesTypes typ,
                                 const uint256&                    hash) {
	return Write("intervalStateHash_" + bhash_cycle.ToString() + strprintf("%016x", typ), hash);
}

bool CPegDB::ReadCycleStateData1(const uint256& hash, std::set<std::string>& results) {
	string data_txt;
	if (Read("intervalStateData_" + hash.GetHex(), data_txt)) {
		std::set<std::string> datas;
		boost::split(datas, data_txt, boost::is_any_of(","));
		for (const string& data : datas) {
			if (data.empty())
				continue;
			results.insert(data);
		}
		return true;
	}
	return false;
}

bool CPegDB::WriteCycleStateData1(const std::set<string>& datas, uint256& written_hash) {
	std::vector<string> datas_sorted;
	for (const string& addr : datas) {
		datas_sorted.push_back(addr);
	}
	sort(datas_sorted.begin(), datas_sorted.end());

	string phash;
	{
		CDataStream ss(SER_GETHASH, 0);
		for (const string& addr : datas) {
			ss << addr;
		}
		written_hash = Hash(ss.begin(), ss.end());
	}
	string data_txt = boost::algorithm::join(datas_sorted, ",");
	return Write("intervalStateData_" + written_hash.GetHex(), data_txt);
}

bool CPegDB::WriteCycleStateData2(const std::map<int, CChainParams::ConsensusVotes>& datas,
                                  uint256&                                           written_hash) {
	std::vector<string> datas_sorted;
	CDataStream         ss(SER_GETHASH, 0);

	for (auto it : datas) {
		ss << it.first;
		ss << it.second.tstakers1;
		ss << it.second.tstakers2;
		ss << it.second.ostakers;
		vector<string> datas;
		datas.push_back(std::to_string(it.first));
		datas.push_back(std::to_string(it.second.tstakers1));
		datas.push_back(std::to_string(it.second.tstakers2));
		datas.push_back(std::to_string(it.second.ostakers));
		string consensus_txt = boost::algorithm::join(datas, ":");
		datas_sorted.push_back(consensus_txt);
	}
	written_hash = Hash(ss.begin(), ss.end());

	string data_txt = boost::algorithm::join(datas_sorted, ",");
	return Write("intervalStateData_" + written_hash.GetHex(), data_txt);
}

bool CPegDB::ReadCycleStateData2(uint256&                                     hash,
                                 std::map<int, CChainParams::ConsensusVotes>& result) {
	string data_txt;
	if (Read("intervalStateData_" + hash.GetHex(), data_txt)) {
		set<string> vdatas;
		boost::split(vdatas, data_txt, boost::is_any_of(","));
		for (const string& consensus_txt : vdatas) {
			if (consensus_txt.empty())
				continue;
			vector<string> args;
			boost::split(args, consensus_txt, boost::is_any_of(":"));
			if (args.size() != 4) {
				return false;
			}
			CChainParams::ConsensusVotes consensus = {atoi(args[1]), atoi(args[2]), atoi(args[3])};
			result[atoi(args[0])]                  = consensus;
		}
		return true;
	}
	return false;
}

bool CPegDB::WriteCycleStateData3(const std::map<std::string, std::vector<std::string>>& datas,
                                  uint256& written_hash) {
	std::vector<string> datas_sorted;
	CDataStream         ss(SER_GETHASH, 0);

	for (auto& it : datas) {
		ss << it.first;
		for (const string& arg : it.second) {
			ss << arg;
		}
		vector<string> vdatas;
		vdatas.push_back(it.first);
		for (const string& arg : it.second) {
			vdatas.push_back(arg);
		}
		string vdatas_txt = boost::algorithm::join(vdatas, "|");
		datas_sorted.push_back(vdatas_txt);
	}
	written_hash = Hash(ss.begin(), ss.end());

	string datas_txt = boost::algorithm::join(datas_sorted, ",");
	return Write("intervalStateData_" + written_hash.GetHex(), datas_txt);
}

bool CPegDB::ReadCycleStateData3(uint256&                                         hash,
                                 std::map<std::string, std::vector<std::string>>& result) {
	string datas_txt;
	if (Read("intervalStateData_" + hash.GetHex(), datas_txt)) {
		set<string> datas;
		boost::split(datas, datas_txt, boost::is_any_of(","));
		for (const string& vdatas_txt : datas) {
			if (vdatas_txt.empty())
				continue;
			vector<string> vdata;
			boost::split(vdata, vdatas_txt, boost::is_any_of("|"));
			if (vdata.size() < 1)
				continue;
			string         vdata_key = vdata[0];
			vector<string> vdata_val;
			for (size_t i = 0; i < vdata.size(); i++) {
				if (i == 0)
					continue;
				vdata_val.push_back(vdata[i]);
			}
			result[vdata_key] = vdata_val;
		}
		return true;
	}
	return false;
}

bool CPegDB::AppendCycleStateMapItem1(const uint256&     block_hash,
                                      const std::string& map_id,
                                      uint256            map_end,
                                      const std::string& map_item_key,
                                      const std::string& map_item_data,
                                      uint256&           written_hash) {
	int    index = -1;
	string prev_data;
	if (Read("intervalStateDataRef_" + map_end.GetHex(), prev_data)) {
		vector<string> prev_datas;
		boost::split(prev_datas, prev_data, boost::is_any_of(":"));
		if (prev_datas.size() == 3) {
			index = std::atoi(prev_datas.front().c_str());
		}
	}
	CDataStream ss_data(SER_GETHASH, 0);
	ss_data << map_end;
	ss_data << map_item_key;
	written_hash = Hash(ss_data.begin(), ss_data.end());
	string datas_txt;
	datas_txt += std::to_string(index + 1);
	datas_txt += ":";
	datas_txt += EncodeBase64(map_item_key);
	datas_txt += ":";
	datas_txt += map_end.GetHex();
	bool ok = Write("intervalStateDataRef_" + written_hash.GetHex(), datas_txt);
	if (!ok)
		return false;
	CDataStream ss_item(SER_GETHASH, 0);
	ss_item << map_id;
	ss_item << map_item_key;
	uint256 item_hash = Hash(ss_item.begin(), ss_item.end());
	ok                = Write("intervalStateDataKey_" + item_hash.GetHex(), block_hash.GetHex());
	if (!ok)
		return false;
	return Write("intervalStateDataItem_" + item_hash.GetHex(), map_item_data);
}

bool CPegDB::ReadMapItemBlockHash1(const std::string& map_id,
                                   const std::string& map_item_key,
                                   uint256&           block_hash) {
	CDataStream ss_item(SER_GETHASH, 0);
	ss_item << map_id;
	ss_item << map_item_key;
	uint256 item_hash = Hash(ss_item.begin(), ss_item.end());
	string  block_hash_data;
	bool    ok = Read("intervalStateDataKey_" + item_hash.GetHex(), block_hash_data);
	if (!ok)
		return false;
	block_hash = uint256(block_hash_data);
	return true;
}

bool CPegDB::ReadMapItemData1(const std::string& map_id,
                              const std::string& map_item_key,
                              std::string&       data) {
	CDataStream ss_item(SER_GETHASH, 0);
	ss_item << map_id;
	ss_item << map_item_key;
	uint256 item_hash = Hash(ss_item.begin(), ss_item.end());
	return Read("intervalStateDataItem_" + item_hash.GetHex(), data);
}

bool CPegDB::ReadMapItem1(const uint256&     map_item_shash,
                          const std::string& map_id,
                          int&               map_item_index,
                          std::string&       map_item_key,
                          uint256&           map_prev_shash,
                          std::string&       map_item_data) {
	map_prev_shash = uint256(0);
	string item_data;
	if (!Read("intervalStateDataRef_" + map_item_shash.GetHex(), item_data)) {
		return false;
	}
	vector<string> item_datas;
	boost::split(item_datas, item_data, boost::is_any_of(":"));
	if (item_datas.size() != 3) {
		return false;
	}
	map_item_index = std::atoi(item_datas.front().c_str());
	map_prev_shash = uint256(item_datas.back());
	map_item_key   = DecodeBase64(item_datas[1]);

	CDataStream ss_item(SER_GETHASH, 0);
	ss_item << map_id;
	ss_item << map_item_key;
	uint256 item_hash = Hash(ss_item.begin(), ss_item.end());
	return Read("intervalStateDataItem_" + item_hash.GetHex(), map_item_data);
}

bool CPegDB::ReadCycleProposals(const uint256& bhash_cycle, std::set<string>& results) {
	string data_txt;
	if (Read("cycleProposals_" + bhash_cycle.GetHex(), data_txt)) {
		std::set<std::string> datas;
		boost::split(datas, data_txt, boost::is_any_of(","));
		for (const string& data : datas) {
			if (data.empty())
				continue;
			results.insert(data);
		}
		return true;
	}
	return false;
}

bool CPegDB::WriteCycleProposals(const uint256& bhash_cycle, const std::set<string>& datas) {
	std::vector<string> datas_sorted;
	for (const string& addr : datas) {
		datas_sorted.push_back(addr);
	}
	sort(datas_sorted.begin(), datas_sorted.end());
	string data_txt = boost::algorithm::join(datas_sorted, ",");
	return Write("cycleProposals_" + bhash_cycle.GetHex(), data_txt);
}

bool CPegDB::ReadProposal(const string& proposal_hash, std::vector<string>& datas) {
	string data_txt;
	if (Read("proposalData_" + proposal_hash, data_txt)) {
		boost::split(datas, data_txt, boost::is_any_of(","));
		return true;
	}
	return false;
}

bool CPegDB::WriteProposal(const string& proposal_hash, const std::vector<string>& datas) {
	string data_txt = boost::algorithm::join(datas, ",");
	return Write("proposalData_" + proposal_hash, data_txt);
}

bool CPegDB::ReadProposalBlockVotes(const uint256&    bhash,
                                    const string&     phash,
                                    std::set<string>& results) {
	string data_txt;
	if (Read("proposalBlockVotes_" + bhash.GetHex() + phash, data_txt)) {
		std::set<std::string> datas;
		boost::split(datas, data_txt, boost::is_any_of(","));
		for (const string& data : datas) {
			if (data.empty())
				continue;
			results.insert(data);
		}
		return true;
	}
	return false;
}

bool CPegDB::WriteProposalBlockVotes(const uint256&          bhash,
                                     const string&           phash,
                                     const std::set<string>& datas) {
	std::vector<string> datas_sorted;
	for (const string& addr : datas) {
		datas_sorted.push_back(addr);
	}
	sort(datas_sorted.begin(), datas_sorted.end());
	string data_txt = boost::algorithm::join(datas_sorted, ",");
	return Write("proposalBlockVotes_" + bhash.GetHex() + phash, data_txt);
}

bool CPegDB::ReadProposalBlockVoteStaker(const string& txoutid, string& staker_addr) {
	return Read("proposalBlockVoteStaker_" + txoutid, staker_addr);
}

bool CPegDB::WriteProposalBlockVoteStaker(const string& txoutid, const string& staker_addr) {
	return Write("proposalBlockVoteStaker_" + txoutid, staker_addr);
}

// txouts in block

bool CPegDB::ReadBlockBurnsToBridge(const uint256&    bhash,
                                    const string&     br_hash,
                                    std::set<string>& results) {
	string data_txt;
	if (Read("blockBurnsToBridge_" + bhash.GetHex() + br_hash, data_txt)) {
		std::set<std::string> datas;
		boost::split(datas, data_txt, boost::is_any_of(","));
		for (const string& data : datas) {
			if (data.empty())
				continue;
			results.insert(data);
		}
		return true;
	}
	return false;
}

bool CPegDB::WriteBlockBurnsToBridge(const uint256&          bhash,
                                     const string&           br_hash,
                                     const std::set<string>& datas) {
	std::vector<string> datas_sorted;
	for (const string& addr : datas) {
		datas_sorted.push_back(addr);
	}
	sort(datas_sorted.begin(), datas_sorted.end());
	string data_txt = boost::algorithm::join(datas_sorted, ",");
	return Write("blockBurnsToBridge_" + bhash.GetHex() + br_hash, data_txt);
}

// bridge hashes in bridge cycle

bool CPegDB::ReadBridgeCycleBridgeHashes(const uint256& bcycle_hash, std::set<string>& br_hashes) {
	string data_txt;
	if (Read("cycleBridgeHashesToBridge_" + bcycle_hash.GetHex(), data_txt)) {
		std::set<std::string> datas;
		boost::split(datas, data_txt, boost::is_any_of(","));
		for (const string& data : datas) {
			if (data.empty())
				continue;
			br_hashes.insert(data);
		}
		return true;
	}
	return false;
}

bool CPegDB::WriteBridgeCycleBridgeHashes(const uint256&          bcycle_hash,
                                          const std::set<string>& br_hashes) {
	std::vector<string> datas_sorted;
	for (const string& addr : br_hashes) {
		datas_sorted.push_back(addr);
	}
	sort(datas_sorted.begin(), datas_sorted.end());
	string data_txt = boost::algorithm::join(datas_sorted, ",");
	return Write("cycleBridgeHashesToBridge_" + bcycle_hash.GetHex(), data_txt);
}

// txouts in bridge cycle

bool CPegDB::ReadBridgeCycleBurnsToBridge(const uint256&    bcycle_hash,
                                          const string&     br_hash,
                                          std::set<string>& results) {
	string data_txt;
	if (Read("cycleBurnsToBridge_" + bcycle_hash.GetHex() + br_hash, data_txt)) {
		std::set<std::string> datas;
		boost::split(datas, data_txt, boost::is_any_of(","));
		for (const string& data : datas) {
			if (data.empty())
				continue;
			results.insert(data);
		}
		return true;
	}
	return false;
}

bool CPegDB::WriteBridgeCycleBurnsToBridge(const uint256&          bcycle_hash,
                                           const string&           br_hash,
                                           const std::set<string>& datas) {
	std::vector<string> datas_sorted;
	for (const string& addr : datas) {
		datas_sorted.push_back(addr);
	}
	sort(datas_sorted.begin(), datas_sorted.end());
	string data_txt = boost::algorithm::join(datas_sorted, ",");
	return Write("cycleBurnsToBridge_" + bcycle_hash.GetHex() + br_hash, data_txt);
}

// merkle

bool CPegDB::ReadBridgeCycleMerkle(const uint256& bcycle_hash,
                                   const string&  br_hash,
                                   std::string&   merkle_data) {
	return Read("bridgeCycleMerkle_" + bcycle_hash.GetHex() + br_hash, merkle_data);
}

bool CPegDB::WriteBridgeCycleMerkle(const uint256&     bcycle_hash,
                                    const string&      br_hash,
                                    const std::string& merkle_data) {
	return Write("bridgeCycleMerkle_" + bcycle_hash.GetHex() + br_hash, merkle_data);
}
