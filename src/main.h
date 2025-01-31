// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2020 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_MAIN_H
#define BITCOIN_MAIN_H

#include "bignum.h"
#include "blockindexmap.h"
#include "core.h"
#include "net.h"
#include "peg.h"
#include "script.h"
#include "sync.h"
#include "txmempool.h"

#include <boost/algorithm/string/predicate.hpp>
#include <functional>
#include <list>

class CBlock;
class CBlockIndex;
class CInv;
class CKeyItem;
class CNode;
class CReserveKey;
class CWallet;

// Added
static const int LAST_POW_TIME = 1416402000;
/** The maximum allowed size for a serialized block, in bytes (network rule) */
static const uint32_t MAX_BLOCK_SIZE = 1000000;
/** The maximum size for mined blocks */
static const uint32_t MAX_BLOCK_SIZE_GEN = MAX_BLOCK_SIZE / 2;
/** The maximum size for transactions we're willing to relay/mine **/
static const uint32_t MAX_STANDARD_TX_SIZE = MAX_BLOCK_SIZE_GEN / 5;
/** The maximum allowed number of signature check operations in a block (network rule) */
static const uint32_t MAX_BLOCK_SIGOPS = MAX_BLOCK_SIZE / 50;
/** Maxiumum number of signature check operations in an IsStandard() P2SH script */
static const uint32_t MAX_P2SH_SIGOPS = 15;
/** The maximum number of sigops we're willing to relay/mine in a single tx */
static const uint32_t MAX_TX_SIGOPS = MAX_BLOCK_SIGOPS / 5;
/** The maximum number of orphan transactions kept in memory */
static const uint32_t MAX_ORPHAN_TRANSACTIONS = MAX_BLOCK_SIZE / 100;
/** Default for -maxorphanblocks, maximum number of orphan blocks kept in memory */
static const uint32_t DEFAULT_MAX_ORPHAN_BLOCKS = 3000;
/** The maximum number of entries in an 'inv' protocol message */
static const uint32_t MAX_INV_SZ = 50000;
/** Fees smaller than this (in satoshi) are considered zero fee (for transaction creation) */
static const int64_t MIN_TX_FEE = 10000;
/** Fees to be exact for coinmint transactions */
static const int64_t MINT_TX_FEE = 100000;
/** Fees smaller than this (in satoshi) are considered zero fee (for relaying) */
static const int64_t MIN_RELAY_TX_FEE = MIN_TX_FEE;
/** No amount larger than this (in satoshi) is valid */
static const int64_t MAX_MONEY = 2000000000 * COIN;
inline bool          MoneyRange(int64_t nValue) {
    return (nValue >= 0 && nValue <= MAX_MONEY);
}
/** Threshold for nLockTime: below this value it is interpreted as block number, otherwise as UNIX
 * timestamp. */
static const uint32_t LOCKTIME_THRESHOLD = 500000000;  // Tue Nov  5 00:53:20 1985 UTC
/** Peg start block */
extern int nPegStartHeight;

static const int64_t COIN_YEAR_REWARD = 1 * CENT;  // 1% per year

inline bool IsProtocolV1RetargetingFixed(int nHeight) {
	return TestNet() || nHeight > 19935;
}
inline bool IsProtocolV2(int nHeight) {
	return TestNet() || nHeight > 20000;
}
inline bool IsProtocolV3(int64_t nTime) {
	return TestNet() || nTime > 1484956800;
}
inline bool IsProtocolVS(int64_t nTime) {
	return TestNet() || nTime > 1512000000;
}
inline bool IsProtocolVP(int nHeight) {
	return nHeight >= nPegStartHeight;
}

inline int64_t FutureDriftV1(int64_t nTime) {
	return nTime + 10 * 60;
}
inline int64_t FutureDriftV2(int64_t nTime) {
	return nTime + 15;
}
inline int64_t FutureDrift(int64_t nTime, int nHeight) {
	return IsProtocolV2(nHeight) ? FutureDriftV2(nTime) : FutureDriftV1(nTime);
}

inline uint32_t GetTargetSpacing(int nHeight) {
	return IsProtocolV2(nHeight) ? 64 : 60;
}

extern void scrypt_1024_1_1_256(const char* input, char* output);

extern CScript                                  COINBASE_FLAGS;
extern CCriticalSection                         cs_main;
extern CTxMemPool                               mempool;
extern CBlockIndexMap                           mapBlockIndex;
extern std::set<std::pair<COutPoint, uint32_t>> setStakeSeen;
extern CBlockIndex*                             pindexGenesisBlock;
extern int                                      nRecommendedConfirmations;
extern uint32_t                                 nStakeMinAge;
extern uint32_t                                 nNodeLifespan;
extern int                                      nBestHeight;
extern uint256                                  nBestChainTrust;
extern uint256                                  nBestInvalidTrust;
extern uint256                                  hashBestChain;
extern CBlockIndex*                             pindexBest;
extern uint64_t                                 nLastBlockTx;
extern uint64_t                                 nLastBlockSize;
extern int64_t                                  nLastCoinStakeSearchInterval;
extern const std::string                        strMessageMagic;
extern int64_t                                  nTimeBestReceived;
extern bool                                     fImporting;
extern bool                                     fReindex;
struct COrphanBlock;
extern std::map<uint256, COrphanBlock*> mapOrphanBlocks;
extern bool                             fHaveGUI;
extern bool                             fAboutToSendGUI;

// Settings
extern bool fUseFastIndex;

// Minimum disk space required - used in CheckDiskSpace()
static const uint64_t nMinDiskSpace = 52428800;

class CReserveKey;
class CTxDB;
class CTxIndex;
class CWalletInterface;
class CPegDB;

// functors for messagings
typedef std::function<void(const std::string&)> LoadMsg;

/** Register a wallet to receive updates from core */
void RegisterWallet(CWalletInterface* pwalletIn);
/** Unregister a wallet from core */
void UnregisterWallet(CWalletInterface* pwalletIn);
/** Unregister all wallets from core */
void UnregisterAllWallets();
/** Push an updated transaction to all registered wallets */
void SyncWithWallets(const CTransaction& tx,
                     const CBlock*       pblock,
                     bool                fConnect,
                     MapFractions&       mapQueuedFractionsChanges);
/** Ask wallets to resend their transactions */
void ResendWalletTransactions(bool fForce = false);

/** Register with a network node to receive its signals */
void RegisterNodeSignals(CNodeSignals& nodeSignals);
/** Unregister a network node */
void UnregisterNodeSignals(CNodeSignals& nodeSignals);

void PushGetBlocks(CNode* pnode, CBlockIndex* pindexBegin, uint256 hashEnd);

bool         ProcessBlock(CNode* pfrom, CBlock* pblock);
bool         CheckDiskSpace(uint64_t nAdditionalBytes = 0);
FILE*        OpenBlockFile(uint32_t nFile, uint32_t nBlockPos, const char* pszMode = "rb");
FILE*        AppendBlockFile(uint32_t& nFileRet);
bool         LoadBlockIndex(LoadMsg fLoadMsg, bool fAllowNew = true);
void         PrintBlockTree();
CBlockIndex* FindBlockByHeight(int nHeight);
bool         ProcessMessages(CNode* pfrom);
bool         SendMessages(CNode* pto, bool fSendTrickle);
void         ThreadImport(std::vector<boost::filesystem::path> vImportFiles);

bool               CheckProofOfWork(uint256 hash, uint32_t nBits);
uint32_t           GetNextTargetRequired(const CBlockIndex* pindexLast, bool fProofOfStake);
int64_t            GetProofOfWorkReward(int64_t nFees);
int64_t            GetProofOfStakeReward(CTxDB&              txdb,
                                         const CTransaction& tx_coin_stake,
                                         const CBlockIndex*  pindexPrev,
                                         int64_t             nFees,
                                         const CFractions&   inp,
                                         int64_t&            nReward);
bool               IsInitialBlockDownload();
bool               IsConfirmedInNPrevBlocks(const CTxIndex&    txindex,
                                            const CBlockIndex* pindexFrom,
                                            int                nMaxDepth,
                                            int&               nActualDepth);
std::string        GetWarnings(std::string strFor);
bool               GetTransaction(const uint256& hash, CTransaction& tx, uint256& hashBlock);
uint256            WantedByOrphan(const COrphanBlock* pblockOrphan);
const CBlockIndex* GetLastBlockIndex(const CBlockIndex* pindex, bool fProofOfStake);
void               ThreadStakeMiner(CWallet* pwallet);
void               ThreadBrigeAuto1(CWallet* pwallet);
void               ThreadBrigeAuto2(CWallet* pwallet);

/** (try to) add transaction to memory pool **/
bool AcceptToMemoryPool(CTxMemPool& pool, CTransaction& tx, bool fLimitFree, bool* pfMissingInputs);

/** Position on disk for a particular transaction. */
class CDiskTxPos {
public:
	uint32_t nFile;
	uint32_t nBlockPos;
	uint32_t nTxPos;

	CDiskTxPos() { SetNull(); }

	CDiskTxPos(uint32_t nFileIn, uint32_t nBlockPosIn, uint32_t nTxPosIn) {
		nFile     = nFileIn;
		nBlockPos = nBlockPosIn;
		nTxPos    = nTxPosIn;
	}

	IMPLEMENT_SERIALIZE(READWRITE(FLATDATA(*this));)
	void SetNull() {
		nFile     = (uint32_t)-1;
		nBlockPos = 0;
		nTxPos    = 0;
	}
	bool IsNull() const { return (nFile == (uint32_t)-1); }

	friend bool operator==(const CDiskTxPos& a, const CDiskTxPos& b) {
		return (a.nFile == b.nFile && a.nBlockPos == b.nBlockPos && a.nTxPos == b.nTxPos);
	}

	friend bool operator!=(const CDiskTxPos& a, const CDiskTxPos& b) { return !(a == b); }

	std::string ToString() const {
		if (IsNull())
			return "null";
		else
			return strprintf("(nFile=%u, nBlockPos=%u, nTxPos=%u)", nFile, nBlockPos, nTxPos);
	}
};

enum GetMinFee_mode {
	GMF_BLOCK,
	GMF_RELAY,
	GMF_SEND,
};

// struct PrevTxData { // TODO
// 	CTxIndex     txindex;
// 	CTransaction tx;
// };
typedef std::map<uint256, std::pair<CTxIndex, CTransaction>> MapPrevTx;
typedef std::map<uint256, CTxIndex>                          MapPrevTxIndex;

int64_t GetMinFee(const CTransaction& tx,
                  int                 nHeight,
                  uint32_t            nBlockSize,
                  enum GetMinFee_mode mode,
                  uint32_t            nBytes = 0);

/** The basic transaction that is broadcasted on the network and contained in
 * blocks.  A transaction can contain multiple inputs and outputs.
 */
class CTransaction {
public:
	static const int    CURRENT_VERSION = 1;
	int                 nVersion;
	uint32_t            nTime;
	uint32_t            nTimeFetched;
	std::vector<CTxIn>  vin;
	std::vector<CTxOut> vout;
	uint32_t            nLockTime;

	// Denial-of-service detection:
	mutable int nDoS;
	bool        DoS(int nDoSIn, bool fIn) const {
        nDoS += nDoSIn;
        return fIn;
	}

	CTransaction() { SetNull(); }

	CTransaction(int                        nVersion,
	             uint32_t                   nTime,
	             const std::vector<CTxIn>&  vin,
	             const std::vector<CTxOut>& vout,
	             uint32_t                   nLockTime)
	    : nVersion(nVersion),
	      nTime(nTime),
	      nTimeFetched(0),
	      vin(vin),
	      vout(vout),
	      nLockTime(nLockTime),
	      nDoS(0) {}

	IMPLEMENT_SERIALIZE(READWRITE(this->nVersion); nVersion = this->nVersion; READWRITE(nTime);
	                    READWRITE(vin);
	                    READWRITE(vout);
	                    READWRITE(nLockTime);)

	void SetNull() {
		nVersion     = CTransaction::CURRENT_VERSION;
		nTime        = GetAdjustedTime();
		nTimeFetched = 0;
		vin.clear();
		vout.clear();
		nLockTime = 0;
		nDoS      = 0;  // Denial-of-service prevention
	}

	bool IsNull() const { return (vin.empty() && vout.empty()); }

	uint256 GetHash() const { return SerializeHash(*this); }

	bool IsCoinBase() const {
		return (vin.size() == 1 && vin[0].prevout.IsNull() && vout.size() >= 1);
	}

	bool IsCoinStake() const {
		// ppcoin: the coin stake transaction is marked with the first output empty
		return (vin.size() > 0 && (!vin[0].prevout.IsNull()) && vout.size() >= 2 &&
		        vout[0].IsEmpty());
	}

	bool IsCoinMint() const {
		if (vin.size() != 2)
			return false;
		if (nVersion != 1)
			return false;
		if (nLockTime != 0)
			return false;
		if (vin[0].prevout.hash != 0)
			return false;
		if (vin[0].prevout.n != 0)
			return false;
		if (vin[0].nSequence != std::numeric_limits<uint32_t>::max())
			return false;
		if (vin[1].prevout.IsNull())
			return false;
		if (vin[1].prevout.n != 0)
			return false;
		if (vout.size() != 1)
			return false;
		if (vin[1].nSequence != std::numeric_limits<uint32_t>::max())
			return false;
		return true;
	}

	bool IsBridgeBurn() const {
		for (const CTxOut& txout : vout) {
			string notary;
			if (txout.scriptPubKey.ToNotary(notary)) {
				if (boost::starts_with(notary, "**Z**")) {
					return true;
				}
			}
		}
		return false;
	}

	/** Amount of bitcoins spent by this transaction.
	    @return sum of all outputs (note: does not include fees)
	 */
	int64_t GetValueOut() const {
		int64_t nValueOut = 0;
		for (const CTxOut& txout : vout) {
			nValueOut += txout.nValue;
			if (!MoneyRange(txout.nValue) || !MoneyRange(nValueOut))
				throw std::runtime_error("CTransaction::GetValueOut() : value out of range");
		}
		return nValueOut;
	}

	/** Amount of bitcoins burn by this transaction.
	    @return sum of all burned outputs
	 */
	int64_t GetValueBurn() const {
		int64_t nValueBurn = 0;
		for (const CTxOut& txout : vout) {
			opcodetype              opcode1;
			vector<unsigned char>   vch1;
			CScript::const_iterator pc1 = txout.scriptPubKey.begin();
			if (txout.scriptPubKey.GetOp(pc1, opcode1, vch1)) {
				if (opcode1 == OP_RETURN)
					nValueBurn += txout.nValue;
			}
		}
		return nValueBurn;
	}

	/** Amount of bitcoins coming in to this transaction
	    Note that lightweight clients may not know anything besides the hash of previous
	   transactions, so may not be able to calculate this.

	    @param[in] mapInputs	Map of previous transactions that have outputs we're spending
	    @return	Sum of value of all inputs (scriptSigs)
	    @see CTransaction::FetchInputs
	 */
	int64_t GetValueIn(const MapPrevTx& mapInputs) const;

	bool ReadFromDisk(CDiskTxPos pos, FILE** pfileRet = NULL) {
		CAutoFile filein = CAutoFile(OpenBlockFile(pos.nFile, 0, pfileRet ? "rb+" : "rb"), SER_DISK,
		                             CLIENT_VERSION);
		if (!filein)
			return error(
			    "CTransaction::ReadFromDisk() : OpenBlockFile failed nFile=%i, blockpos=0, mode=%s",
			    pos.nFile, pfileRet ? "rb+" : "rb");

		// Read transaction
		if (fseek(filein, pos.nTxPos, SEEK_SET) != 0)
			return error("CTransaction::ReadFromDisk() : fseek failed");

		try {
			filein >> *this;
		} catch (std::exception& e) {
			return error("%s() : deserialize or I/O error", __PRETTY_FUNCTION__);
		}

		// Return file pointer
		if (pfileRet) {
			if (fseek(filein, pos.nTxPos, SEEK_SET) != 0)
				return error("CTransaction::ReadFromDisk() : second fseek failed");
			*pfileRet = filein.release();
		}
		return true;
	}

	friend bool operator==(const CTransaction& a, const CTransaction& b) {
		return (a.nVersion == b.nVersion && a.nTime == b.nTime && a.vin == b.vin &&
		        a.vout == b.vout && a.nLockTime == b.nLockTime);
	}

	friend bool operator!=(const CTransaction& a, const CTransaction& b) { return !(a == b); }

	std::string ToString() const {
		std::string str;
		str += IsCoinBase() ? "Coinbase" : (IsCoinStake() ? "Coinstake" : "CTransaction");
		str += strprintf("(hash=%s, nTime=%d, ver=%d, vin.size=%u, vout.size=%u, nLockTime=%d)\n",
		                 GetHash().ToString(), nTime, nVersion, vin.size(), vout.size(), nLockTime);
		for (uint32_t i = 0; i < vin.size(); i++)
			str += "    " + vin[i].ToString() + "\n";
		for (uint32_t i = 0; i < vout.size(); i++)
			str += "    " + vout[i].ToString() + "\n";
		return str;
	}

	bool ReadFromDisk(CTxDB& txdb, COutPoint prevout, CTxIndex& txindexRet);
	bool ReadFromDisk(CTxDB& txdb, COutPoint prevout);
	bool ReadFromDisk(COutPoint prevout);
	bool DisconnectInputs(CTxDB& txdb, CPegDB& pegdb);

	/** Fetch from memory and/or disk. inputsRet keys are transaction hashes.

	 @param[in] txdb	Transaction database
	 @param[in] mapTestPool	List of pending changes to the transaction index database
	 @param[in] fBlock	True if being called to add a new best-block to the chain
	 @param[in] fMiner	True if being called by CreateNewBlock
	 @param[out] inputsRet	Pointers to this transaction's inputs
	 @param[out] fInvalid	returns true if transaction is invalid
	 @return	Returns true if all inputs are in txdb or mapTestPool
	 */
	bool FetchInputs(CTxDB&                                   txdb,
	                 CPegDB&                                  pegdb,
	                 int                                      nBridgePoolNout,
	                 bool                                     fBridgePoolMustInTestPool,
	                 const std::map<std::string, CBridgeInfo> bridges,
	                 std::function<CMerkleInfo(std::string)>  fnMerkleIn,
	                 const std::map<uint256, CTxIndex>&       mapTestPool,
	                 const MapFractions&                      mapTestFractionsPool,
	                 bool                                     fBlock,
	                 bool                                     fMiner,
	                 uint32_t                                 nBlockTime,
	                 bool                                     fSkipPruned,
	                 MapPrevTx&                               inputsRet,
	                 MapFractions&                            finputsRet,
	                 bool&                                    fInvalid);

	/** Sanity check previous transactions, then, if all checks succeed,
	    mark them as spent by this transaction.

	    @param[in] inputs	Previous transactions (from FetchInputs)
	    @param[out] mapTestPool	Keeps track of inputs that need to be updated on disk
	    @param[in] posThisTx	Position of this transaction on disk
	    @param[in] pindexBlock
	    @param[in] fBlock	true if called from ConnectBlock
	    @param[in] fMiner	true if called from CreateNewBlock
	    @return Returns true if all checks succeed
	 */
	bool ConnectInputs(MapPrevTx                                inputs,
	                   MapFractions&                            finputs,
	                   std::map<uint256, CTxIndex>&             mapTestPool,
	                   MapFractions&                            mapTestFractionsPool,
	                   int                                      nBridgePoolNout,
	                   const std::map<std::string, CBridgeInfo> bridges,
	                   std::function<CMerkleInfo(std::string)>  fnMerkleIn,
	                   const set<string>                        timelockpasses,
	                   CFractions&                              feesFractions,
	                   const CDiskTxPos&                        posThisTx,
	                   const CBlockIndex*                       pindexBlock,
	                   bool                                     fBlock,
	                   bool                                     fMiner,
	                   uint32_t flags = STANDARD_SCRIPT_VERIFY_FLAGS);
	bool CheckTransaction() const;

	void GetOutputFor(const CTxIn& input, const MapPrevTx& inputs, CTxOut& txout) const;

	bool IsExchangeTx(int& nOut, uint256& id) const;

	bool ConnectUtxo(CTxDB&             txdb,
	                 const CBlockIndex* pindex,
	                 int16_t            nTxIdx,
	                 MapPrevTx&         mapInputs,
	                 MapFractions&      mapInputsFractions,
	                 MapFractions&      mapOutputsFractions) const;
	bool DisconnectUtxo(CTxDB&        txdb,
	                    CPegDB&       pegdb,
	                    MapPrevTx&    mapInputs,
	                    MapFractions& mapInputsFractions,
	                    MapFractions& mapOutputsFractions) const;
};

/** wrapper for CTxOut that provides a more compact serialization */
class CTxOutCompressor {
private:
	CTxOut& txout;

public:
	CTxOutCompressor(CTxOut& txoutIn) : txout(txoutIn) {}

	IMPLEMENT_SERIALIZE(READWRITE(VARINT(txout.nValue));
	                    CScriptCompressor cscript(REF(txout.scriptPubKey));
	                    READWRITE(cscript);)
};

/** Check for standard transaction types
    @param[in] mapInputs	Map of previous transactions that have outputs we're spending
    @return True if all inputs (scriptSigs) use only standard transaction forms
    @see CTransaction::FetchInputs
*/
bool AreInputsStandard(const CTransaction& tx, const MapPrevTx& mapInputs);

/** Count ECDSA signature operations the old-fashioned (pre-0.6) way
    @return number of sigops this transaction's outputs will produce when spent
    @see CTransaction::FetchInputs
*/
uint32_t GetLegacySigOpCount(const CTransaction& tx);

/** Count ECDSA signature operations in pay-to-script-hash inputs.

    @param[in] mapInputs	Map of previous transactions that have outputs we're spending
    @return maximum number of sigops required to validate this transaction's inputs
    @see CTransaction::FetchInputs
 */
uint32_t GetP2SHSigOpCount(const CTransaction& tx, const MapPrevTx& mapInputs);

/** Check for standard transaction types
    @return True if all outputs (scriptPubKeys) use only standard transaction forms
*/
bool IsStandardTx(const CTransaction& tx, std::string& reason);

bool IsFinalTx(const CTransaction& tx, int nBlockHeight = 0, int64_t nBlockTime = 0);

/** A transaction with a merkle branch linking it to the block chain. */
class CMerkleTx : public CTransaction {
private:
	int GetDepthInMainChainINTERNAL(CBlockIndex*& pindexRet) const;
	int GetBlockNumInMainChainINTERNAL(CBlockIndex*& pindexRet) const;

public:
	uint256              hashBlock;
	std::vector<uint256> vMerkleBranch;
	int                  nIndex;

	// memory only
	mutable bool fMerkleVerified;

	CMerkleTx() {
		nTime = 0;  // New wallet tx
		Init();
	}

	CMerkleTx(const CTransaction& txIn) : CTransaction(txIn) { Init(); }

	void Init() {
		hashBlock       = 0;
		nIndex          = -1;
		fMerkleVerified = false;
	}

	IMPLEMENT_SERIALIZE(nSerSize +=
	                    SerReadWrite(s, *(CTransaction*)this, nType, nVersion, ser_action);
	                    nVersion = this->nVersion;
	                    READWRITE(hashBlock);
	                    READWRITE(vMerkleBranch);
	                    READWRITE(nIndex);)

	int SetMerkleBranch(const CBlock* pblock = NULL);

	// Return depth of transaction in blockchain:
	// -1  : not in blockchain, and not in memory pool (conflicted transaction)
	//  0  : in memory pool, waiting to be included in a block
	// >=1 : this many blocks deep in the main chain
	int GetDepthInMainChain(CBlockIndex*& pindexRet) const;
	int GetDepthInMainChain() const {
		CBlockIndex* pindexRet;
		return GetDepthInMainChain(pindexRet);
	}
	int GetBlockNumInMainChain(CBlockIndex*& pindexRet) const;
	int GetBlockNumInMainChain() const {
		CBlockIndex* pindexRet;
		return GetBlockNumInMainChain(pindexRet);
	}
	bool IsInMainChain() const {
		CBlockIndex* pindexRet;
		return GetDepthInMainChainINTERNAL(pindexRet) > 0;
	}
	int  GetBlocksToMaturity() const;
	bool AcceptToMemoryPool(bool fLimitFree = true);
};

/**  A txdb record that contains the disk location of a transaction and the
 * locations of transactions that spend its outputs.  vSpent is really only
 * used as a flag, but having the location is very helpful for debugging.
 */
class CTxIndex {
public:
	static const int        CURRENT_VERSION = 1;
	int                     nVersion;
	CDiskTxPos              pos;
	std::vector<CDiskTxPos> vSpent;
	int64_t                 nHeight;
	uint16_t                nIndex;

	CTxIndex() { SetNull(); }

	CTxIndex(const CDiskTxPos& posIn, uint32_t nOutputs, int64_t nHeight, uint16_t nTxIndex) {
		SetNull();
		pos = posIn;
		vSpent.resize(nOutputs);
	}

	IMPLEMENT_SERIALIZE(
	    READWRITE(this->nVersion); nVersion = this->nVersion; if (nVersion >= 1000000) {
		    READWRITE(pos);
		    READWRITE(vSpent);
	    } else if (nVersion == 1) {
		    READWRITE(pos);
		    READWRITE(vSpent);
		    READWRITE(nHeight);
		    READWRITE(nIndex);
	    })

	void SetNull() {
		nVersion = CTxIndex::CURRENT_VERSION;
		pos.SetNull();
		vSpent.clear();
		nHeight = 0;
		nIndex  = 0;
	}

	bool IsNull() { return pos.IsNull(); }

	friend bool operator==(const CTxIndex& a, const CTxIndex& b) {
		return (a.pos == b.pos && a.vSpent == b.vSpent);
	}

	friend bool operator!=(const CTxIndex& a, const CTxIndex& b) { return !(a == b); }

	int GetDepthInMainChain() const;
	// if vtxidx - expensive - read whole block
	int GetHeightInMainChain(uint32_t* vtxidx    = nullptr,
	                         uint256   txhash    = uint256(0),
	                         uint256*  blockhash = nullptr) const;
};

/**  A txdb record that contains the balance change per address
 */
class CAddressBalance {
public:
	uint256  txhash;
	uint64_t nTime;
	uint64_t nHeight;
	int64_t  nIndex;
	int64_t  nDebit;
	int64_t  nCredit;
	int64_t  nBalance;
	int64_t  nFrozen;
	uint64_t nLockTime;

	CAddressBalance() { SetNull(); }

	IMPLEMENT_SERIALIZE(READWRITE(txhash); READWRITE(nHeight); READWRITE(nIndex); READWRITE(nTime);
	                    READWRITE(nDebit);
	                    READWRITE(nCredit);
	                    READWRITE(nBalance);
	                    READWRITE(nFrozen);
	                    READWRITE(nLockTime);)

	void SetNull() {
		nHeight   = 0;
		nIndex    = 0;
		nTime     = 0;
		nDebit    = 0;
		nCredit   = 0;
		nBalance  = 0;
		nFrozen   = 0;
		nLockTime = 0;
	}
};

/**  A txdb record that contains the utxo per address
 */
class CAddressUnspent {
public:
	uint320  txoutid;
	int64_t  nHeight;
	int16_t  nIndex;
	int64_t  nAmount;
	uint32_t nFlags;
	uint64_t nLockTime;

	CAddressUnspent() { SetNull(); }

	IMPLEMENT_SERIALIZE(READWRITE(nHeight); READWRITE(nIndex); READWRITE(nAmount);
	                    READWRITE(nFlags);
	                    READWRITE(nLockTime);)

	void SetNull() {
		nHeight   = 0;
		nIndex    = 0;
		nAmount   = 0;
		nFlags    = 0;
		nLockTime = 0;
	}
};

/**  A txdb record that contains the utxo per address
 */
class CFrozenQueued {
public:
	uint64_t    nLockTime;
	uint320     txoutid;
	int64_t     nAmount;
	std::string sAddress;

	CFrozenQueued() { SetNull(); }

	CFrozenQueued(std::string sAddress, int64_t nAmount) {
		SetNull();
		this->nAmount  = nAmount;
		this->sAddress = sAddress;
	}

	IMPLEMENT_SERIALIZE(READWRITE(nAmount); READWRITE(sAddress);)

	void SetNull() {
		nAmount   = 0;
		nLockTime = 0;
	}
};

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 *
 * Blocks are appended to blk0001.dat files on disk.  Their location on disk
 * is indexed by CBlockIndex objects in memory.
 */
class CBlock {
public:
	// header
	static const int CURRENT_VERSION = 8;
	int              nVersion;
	uint256          hashPrevBlock;
	uint256          hashMerkleRoot;
	uint32_t         nTime;
	uint32_t         nBits;
	uint32_t         nNonce;

	// network and disk
	std::vector<CTransaction> vtx;

	// ppcoin: block signature - signed by one of the coin base txout[N]'s owner
	std::vector<unsigned char> vchBlockSig;

	// memory only
	mutable std::vector<uint256> vMerkleTree;

	// Denial-of-service detection:
	mutable int nDoS;
	bool        DoS(int nDoSIn, bool fIn) const {
        nDoS += nDoSIn;
        return fIn;
	}

	CBlock() { SetNull(); }

	IMPLEMENT_SERIALIZE(
	    READWRITE(this->nVersion); nVersion = this->nVersion; READWRITE(hashPrevBlock);
	    READWRITE(hashMerkleRoot);
	    READWRITE(nTime);
	    READWRITE(nBits);
	    READWRITE(nNonce);

	    // ConnectBlock depends on vtx following header to generate CDiskTxPos
	    if (!(nType & (SER_GETHASH | SER_BLOCKHEADERONLY))) {
		    READWRITE(vtx);
		    READWRITE(vchBlockSig);
	    } else if (fRead) {
		    const_cast<CBlock*>(this)->vtx.clear();
		    const_cast<CBlock*>(this)->vchBlockSig.clear();
	    })

	void SetNull() {
		nVersion       = CBlock::CURRENT_VERSION;
		hashPrevBlock  = 0;
		hashMerkleRoot = 0;
		nTime          = 0;
		nBits          = 0;
		nNonce         = 0;
		vtx.clear();
		vchBlockSig.clear();
		vMerkleTree.clear();
		nDoS = 0;
	}

	bool IsNull() const { return (nBits == 0); }

	uint256 GetHash() const {
		if (nVersion > 6)
			return Hash(BEGIN(nVersion), END(nNonce));
		else
			return GetPoWHash();
	}

	uint256 GetPoWHash() const {
		uint256 thash;
		scrypt_1024_1_1_256(BEGIN(nVersion), BEGIN(thash));
		return thash;
	}

	int64_t GetBlockTime() const { return (int64_t)nTime; }

	void UpdateTime(const CBlockIndex* pindexPrev);

	// entropy bit for stake modifier if chosen by modifier
	uint32_t GetStakeEntropyBit() const {
		// Take last bit of block hash as entropy bit
		uint32_t nEntropyBit = ((GetHash().GetLow64()) & 1llu);
		LogPrint("stakemodifier", "GetStakeEntropyBit: hashBlock=%s nEntropyBit=%u\n",
		         GetHash().ToString(), nEntropyBit);
		return nEntropyBit;
	}

	// ppcoin: two types of block: proof-of-work or proof-of-stake
	bool IsProofOfStake() const { return (vtx.size() > 1 && vtx[1].IsCoinStake()); }

	bool IsProofOfWork() const { return !IsProofOfStake(); }

	bool IsPeg() const { return false; }

	std::pair<COutPoint, uint32_t> GetProofOfStake() const {
		return IsProofOfStake() ? std::make_pair(vtx[1].vin[0].prevout, vtx[1].nTime)
		                        : std::make_pair(COutPoint(), (uint32_t)0);
	}

	// ppcoin: get max transaction timestamp
	int64_t GetMaxTransactionTime() const {
		int64_t maxTransactionTime = 0;
		for (const CTransaction& tx : vtx) {
			maxTransactionTime = std::max(maxTransactionTime, (int64_t)tx.nTime);
		}
		return maxTransactionTime;
	}

	uint256 BuildMerkleTree() const {
		vMerkleTree.clear();
		for (const CTransaction& tx : vtx) {
			vMerkleTree.push_back(tx.GetHash());
		}
		int j = 0;
		for (int nSize = vtx.size(); nSize > 1; nSize = (nSize + 1) / 2) {
			for (int i = 0; i < nSize; i += 2) {
				int i2 = std::min(i + 1, nSize - 1);
				vMerkleTree.push_back(Hash(BEGIN(vMerkleTree[j + i]), END(vMerkleTree[j + i]),
				                           BEGIN(vMerkleTree[j + i2]), END(vMerkleTree[j + i2])));
			}
			j += nSize;
		}
		return (vMerkleTree.empty() ? 0 : vMerkleTree.back());
	}

	std::vector<uint256> GetMerkleBranch(int nIndex) const {
		if (vMerkleTree.empty())
			BuildMerkleTree();
		std::vector<uint256> vMerkleBranch;
		int                  j = 0;
		for (int nSize = vtx.size(); nSize > 1; nSize = (nSize + 1) / 2) {
			int i = std::min(nIndex ^ 1, nSize - 1);
			vMerkleBranch.push_back(vMerkleTree[j + i]);
			nIndex >>= 1;
			j += nSize;
		}
		return vMerkleBranch;
	}

	static uint256 CheckMerkleBranch(uint256                     hash,
	                                 const std::vector<uint256>& vMerkleBranch,
	                                 int                         nIndex) {
		if (nIndex == -1)
			return 0;
		for (const uint256& otherside : vMerkleBranch) {
			if (nIndex & 1)
				hash = Hash(BEGIN(otherside), END(otherside), BEGIN(hash), END(hash));
			else
				hash = Hash(BEGIN(hash), END(hash), BEGIN(otherside), END(otherside));
			nIndex >>= 1;
		}
		return hash;
	}

	bool WriteToDisk(uint32_t& nFileRet, uint32_t& nBlockPosRet) {
		// Open history file to append
		CAutoFile fileout = CAutoFile(AppendBlockFile(nFileRet), SER_DISK, CLIENT_VERSION);
		if (!fileout)
			return error("CBlock::WriteToDisk() : AppendBlockFile failed");

		// Write index header
		uint32_t nSize = fileout.GetSerializeSize(*this);
		fileout << FLATDATA(Params().MessageStart()) << nSize;

		// Write block
		long fileOutPos = ftell(fileout);
		if (fileOutPos < 0)
			return error("CBlock::WriteToDisk() : ftell failed");
		nBlockPosRet = fileOutPos;
		fileout << *this;

		// Flush stdio buffers and commit to disk before returning
		fflush(fileout);
		if (!IsInitialBlockDownload() || (nBestHeight + 1) % 500 == 0)
			FileCommit(fileout);

		return true;
	}

	bool ReadFromDisk(uint32_t nFile, uint32_t nBlockPos, bool fReadTransactions = true) {
		SetNull();

		// Open history file to read
		CAutoFile filein =
		    CAutoFile(OpenBlockFile(nFile, nBlockPos, "rb"), SER_DISK, CLIENT_VERSION);
		if (!filein)
			return error("CBlock::ReadFromDisk() : OpenBlockFile failed");
		if (!fReadTransactions)
			filein.nType |= SER_BLOCKHEADERONLY;

		// Read block
		try {
			filein >> *this;
		} catch (std::exception& e) {
			return error("%s() : deserialize or I/O error", __PRETTY_FUNCTION__);
		}

		// Check the header
		if (fReadTransactions && IsProofOfWork() && !CheckProofOfWork(GetPoWHash(), nBits))
			return error("CBlock::ReadFromDisk() : errors in block header");

		return true;
	}

	std::string ToString() const {
		std::stringstream s;
		s << strprintf(
		    "CBlock(hash=%s, ver=%d, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, "
		    "nNonce=%u, vtx=%u, vchBlockSig=%s)\n",
		    GetHash().ToString(), nVersion, hashPrevBlock.ToString(), hashMerkleRoot.ToString(),
		    nTime, nBits, nNonce, vtx.size(), HexStr(vchBlockSig.begin(), vchBlockSig.end()));
		for (uint32_t i = 0; i < vtx.size(); i++) {
			s << "  " << vtx[i].ToString() << "\n";
		}
		s << "  vMerkleTree: ";
		for (uint32_t i = 0; i < vMerkleTree.size(); i++)
			s << " " << vMerkleTree[i].ToString();
		s << "\n";
		return s.str();
	}

	bool DisconnectBlock(CTxDB& txdb, CPegDB& pegdb, CBlockIndex* pindex);
	bool ConnectBlock(CTxDB& txdb, CPegDB& pegdb, CBlockIndex* pindex, bool fJustCheck = false);
	bool ReadFromDisk(const CBlockIndex* pindex, bool fReadTransactions = true);
	bool SetBestChain(CTxDB& txdb, CPegDB& pegdb, CBlockIndex* pindexNew);
	bool AddToBlockIndex(uint32_t nFile, uint32_t nBlockPos, const uint256& hashProof);
	bool CheckBlock(bool fCheckPOW        = true,
	                bool fCheckMerkleRoot = true,
	                bool fCheckSig        = true) const;
	bool AcceptBlock();
	bool SignBlock(CWallet& keystore, int64_t nFees);
	bool CheckBlockSignature() const;
	bool ProcessFrozenQueue(CTxDB&             txdb,
	                        CPegDB&            pegdb,
	                        MapFractions&      mapFractions,
	                        const CBlockIndex* pindex,
	                        bool               fLoading);

private:
	bool SetBestChainInner(CTxDB& txdb, CPegDB& pegdb, CBlockIndex* pindexNew);
};

/** The block chain is a tree shaped structure starting with the
 * genesis block at the root, with each block potentially having multiple
 * candidates to be the next block.  pprev and pnext link a path through the
 * main/longest chain.  A blockindex may have multiple pprev pointing back
 * to it, but pnext will only point forward to the longest branch, or will
 * be null if the block is not part of the longest chain.
 */
class CBlockIndex {
public:
	const uint256* phashBlock;
	CBlockIndex*   Prev() const { return pPrev; }
	CBlockIndex*   Next() const { return pNext; }
	void           SetPrev(CBlockIndex* p) { pPrev = p; }
	void           SetNext(CBlockIndex* p) { pNext = p; }

	int          PegCycle() const;
	CBlockIndex* PegCycleBlock() const;
	CBlockIndex* PrevPegCycleBlock() const;

	int          BridgeCycle() const;
	CBlockIndex* BridgeCycleBlock() const;
	CBlockIndex* PrevBridgeCycleBlock() const;

private:
	CBlockIndex* pPrev;
	CBlockIndex* pNext;

public:
	uint32_t nFile;
	uint32_t nBlockPos;
	uint256  nChainTrust;  // ppcoin: trust score of block chain
	int      nHeight;
	bool     fMainChainNoReorg;

	int64_t nMint;
	int64_t nMoneySupply;
	int     nPegSupplyIndex;
	int     nPegVotesInflate;
	int     nPegVotesDeflate;
	int     nPegVotesNochange;

	uint32_t nFlags;  // ppcoin: block index flags
	enum {
		BLOCK_PROOF_OF_STAKE = (1 << 0),  // is proof-of-stake block
		BLOCK_STAKE_ENTROPY  = (1 << 1),  // entropy bit for stake modifier
		BLOCK_STAKE_MODIFIER = (1 << 2),  // regenerated stake modifier
		BLOCK_PEG            = (1 << 3),  // block with peg
	};

	uint64_t nStakeModifier;  // hash modifier for proof-of-stake
	uint256  bnStakeModifierV2;

	// proof-of-stake specific fields
	COutPoint prevoutStake;
	uint32_t  nStakeTime;

	uint256 hashProof;

	// block header
	int      nVersion;
	uint256  hashMerkleRoot;
	uint32_t nTime;
	uint32_t nBits;
	uint32_t nNonce;

	CBlockIndex() {
		phashBlock        = NULL;
		pPrev             = NULL;
		pNext             = NULL;
		nFile             = 0;
		nBlockPos         = 0;
		nHeight           = 0;
		fMainChainNoReorg = false;
		nChainTrust       = 0;
		nMint             = 0;
		nMoneySupply      = 0;
		nPegSupplyIndex   = 0;
		nPegVotesInflate  = 0;
		nPegVotesDeflate  = 0;
		nPegVotesNochange = 0;
		nFlags            = 0;
		nStakeModifier    = 0;
		bnStakeModifierV2 = 0;
		hashProof         = 0;
		prevoutStake.SetNull();
		nStakeTime = 0;

		nVersion       = 0;
		hashMerkleRoot = 0;
		nTime          = 0;
		nBits          = 0;
		nNonce         = 0;
	}

	CBlockIndex(uint32_t nFileIn, uint32_t nBlockPosIn, CBlock& block) {
		phashBlock        = NULL;
		pPrev             = NULL;
		pNext             = NULL;
		nFile             = nFileIn;
		nBlockPos         = nBlockPosIn;
		nHeight           = 0;
		fMainChainNoReorg = false;
		nChainTrust       = 0;
		nMint             = 0;
		nMoneySupply      = 0;
		nPegSupplyIndex   = 0;
		nPegVotesInflate  = 0;
		nPegVotesDeflate  = 0;
		nPegVotesNochange = 0;
		nFlags            = 0;
		nStakeModifier    = 0;
		bnStakeModifierV2 = 0;
		hashProof         = 0;
		if (block.IsProofOfStake()) {
			SetProofOfStake();
			prevoutStake = block.vtx[1].vin[0].prevout;
			nStakeTime   = block.vtx[1].nTime;
		} else {
			prevoutStake.SetNull();
			nStakeTime = 0;
		}
		SetPeg(true);

		nVersion       = block.nVersion;
		hashMerkleRoot = block.hashMerkleRoot;
		nTime          = block.nTime;
		nBits          = block.nBits;
		nNonce         = block.nNonce;
	}

	CBlock GetBlockHeader() const {
		CBlock block;
		block.nVersion = nVersion;
		if (Prev())
			block.hashPrevBlock = Prev()->GetBlockHash();
		block.hashMerkleRoot = hashMerkleRoot;
		block.nTime          = nTime;
		block.nBits          = nBits;
		block.nNonce         = nNonce;
		return block;
	}

	uint256 GetBlockHash() const { return *phashBlock; }

	int64_t GetBlockTime() const { return (int64_t)nTime; }

	uint256 GetBlockTrust() const;

	bool IsInMainChain() const { return (fMainChainNoReorg || Next() || this == pindexBest); }

	bool CheckIndex() const { return true; }

	// TODO: eliminate
	int64_t GetPastTimeLimit() const {
		if (IsProtocolV2(nHeight))
			return GetBlockTime();
		else
			return GetMedianTimePast();
	}

	enum { nMedianTimeSpan = 11 };

	int64_t GetMedianTimePast() const {
		int64_t  pmedian[nMedianTimeSpan];
		int64_t* pbegin = &pmedian[nMedianTimeSpan];
		int64_t* pend   = &pmedian[nMedianTimeSpan];

		const CBlockIndex* pindex = this;
		for (int i = 0; i < nMedianTimeSpan && pindex; i++, pindex = pindex->Prev())
			*(--pbegin) = pindex->GetBlockTime();

		std::sort(pbegin, pend);
		return pbegin[(pend - pbegin) / 2];
	}

	/**
	 * Returns true if there are nRequired or more blocks of minVersion or above
	 * in the last nToCheck blocks, starting at pstart and going backwards.
	 */
	static bool IsSuperMajority(int                minVersion,
	                            const CBlockIndex* pstart,
	                            uint32_t           nRequired,
	                            uint32_t           nToCheck);

	bool IsProofOfWork() const { return !(nFlags & BLOCK_PROOF_OF_STAKE); }

	bool IsProofOfStake() const { return (nFlags & BLOCK_PROOF_OF_STAKE); }

	void SetProofOfStake() { nFlags |= BLOCK_PROOF_OF_STAKE; }

	bool IsPeg() const { return (nFlags & BLOCK_PEG); }

	void SetPeg(bool bPeg) {
		uint32_t flags = nFlags;
		if (bPeg) {
			flags             = flags | BLOCK_PEG;
			nPegSupplyIndex   = -1;  // to be calculated
			nPegVotesInflate  = -1;  // to be calculated
			nPegVotesDeflate  = -1;  // to be calculated
			nPegVotesNochange = -1;  // to be calculated
		} else {
			uint32_t f_peg    = BLOCK_PEG;
			flags             = flags & ~f_peg;
			nPegSupplyIndex   = 0;
			nPegVotesInflate  = 0;
			nPegVotesDeflate  = 0;
			nPegVotesNochange = 0;
		}
		nFlags = flags;
	}

	int        GetNextBlockPegSupplyIndex() const;
	int        GetNextIntervalPegSupplyIndex() const;
	int        GetNextNextIntervalPegSupplyIndex() const;
	static int ComputeNextPegSupplyIndex(int                nPegBase,
	                                     const CBlockIndex* back2interval,
	                                     const CBlockIndex* back3interval);

	uint32_t GetStakeEntropyBit() const { return ((nFlags & BLOCK_STAKE_ENTROPY) >> 1); }

	bool SetStakeEntropyBit(uint32_t nEntropyBit) {
		if (nEntropyBit > 1)
			return false;
		nFlags |= (nEntropyBit ? BLOCK_STAKE_ENTROPY : 0);
		return true;
	}

	bool GeneratedStakeModifier() const { return (nFlags & BLOCK_STAKE_MODIFIER); }

	void SetStakeModifier(uint64_t nModifier, bool fGeneratedStakeModifier) {
		nStakeModifier = nModifier;
		if (fGeneratedStakeModifier)
			nFlags |= BLOCK_STAKE_MODIFIER;
	}

	std::string ToString() const {
		return strprintf(
		    "CBlockIndex(nprev=%p, pnext=%p, nFile=%u, nBlockPos=%-6d nHeight=%d, nMint=%s, "
		    "nMoneySupply=%s, nPegSupplyIndex=%d, PegVotes={%d,%d,%d}, nFlags=(%s)(%d)(%s), "
		    "nStakeModifier=%016x, hashProof=%s, prevoutStake=(%s), nStakeTime=%d merkle=%s, "
		    "hashBlock=%s)",
		    Prev(), Next(), nFile, nBlockPos, nHeight, FormatMoney(nMint),
		    FormatMoney(nMoneySupply), nPegSupplyIndex, nPegVotesInflate, nPegVotesDeflate,
		    nPegVotesNochange, GeneratedStakeModifier() ? "MOD" : "-", GetStakeEntropyBit(),
		    IsProofOfStake() ? "PoS" : "PoW", nStakeModifier, hashProof.ToString(),
		    prevoutStake.ToString(), nStakeTime, hashMerkleRoot.ToString(),
		    GetBlockHash().ToString());
	}

	// proposals extra data
	bool ReadTrustedStakers1(CPegDB& pegdb, std::set<string>& addresses) const;
	bool ReadTrustedStakers2(CPegDB& pegdb, std::set<string>& addresses) const;
	bool ReadConsensusMap(CPegDB&                                      pegdb,
	                      std::map<int, CChainParams::ConsensusVotes>& consensus) const;
	bool ReadBridgesMap(CPegDB& pegdb, std::map<std::string, std::vector<string>>& bridges) const;
	bool ReadBridges(CPegDB& pegdb, std::map<std::string, CBridgeInfo>& bridges) const;
	bool ReadMerklesIn(CPegDB& pegdb, int limit, std::vector<CMerkleInfo>& merkles) const;
	CMerkleInfo ReadMerkleIn(CPegDB& pegdb, std::string hash) const;
	bool        ReadTimeLockPasses(CPegDB& pegdb, std::set<string>& pubkeys) const;
	bool        ReadBridgesPause(CPegDB& pegdb, bool& pause) const;
};

/** Used to marshal pointers into hashes for db storage. */
class CDiskBlockIndex : public CBlockIndex {
private:
	uint256 blockHash;

public:
	uint256 hashPrev;
	uint256 hashNext;

	CDiskBlockIndex() {
		hashPrev  = 0;
		hashNext  = 0;
		blockHash = 0;
	}

	explicit CDiskBlockIndex(CBlockIndex* pindex) : CBlockIndex(*pindex) {
		hashPrev = (Prev() ? Prev()->GetBlockHash() : 0);
		hashNext = (Next() ? Next()->GetBlockHash() : 0);
	}

	IMPLEMENT_SERIALIZE(
	    if (!(nType & SER_GETHASH)) READWRITE(nVersion);

	    READWRITE(hashNext);
	    READWRITE(nFile);
	    READWRITE(nBlockPos);
	    READWRITE(nHeight);
	    READWRITE(nMint);
	    READWRITE(nMoneySupply);
	    READWRITE(nFlags);
	    READWRITE(nStakeModifier);
	    READWRITE(bnStakeModifierV2);
	    if (IsProofOfStake()) {
		    READWRITE(prevoutStake);
		    READWRITE(nStakeTime);
	    } else if (fRead) {
		    const_cast<CDiskBlockIndex*>(this)->prevoutStake.SetNull();
		    const_cast<CDiskBlockIndex*>(this)->nStakeTime = 0;
	    } if (IsPeg()) {
		    READWRITE(nPegSupplyIndex);
		    READWRITE(nPegVotesInflate);
		    READWRITE(nPegVotesDeflate);
		    READWRITE(nPegVotesNochange);
	    } else {
		    const_cast<CDiskBlockIndex*>(this)->nPegSupplyIndex   = 0;
		    const_cast<CDiskBlockIndex*>(this)->nPegVotesInflate  = 0;
		    const_cast<CDiskBlockIndex*>(this)->nPegVotesDeflate  = 0;
		    const_cast<CDiskBlockIndex*>(this)->nPegVotesNochange = 0;
	    } READWRITE(hashProof);

	    // block header
	    READWRITE(this->nVersion);
	    READWRITE(hashPrev);
	    READWRITE(hashMerkleRoot);
	    READWRITE(nTime);
	    READWRITE(nBits);
	    READWRITE(nNonce);
	    READWRITE(blockHash);)

	uint256 GetBlockHash() const {
		if (fUseFastIndex && (nTime < GetAdjustedTime() - 24 * 60 * 60) && blockHash != 0)
			return blockHash;

		CBlock block;
		block.nVersion       = nVersion;
		block.hashPrevBlock  = hashPrev;
		block.hashMerkleRoot = hashMerkleRoot;
		block.nTime          = nTime;
		block.nBits          = nBits;
		block.nNonce         = nNonce;

		const_cast<CDiskBlockIndex*>(this)->blockHash = block.GetHash();

		return blockHash;
	}

	std::string ToString() const {
		std::string str = "CDiskBlockIndex(";
		str += CBlockIndex::ToString();
		str += strprintf("\n                hashBlock=%s, hashPrev=%s, hashNext=%s)",
		                 GetBlockHash().ToString(), hashPrev.ToString(), hashNext.ToString());
		return str;
	}
};

/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
class CBlockLocator {
protected:
	std::vector<uint256> vHave;

public:
	CBlockLocator() {}

	explicit CBlockLocator(const CBlockIndex* pindex) { Set(pindex); }

	explicit CBlockLocator(uint256 hashBlock) {
		std::unordered_map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
		if (mi != mapBlockIndex.end())
			Set((*mi).second);
	}

	CBlockLocator(const std::vector<uint256>& vHaveIn) { vHave = vHaveIn; }

	IMPLEMENT_SERIALIZE(if (!(nType & SER_GETHASH)) READWRITE(nVersion); READWRITE(vHave);)

	void SetNull() { vHave.clear(); }

	bool IsNull() { return vHave.empty(); }

	void Set(const CBlockIndex* pindex) {
		vHave.clear();
		int nStep = 1;
		while (pindex) {
			vHave.push_back(pindex->GetBlockHash());

			// Exponentially larger steps back
			for (int i = 0; pindex && i < nStep; i++)
				pindex = pindex->Prev();
			if (vHave.size() > 10)
				nStep *= 2;
		}
		vHave.push_back(Params().HashGenesisBlock());
	}

	int GetDistanceBack() {
		// Retrace how far back it was in the sender's branch
		int nDistance = 0;
		int nStep     = 1;
		for (const uint256& hash : vHave) {
			std::unordered_map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hash);
			if (mi != mapBlockIndex.end()) {
				CBlockIndex* pindex = (*mi).second;
				if (pindex->IsInMainChain())
					return nDistance;
			}
			nDistance += nStep;
			if (nDistance > 10)
				nStep *= 2;
		}
		return nDistance;
	}

	CBlockIndex* GetBlockIndex() {
		// Find the first block the caller has in the main chain
		for (const uint256& hash : vHave) {
			std::unordered_map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hash);
			if (mi != mapBlockIndex.end()) {
				CBlockIndex* pindex = (*mi).second;
				if (pindex->IsInMainChain())
					return pindex;
			}
		}
		return pindexGenesisBlock;
	}

	uint256 GetBlockHash() {
		// Find the first block the caller has in the main chain
		for (const uint256& hash : vHave) {
			std::unordered_map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hash);
			if (mi != mapBlockIndex.end()) {
				CBlockIndex* pindex = (*mi).second;
				if (pindex->IsInMainChain())
					return hash;
			}
		}
		return Params().HashGenesisBlock();
	}

	int GetHeight() {
		CBlockIndex* pindex = GetBlockIndex();
		if (!pindex)
			return 0;
		return pindex->nHeight;
	}
};

class CWalletInterface {
protected:
	virtual void SyncTransaction(const CTransaction& tx,
	                             const CBlock*       pblock,
	                             bool                fConnect,
	                             const MapFractions&)       = 0;
	virtual void EraseFromWallet(const uint256& hash)       = 0;
	virtual void SetBestChain(const CBlockLocator& locator) = 0;
	virtual void UpdatedTransaction(const uint256& hash)    = 0;
	virtual void Inventory(const uint256& hash)             = 0;
	virtual void ResendWalletTransactions(bool fForce)      = 0;
	friend void ::RegisterWallet(CWalletInterface*);
	friend void ::UnregisterWallet(CWalletInterface*);
	friend void ::UnregisterAllWallets();
};

#endif
