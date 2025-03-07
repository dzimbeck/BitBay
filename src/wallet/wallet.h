// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_WALLET_H
#define BITCOIN_WALLET_H

#include "walletdb.h"

#include <string>
#include <vector>

#include <stdlib.h>

#include "crypter.h"
#include "key.h"
#include "keystore.h"
#include "main.h"
#include "peg.h"
#include "script.h"
#include "ui_interface.h"
#include "util.h"

// Settings
extern int64_t nTransactionFee;
extern int64_t nNoStakeBalance;
extern int64_t nMinimumInputValue;
extern bool    fWalletUnlockStakingOnly;
extern bool    fConfChange;

class CAccountingEntry;
class CCoinControl;
class CSelectedCoin;
class CWalletTx;
class CReserveKey;
class COutput;
class CWalletDB;

/** (client) version numbers for particular wallet features */
enum WalletFeature {
	FEATURE_BASE = 10500,  // the earliest version new wallets supports (only useful for getinfo's
	                       // clientversion output)

	FEATURE_WALLETCRYPT = 40000,  // wallet encryption
	FEATURE_COMPRPUBKEY = 60000,  // compressed public keys

	FEATURE_LATEST = 60000
};

/** A key pool entry */
class CKeyPool {
public:
	int64_t nTime;
	CPubKey vchPubKey;

	CKeyPool() { nTime = GetTime(); }

	CKeyPool(const CPubKey& vchPubKeyIn) {
		nTime     = GetTime();
		vchPubKey = vchPubKeyIn;
	}

	IMPLEMENT_SERIALIZE(if (!(nType & SER_GETHASH)) READWRITE(nVersion); READWRITE(nTime);
	                    READWRITE(vchPubKey);)
};

struct RewardInfo {
	PegRewardType type;
	int64_t       amount;
	int           count;
	int           stake;
};

class CFrozenCoinInfo {
public:
	uint256  txhash;
	int      n;
	int64_t  nValue;
	uint64_t nFlags;
	uint64_t nLockTime;
	bool     operator==(const CFrozenCoinInfo& b) const {
		return txhash == b.txhash && n == b.n && nValue == b.nValue && nFlags == b.nFlags &&
			   nLockTime == b.nFlags;
	}
};

/** A CWallet is an extension of a keystore, which also maintains a set of transactions and
 * balances, and provides the ability to create new transactions.
 */
class CWallet : public CCryptoKeyStore, public CWalletInterface {
private:
	bool SelectCoinsForStaking(int64_t                                           nTargetValue,
	                           uint32_t                                          nSpendTime,
	                           std::set<std::pair<const CWalletTx*, uint32_t> >& setCoinsRet,
	                           int64_t&                                          nValueRet) const;
	bool SelectCoins(PegTxType                txType,
	                 int64_t                  nTargetValue,
	                 uint32_t                 nSpendTime,
	                 std::set<CSelectedCoin>& setCoinsRet,
	                 int64_t&                 nValueRet,
	                 bool                     fUseFrozenUnlocked,
	                 const CCoinControl*      coinControl) const;

	CWalletDB* pwalletdbEncryption;

	// the current wallet version: clients below this version are not able to load the wallet
	int nWalletVersion;

	// the maximum wallet format version: memory-only variable that specifies to what version this
	// wallet may be upgraded
	int nWalletMaxVersion;

	// peg vote type for staking
	PegVoteType pegVoteType         = PEG_VOTE_AUTO;
	PegVoteType trackerVoteType     = PEG_VOTE_NONE;
	PegVoteType lastAutoPegVoteType = PEG_VOTE_AUTO;

	std::vector<double> vBayRates;
	std::vector<double> vBtcRates;
	double              dBayPeakPrice = 0;

	std::string rewardAddress;
	std::string supportAddress;
	bool        supportEnabled = true;
	uint32_t    supportPart;
	bool        consolidateEnabled    = true;
	int         nConsolidateLeast     = 5;
	int         nConsolidateMin       = 20;
	int         nConsolidateMax       = 50;
	int64_t     nConsolidateMaxAmount = 10000000000000;

public:
	/// Main wallet lock.
	/// This lock protects all the fields added by CWallet
	///   except for:
	///      fFileBacked (immutable after instantiation)
	///      strWalletFile (immutable after instantiation)
	mutable CCriticalSection cs_wallet;

	bool        fFileBacked;
	std::string strWalletFile;

	std::set<int64_t>              setKeyPool;
	std::map<CKeyID, CKeyMetadata> mapKeyMetadata;

	typedef std::map<uint32_t, CMasterKey> MasterKeyMap;
	MasterKeyMap                           mapMasterKeys;
	uint32_t                               nMasterKeyMaxID;

	CWallet() { SetNull(); }
	CWallet(std::string strWalletFileIn) {
		SetNull();

		strWalletFile = strWalletFileIn;
		fFileBacked   = true;
	}
	void SetNull() {
		nWalletVersion      = FEATURE_BASE;
		nWalletMaxVersion   = FEATURE_BASE;
		fFileBacked         = false;
		nMasterKeyMaxID     = 0;
		pwalletdbEncryption = NULL;
		nOrderPosNext       = 0;
		nTimeFirstKey       = 0;
	}

	std::map<uint256, CWalletTx> mapWallet;
	int64_t                      nOrderPosNext;
	std::map<uint256, int>       mapRequestCount;

	std::map<CTxDestination, std::string> mapAddressBook;

	CPubKey          vchDefaultKey;
	int64_t          nTimeFirstKey;
	mutable uint256  nLastHashBestChain;
	mutable int      nLastPegCycle               = 1;
	mutable int      nLastPegSupplyIndex         = 0;
	mutable int      nLastPegSupplyNIndex        = 0;
	mutable int      nLastPegSupplyNNIndex       = 0;
	mutable int      nLastPegSupplyIndexToRecalc = 0;
	mutable uint32_t nLastBlockTime              = 0;

	// check whether we are allowed to upgrade (or already support) to the named feature
	bool CanSupportFeature(enum WalletFeature wf) {
		AssertLockHeld(cs_wallet);
		return nWalletMaxVersion >= wf;
	}

	void AvailableCoinsForStaking(std::vector<COutput>& vCoins, uint32_t nSpendTime) const;
	void AvailableCoins(std::vector<COutput>& vCoins,
	                    bool                  fOnlyConfirmed,
	                    bool                  fUseFrozenUnlocked,
	                    const CCoinControl*   coinControl) const;
	void FrozenCoins(std::vector<COutput>& vCoins,
	                 bool                  fOnlyConfirmed,
	                 bool                  fClearArray,
	                 const CCoinControl*   coinControl) const;
	void StakedCoins(std::vector<COutput>& vCoins,
					 bool                  fOnlyConfirmed,
					 bool                  fClearArray,
					 const CCoinControl*   coinControl) const;
	bool SelectCoinsMinConf(PegTxType                txType,
	                        int64_t                  nTargetValue,
	                        uint32_t                 nSpendTime,
	                        int                      nConfMine,
	                        int                      nConfTheirs,
	                        std::vector<COutput>     vCoins,
	                        std::set<CSelectedCoin>& setCoinsRet,
	                        int64_t&                 nValueRet) const;

	// keystore implementation
	// Generate a new key
	CPubKey GenerateNewKey();
	// Adds a key to the store, and saves it to disk.
	bool AddKeyPubKey(const CKey& key, const CPubKey& pubkey);
	// Adds a key to the store, without saving it to disk (used by LoadWallet)
	bool LoadKey(const CKey& key, const CPubKey& pubkey) {
		return CCryptoKeyStore::AddKeyPubKey(key, pubkey);
	}
	// Load metadata (used by LoadWallet)
	bool LoadKeyMetadata(const CPubKey& pubkey, const CKeyMetadata& metadata);

	bool LoadMinVersion(int nVersion) {
		AssertLockHeld(cs_wallet);
		nWalletVersion    = nVersion;
		nWalletMaxVersion = std::max(nWalletMaxVersion, nVersion);
		return true;
	}

	// Adds an encrypted key to the store, and saves it to disk.
	bool AddCryptedKey(const CPubKey&                    vchPubKey,
	                   const std::vector<unsigned char>& vchCryptedSecret);
	// Adds an encrypted key to the store, without saving it to disk (used by LoadWallet)
	bool LoadCryptedKey(const CPubKey&                    vchPubKey,
	                    const std::vector<unsigned char>& vchCryptedSecret);
	bool AddCScript(const CScript& redeemScript);
	bool LoadCScript(const CScript& redeemScript);

	// Adds a watch-only address to the store, and saves it to disk.
	bool AddWatchOnly(const CTxDestination& dest);
	// Adds a watch-only address to the store, without saving it to disk (used by LoadWallet)
	bool LoadWatchOnly(const CTxDestination& dest);

	bool Unlock(const SecureString& strWalletPassphrase);
	bool ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase,
	                            const SecureString& strNewWalletPassphrase);
	bool EncryptWallet(const SecureString& strWalletPassphrase);

	void GetKeyBirthTimes(std::map<CKeyID, int64_t>& mapKeyBirth) const;

	/** Increment the next transaction order id
	    @return next transaction order id
	 */
	int64_t IncOrderPosNext(CWalletDB* pwalletdb = NULL);

	typedef std::pair<CWalletTx*, CAccountingEntry*> TxPair;
	typedef std::multimap<int64_t, TxPair>           TxItems;

	/** Get the wallet's activity log
	    @return multimap of ordered transactions and accounting entries
	    @warning Returned pointers are *only* valid within the scope of passed acentries
	 */
	TxItems OrderedTxItems(std::list<CAccountingEntry>& acentries, std::string strAccount = "");

	void    MarkDirty();
	bool    AddToWallet(const CWalletTx& wtxIn);
	void    SyncTransaction(const CTransaction& tx,
	                        const CBlock*       pblock,
	                        bool                fConnect,
	                        const MapFractions&);
	bool    AddToWalletIfInvolvingMe(const CTransaction& tx,
	                                 const CBlock*       pblock,
	                                 bool                fUpdate,
	                                 const MapFractions&);
	void    CleanFractionsOfSpentTxouts(const CBlock* pblock);
	void    EraseFromWallet(const uint256& hash);
	void    WalletUpdateSpent(const CTransaction& prevout, bool fBlock = false);
	int     ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate = false);
	void    ReacceptWalletTransactions();
	void    ResendWalletTransactions(bool fForce = false);
	int64_t GetBalance() const;
	int64_t GetReserve() const;
	int64_t GetFrozen(std::vector<CFrozenCoinInfo>* pFrozenCoins = NULL) const;
	int64_t GetLiquidity() const;
	int64_t GetUnconfirmedBalance() const;
	int64_t GetImmatureBalance() const;
	int64_t GetStake() const;
	int64_t GetNewMint() const;
	int     GetPegCycle() const;
	int     GetPegSupplyIndex() const;
	int     GetPegSupplyNIndex() const;
	int     GetPegSupplyNNIndex() const;
	bool    GetRewardInfo(std::vector<RewardInfo>&) const;
	bool    CreateTransaction(PegTxType                                        txType,
	                          const std::vector<std::pair<CScript, int64_t> >& vecSend,
	                          CWalletTx&                                       wtxNew,
	                          int64_t&                                         nFeeRet,
	                          const CCoinControl*                              coinControl,
	                          bool                                             fTest,
	                          std::string&                                     sFailCause);
	bool    CreateTransaction(CScript             scriptPubKey,
	                          int64_t             nValue,
	                          CWalletTx&          wtxNew,
	                          int64_t&            nFeeRet,
	                          const CCoinControl* coinControl = NULL);
	bool    CommitTransaction(CWalletTx& wtxNew);

	uint64_t GetStakeWeight() const;
	bool     CreateCoinStake(const CKeyStore& keystore,
	                         uint32_t         nBits,
	                         int64_t          nSearchInterval,
	                         int64_t          nFees,
	                         CTransaction&    txCoinStake,
	                         CTransaction&    txConsolidate,
	                         CKey&            key,
	                         PegVoteType);

	std::string SendMoney(CScript    scriptPubKey,
	                      int64_t    nValue,
	                      CWalletTx& wtxNew,
	                      bool       fAskFee = false);
	std::string SendMoneyToDestination(const CTxDestination& address,
	                                   int64_t               nValue,
	                                   CWalletTx&            wtxNew,
	                                   bool                  fAskFee = false);

	bool    NewKeyPool();
	bool    TopUpKeyPool(uint32_t nSize = 0);
	int64_t AddReserveKey(const CKeyPool& keypool);
	void    ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool);
	void    KeepKey(int64_t nIndex);
	void    ReturnKey(int64_t nIndex);
	bool    GetKeyFromPool(CPubKey& key);
	void    GetAllReserveKeys(std::set<CKeyID>& setAddress) const;

	std::set<std::set<CTxDestination> > GetAddressGroupings();
	std::map<CTxDestination, int64_t>   GetAddressBalances();

	isminetype IsMine(const CTxIn& txin) const;
	int64_t    GetDebit(const CTxIn& txin) const;
	isminetype IsMine(const CTxOut& txout) const { return ::IsMine(*this, txout.scriptPubKey); }
	int64_t    GetCredit(const CTxOut& txout) const {
		if (!MoneyRange(txout.nValue))
            throw std::runtime_error("CWallet::GetCredit() : value out of range");
        return (IsMine(txout) ? txout.nValue : 0);
	}
	int64_t GetFrozen(uint256                       txhash,
	                  long                          nOut,
	                  const CTxOut&                 txout,
	                  std::vector<CFrozenCoinInfo>* pFrozenCoins = NULL) const;
	int64_t GetReserve(uint256 txhash, long nOut, const CTxOut& txout) const;
	int64_t GetLiquidity(uint256 txhash, long nOut, const CTxOut& txout) const;
	bool    IsChange(const CTxOut& txout) const;
	int64_t GetChange(const CTxOut& txout) const {
		if (!MoneyRange(txout.nValue))
			throw std::runtime_error("CWallet::GetChange() : value out of range");
		return (IsChange(txout) ? txout.nValue : 0);
	}
	bool IsMine(const CTransaction& tx) const {
		for (const CTxOut& txout : tx.vout) {
			if (IsMine(txout) && txout.nValue >= nMinimumInputValue)
				return true;
		}
		return false;
	}
	bool    IsFromMe(const CTransaction& tx) const { return (GetDebit(tx) > 0); }
	int64_t GetDebit(const CTransaction& tx) const {
		int64_t nDebit = 0;
		for (const CTxIn& txin : tx.vin) {
			nDebit += GetDebit(txin);
			if (!MoneyRange(nDebit))
				throw std::runtime_error("CWallet::GetDebit() : value out of range");
		}
		return nDebit;
	}
	int64_t GetCredit(const CTransaction& tx) const {
		int64_t nCredit = 0;
		for (const CTxOut& txout : tx.vout) {
			nCredit += GetCredit(txout);
			if (!MoneyRange(nCredit))
				throw std::runtime_error("CWallet::GetCredit() : value out of range");
		}
		return nCredit;
	}
	int64_t GetChange(const CTransaction& tx) const {
		int64_t nChange = 0;
		for (const CTxOut& txout : tx.vout) {
			nChange += GetChange(txout);
			if (!MoneyRange(nChange))
				throw std::runtime_error("CWallet::GetChange() : value out of range");
		}
		return nChange;
	}
	void SetBestChain(const CBlockLocator& loc);

	DBErrors LoadWallet(bool& fFirstRunRet);

	bool SetAddressBookName(const CTxDestination& address, const std::string& strName);

	bool DelAddressBookName(const CTxDestination& address);

	void UpdatedTransaction(const uint256& hashTx);

	void Inventory(const uint256& hash) {
		{
			LOCK(cs_wallet);
			std::map<uint256, int>::iterator mi = mapRequestCount.find(hash);
			if (mi != mapRequestCount.end())
				(*mi).second++;
		}
	}

	uint32_t GetKeyPoolSize() {
		AssertLockHeld(cs_wallet);  // setKeyPool
		return setKeyPool.size();
	}

	bool SetDefaultKey(const CPubKey& vchPubKey);

	// signify that a particular wallet feature is now used. this may change nWalletVersion and
	// nWalletMaxVersion if those are lower
	bool SetMinVersion(enum WalletFeature, CWalletDB* pwalletdbIn = NULL, bool fExplicit = false);

	// change which version we're allowed to upgrade to (note that this does not immediately imply
	// upgrading to that format)
	bool SetMaxVersion(int nVersion);

	// get the current wallet format (the oldest client version guaranteed to understand this
	// wallet)
	int GetVersion() {
		LOCK(cs_wallet);
		return nWalletVersion;
	}

	void FixSpentCoins(int& nMismatchSpent, int64_t& nBalanceInQuestion, bool fCheckOnly = false);
	void DisableTransaction(const CTransaction& tx);

	/** Address book entry changed.
	 * @note called with lock cs_wallet held.
	 */
	boost::signals2::signal<void(CWallet*              wallet,
	                             const CTxDestination& address,
	                             const std::string&    label,
	                             bool                  isMine,
	                             ChangeType            status)>
	    NotifyAddressBookChanged;

	/** Wallet transaction added, removed or updated.
	 * @note called with lock cs_wallet held.
	 */
	boost::signals2::signal<void(CWallet* wallet, const uint256& hashTx, ChangeType status)>
	    NotifyTransactionChanged;

	// get the current vote type for staking
	PegVoteType GetPegVoteType() {
		LOCK(cs_wallet);
		return pegVoteType;
	}

	// get the current vote type for staking
	bool SetPegVoteType(PegVoteType type) {
		LOCK(cs_wallet);
		pegVoteType = type;
		return true;
	}
	PegVoteType LastAutoVoteType() const {
		LOCK(cs_wallet);
		return lastAutoPegVoteType;
	}
	double LastPeakPrice() const {
		LOCK(cs_wallet);
		return dBayPeakPrice;
	}

	void SetBayRates(std::vector<double>);
	void SetBtcRates(std::vector<double>);
	void SetTrackerVote(PegVoteType, double dPeakRate);

	bool        SetRewardAddress(std::string addr, bool write_wallet);
	std::string GetRewardAddress() const;

	bool SetSupportEnabled(bool on, bool write_wallet);
	bool GetSupportEnabled() const;

	bool        SetSupportAddress(std::string addr, bool write_wallet);
	std::string GetSupportAddress() const;

	bool     SetSupportPart(uint32_t percent, bool write_wallet);
	uint32_t GetSupportPart() const;

	bool SetConsolidateEnabled(bool on, bool write_wallet);
	bool GetConsolidateEnabled() const;
};

/** A key allocated from the key pool. */
class CReserveKey {
protected:
	CWallet* pwallet;
	int64_t  nIndex;
	CPubKey  vchPubKey;

public:
	CReserveKey(CWallet* pwalletIn) {
		nIndex  = -1;
		pwallet = pwalletIn;
	}

	~CReserveKey() { ReturnKey(); }

	void ReturnKey();
	bool GetReservedKey(CPubKey& pubkey);
	void KeepKey();
};

typedef std::map<std::string, std::string> mapValue_t;

static void ReadOrderPos(int64_t& nOrderPos, mapValue_t& mapValue) {
	if (!mapValue.count("n")) {
		nOrderPos = -1;  // TODO: calculate elsewhere
		return;
	}
	nOrderPos = atoi64(mapValue["n"].c_str());
}

static void WriteOrderPos(const int64_t& nOrderPos, mapValue_t& mapValue) {
	if (nOrderPos == -1)
		return;
	mapValue["n"] = i64tostr(nOrderPos);
}

class CFractionsRef {
public:
	CFractionsRef() {}
	CFractionsRef(const CFractionsRef& cp) {
		if (cp.ptr) {
			ptr = std::unique_ptr<CFractions>(new CFractions(*cp.ptr));
		}
	}
	void Init(int64_t value) {
		nValue = value;
		ptr.reset();
	}
	CFractions& Ref() const {
		if (!ptr) {
			ptr = std::unique_ptr<CFractions>(new CFractions(nValue, CFractions::STD));
		}
		return *ptr;
	}
	void UnRef() const {
		if (!ptr)
			return;
		ptr.reset();
	}
	uint32_t nFlags() const {
		if (!ptr)
			return CFractions::VALUE;
		return ptr->nFlags;
	}
	uint64_t nLockTime() const {
		if (!ptr)
			return 0;
		return ptr->nLockTime;
	}

	int64_t                             nValue = 0;
	mutable std::unique_ptr<CFractions> ptr;
};

/** A transaction with a bunch of additional info that only the owner cares about.
 * It includes any unrecorded transactions needed to link it back to the block chain.
 */
class CWalletTx : public CMerkleTx {
private:
	const CWallet* pwallet;

public:
	std::vector<CMerkleTx>                            vtxPrev;
	mapValue_t                                        mapValue;
	std::vector<std::pair<std::string, std::string> > vOrderForm;
	uint32_t                                          fTimeReceivedIsTxTime;
	uint32_t                                          nTimeReceived;  // time received by this node
	uint32_t                                          nTimeSmart;
	char                                              fFromMe;
	std::string                                       strFromAccount;
	std::vector<char>                                 vfSpent;  // which outputs are already spent
	std::vector<CFractionsRef>                        vOutFractions;
	int64_t nOrderPos;  // position in ordered transaction list

	// memory only
	mutable bool                         fDebitCached                   = false;
	mutable bool                         fCreditCached                  = false;
	mutable bool                         fAvailableCreditCached         = false;
	mutable uint64_t                     nLastTimeAvailableFrozenCached = 0;
	mutable bool                         fAvailableReserveCached        = false;
	mutable bool                         fAvailableLiquidityCached      = false;
	mutable bool                         fChangeCached                  = false;
	mutable bool                         fRewardsInfoCached             = false;
	mutable int64_t                      nDebitCached                   = 0;
	mutable int64_t                      nCreditCached                  = 0;
	mutable int64_t                      nAvailableCreditCached         = 0;
	mutable int64_t                      nAvailableFrozenCached         = 0;
	mutable int64_t                      nAvailableReserveCached        = 0;
	mutable int64_t                      nAvailableLiquidityCached      = 0;
	mutable int64_t                      nChangeCached                  = 0;
	mutable std::vector<RewardInfo>      vRewardsInfoCached;
	mutable std::vector<CFrozenCoinInfo> vFrozenCoinInfoCached;

	CWalletTx() { Init(NULL); }

	CWalletTx(const CWallet* pwalletIn) { Init(pwalletIn); }

	CWalletTx(const CWallet* pwalletIn, const CMerkleTx& txIn) : CMerkleTx(txIn) {
		Init(pwalletIn);
	}

	CWalletTx(const CWallet* pwalletIn, const CTransaction& txIn) : CMerkleTx(txIn) {
		Init(pwalletIn);
	}

	void Init(const CWallet* pwalletIn) {
		pwallet = pwalletIn;
		vtxPrev.clear();
		mapValue.clear();
		vOrderForm.clear();
		fTimeReceivedIsTxTime = false;
		nTimeReceived         = 0;
		nTimeSmart            = 0;
		fFromMe               = false;
		strFromAccount.clear();
		vfSpent.clear();
		nOrderPos = -1;
		vRewardsInfoCached.clear();
		vRewardsInfoCached.push_back({PEG_REWARD_5, 0, 0, 0});
		vRewardsInfoCached.push_back({PEG_REWARD_10, 0, 0, 0});
		vRewardsInfoCached.push_back({PEG_REWARD_20, 0, 0, 0});
		vRewardsInfoCached.push_back({PEG_REWARD_40, 0, 0, 0});
		vFrozenCoinInfoCached.clear();
	}

	IMPLEMENT_SERIALIZE(
	    CWalletTx* pthis  = const_cast<CWalletTx*>(this); if (fRead) pthis->Init(NULL);
	    char       fSpent = false;

	    if (!fRead) {
		    pthis->mapValue["fromaccount"] = pthis->strFromAccount;

		    std::string str;
		    for (char f : vfSpent) {
			    str += (f ? '1' : '0');
			    if (f)
				    fSpent = true;
		    }
		    pthis->mapValue["spent"] = str;

		    WriteOrderPos(pthis->nOrderPos, pthis->mapValue);

		    if (nTimeSmart)
			    pthis->mapValue["timesmart"] = strprintf("%u", nTimeSmart);
	    }

	    nSerSize += SerReadWrite(s, *(CMerkleTx*)this, nType, nVersion, ser_action);
	    READWRITE(vtxPrev);
	    READWRITE(mapValue);
	    READWRITE(vOrderForm);
	    READWRITE(fTimeReceivedIsTxTime);
	    READWRITE(nTimeReceived);
	    READWRITE(fFromMe);
	    READWRITE(fSpent);

	    if (fRead) {
		    pthis->strFromAccount = pthis->mapValue["fromaccount"];

		    if (mapValue.count("spent")) {
			    for (char c : pthis->mapValue["spent"]) {
				    pthis->vfSpent.push_back(c != '0');
			    }
		    } else
			    pthis->vfSpent.assign(vout.size(), fSpent);

		    ReadOrderPos(pthis->nOrderPos, pthis->mapValue);

		    pthis->nTimeSmart =
		        mapValue.count("timesmart") ? (uint32_t)atoi64(pthis->mapValue["timesmart"]) : 0;
	    }

	    pthis->mapValue.erase("fromaccount");
	    pthis->mapValue.erase("version");
	    pthis->mapValue.erase("spent");
	    pthis->mapValue.erase("n");
	    pthis->mapValue.erase("timesmart");)

	// marks certain txout's as spent
	// returns true if any update took place
	bool UpdateSpent(const std::vector<char>& vfNewSpent) {
		bool fReturn = false;
		for (uint32_t i = 0; i < vfNewSpent.size(); i++) {
			if (i == vfSpent.size())
				break;

			if (vfNewSpent[i] && !vfSpent[i]) {
				vfSpent[i]                     = true;
				fReturn                        = true;
				fAvailableCreditCached         = false;
				nLastTimeAvailableFrozenCached = 0;
				fAvailableReserveCached        = false;
				fAvailableLiquidityCached      = false;
				fRewardsInfoCached             = false;
			}
		}
		return fReturn;
	}

	// make sure balances are recalculated
	void MarkDirty() {
		fCreditCached                  = false;
		fAvailableCreditCached         = false;
		nLastTimeAvailableFrozenCached = 0;
		fAvailableReserveCached        = false;
		fAvailableLiquidityCached      = false;
		fDebitCached                   = false;
		fChangeCached                  = false;
		fRewardsInfoCached             = false;
	}

	void BindWallet(CWallet* pwalletIn) {
		pwallet = pwalletIn;
		MarkDirty();
	}

	void MarkSpent(uint32_t nOut) {
		if (nOut >= vout.size())
			throw std::runtime_error("CWalletTx::MarkSpent() : nOut out of range");
		vfSpent.resize(vout.size());
		if (!vfSpent[nOut]) {
			vfSpent[nOut]                  = true;
			fAvailableCreditCached         = false;
			nLastTimeAvailableFrozenCached = 0;
			fAvailableReserveCached        = false;
			fAvailableLiquidityCached      = false;
			fRewardsInfoCached             = false;
		}
	}

	void MarkUnspent(uint32_t nOut) {
		if (nOut >= vout.size())
			throw std::runtime_error("CWalletTx::MarkUnspent() : nOut out of range");
		vfSpent.resize(vout.size());
		if (vfSpent[nOut]) {
			vfSpent[nOut]                  = false;
			fAvailableCreditCached         = false;
			nLastTimeAvailableFrozenCached = 0;
			fAvailableReserveCached        = false;
			fAvailableLiquidityCached      = false;
			fRewardsInfoCached             = false;
		}
	}

	bool IsSpent(uint32_t nOut) const {
		if (nOut >= vout.size())
			throw std::runtime_error("CWalletTx::IsSpent() : nOut out of range");
		if (nOut >= vfSpent.size())
			return false;
		return (!!vfSpent[nOut]);
	}

	int64_t GetDebit() const {
		if (vin.empty())
			return 0;
		if (fDebitCached)
			return nDebitCached;
		nDebitCached = pwallet->GetDebit(*this);
		fDebitCached = true;
		return nDebitCached;
	}

	int64_t GetCredit(bool fUseCache = true) const {
		// Must wait until coinbase is safely deep enough in the chain before valuing it
		if ((IsCoinBase() || IsCoinStake()) && GetBlocksToMaturity() > 0)
			return 0;

		// GetBalance can assume transactions in mapWallet won't change
		if (fUseCache && fCreditCached)
			return nCreditCached;
		nCreditCached = pwallet->GetCredit(*this);
		fCreditCached = true;
		return nCreditCached;
	}

	int64_t GetAvailableCredit(bool fUseCache = true) const {
		// Must wait until coinbase is safely deep enough in the chain before valuing it
		if ((IsCoinBase() || IsCoinStake()) && GetBlocksToMaturity() > 0)
			return 0;

		if (fUseCache && fAvailableCreditCached)
			return nAvailableCreditCached;

		int64_t nCredit = 0;
		for (uint32_t i = 0; i < vout.size(); i++) {
			if (!IsSpent(i)) {
				const CTxOut& txout = vout[i];
				nCredit += pwallet->GetCredit(txout);
				if (!MoneyRange(nCredit))
					throw std::runtime_error(
					    "CWalletTx::GetAvailableCredit() : value out of range");
			}
		}

		nAvailableCreditCached = nCredit;
		fAvailableCreditCached = true;
		return nCredit;
	}

	int64_t GetAvailableReserve(bool fUseCache = true) const {
		// Must wait until coinbase is safely deep enough in the chain before valuing it
		if ((IsCoinBase() || IsCoinStake()) && GetBlocksToMaturity() > 0)
			return 0;

		if (fUseCache && fAvailableReserveCached)
			return nAvailableReserveCached;

		int64_t nReserve = 0;
		for (uint32_t i = 0; i < vout.size(); i++) {
			if (!IsSpent(i)) {
				const CTxOut& txout = vout[i];
				if (pwallet->IsMine(txout)) {
					nReserve += pwallet->GetReserve(GetHash(), i, txout);
					if (!MoneyRange(nReserve))
						throw std::runtime_error(
						    "CWalletTx::GetAvailableReserve() : value out of range");
				}
			}
		}

		nAvailableReserveCached = nReserve;
		fAvailableReserveCached = true;
		return nReserve;
	}

	int64_t GetAvailableLiquidity(bool fUseCache = true) const {
		// Must wait until coinbase is safely deep enough in the chain before valuing it
		if ((IsCoinBase() || IsCoinStake()) && GetBlocksToMaturity() > 0)
			return 0;

		if (fUseCache && fAvailableLiquidityCached)
			return nAvailableLiquidityCached;

		int64_t nLiquidity = 0;
		for (uint32_t i = 0; i < vout.size(); i++) {
			if (!IsSpent(i)) {
				const CTxOut& txout = vout[i];
				if (pwallet->IsMine(txout)) {
					nLiquidity += pwallet->GetLiquidity(GetHash(), i, txout);
					if (!MoneyRange(nLiquidity))
						throw std::runtime_error(
						    "CWalletTx::GetAvailableLiquidity() : value out of range");
				}
			}
		}

		nAvailableLiquidityCached = nLiquidity;
		fAvailableLiquidityCached = true;
		return nLiquidity;
	}

	int64_t GetAvailableFrozen(bool                          fUseCache    = true,
	                           std::vector<CFrozenCoinInfo>* pFrozenCoins = NULL) const {
		// Must wait until coinbase is safely deep enough in the chain before valuing it
		if ((IsCoinBase() || IsCoinStake()) && GetBlocksToMaturity() > 0)
			return 0;

		uint64_t nLastTime = pindexBest ? pindexBest->nTime : 0;
		if (fUseCache &&
		    (nLastTimeAvailableFrozenCached > 0 && nLastTime < nLastTimeAvailableFrozenCached)) {
			if (pFrozenCoins) {
				for (auto fcoin : vFrozenCoinInfoCached) {
					pFrozenCoins->push_back(fcoin);
				}
			}
			return nAvailableFrozenCached;
		}

		int64_t                      nFrozen      = 0;
		uint64_t                     nMinLockTime = 0;
		std::vector<CFrozenCoinInfo> vFrozenCoinInfo;
		for (uint32_t i = 0; i < vout.size(); i++) {
			if (!IsSpent(i)) {
				const CTxOut& txout = vout[i];
				if (pwallet->IsMine(txout)) {
					nFrozen += pwallet->GetFrozen(GetHash(), i, txout, &vFrozenCoinInfo);
					if (!MoneyRange(nFrozen))
						throw std::runtime_error(
						    "CWalletTx::GetAvailableFrozen() : value out of range");
				}
			}
		}

		if (!vFrozenCoinInfo.empty()) {
			nMinLockTime = vFrozenCoinInfo.front().nLockTime;
		}
		for (auto fcoin : vFrozenCoinInfo) {
			if (fcoin.nLockTime < nMinLockTime) {
				nMinLockTime = fcoin.nLockTime;
			}
		}

		if (pFrozenCoins) {
			for (auto fcoin : vFrozenCoinInfo) {
				pFrozenCoins->push_back(fcoin);
			}
		}
		vFrozenCoinInfoCached          = vFrozenCoinInfo;
		nAvailableFrozenCached         = nFrozen;
		nLastTimeAvailableFrozenCached = nMinLockTime;
		return nFrozen;
	}

	bool GetRewardInfo(std::vector<RewardInfo>& vRewardsInfo, bool fUseCache = true) const {
		if (vRewardsInfo.size() != PEG_REWARD_LAST)
			return false;

		if (fUseCache && fRewardsInfoCached) {
			for (int i = 0; i < PEG_REWARD_LAST; i++) {
				vRewardsInfo[i].count += vRewardsInfoCached[i].count;
				vRewardsInfo[i].stake += vRewardsInfoCached[i].stake;
				vRewardsInfo[i].amount += vRewardsInfoCached[i].amount;
			}
			return true;
		}

		// clean-up
		for (int i = 0; i < PEG_REWARD_LAST; i++) {
			vRewardsInfoCached[i].count  = 0;
			vRewardsInfoCached[i].stake  = 0;
			vRewardsInfoCached[i].amount = 0;
		}
		int nSupply = pwallet->nLastPegSupplyIndex;
		for (uint32_t i = 0; i < vout.size(); i++) {
			if (!IsSpent(i) && vOutFractions[i].ptr) {
				const CTxOut& txout = vout[i];
				if (pwallet->IsMine(txout)) {
					bool fConfirmed = GetDepthInMainChain() >=
									  Params().MinStakeConfirmations(GetBlockNumInMainChain());
					bool fStake =
					    IsCoinStake() && GetBlocksToMaturity() > 0 && GetDepthInMainChain() > 0;

					if (vOutFractions[i].ptr->nFlags & CFractions::NOTARY_V) {
						if (fStake) {
							vRewardsInfoCached[PEG_REWARD_40].count++;
							vRewardsInfoCached[PEG_REWARD_40].amount += txout.nValue;
							vRewardsInfoCached[PEG_REWARD_40].stake++;
						} else if (fConfirmed) {
							vRewardsInfoCached[PEG_REWARD_40].count++;
							vRewardsInfoCached[PEG_REWARD_40].amount += txout.nValue;
						}
					} else if (vOutFractions[i].ptr->nFlags & CFractions::NOTARY_F) {
						if (fStake) {
							vRewardsInfoCached[PEG_REWARD_20].count++;
							vRewardsInfoCached[PEG_REWARD_20].amount += txout.nValue;
							vRewardsInfoCached[PEG_REWARD_20].stake++;
						} else if (fConfirmed) {
							vRewardsInfoCached[PEG_REWARD_20].count++;
							vRewardsInfoCached[PEG_REWARD_20].amount += txout.nValue;
						}
					} else {
						int64_t reserve   = vOutFractions[i].ptr->Low(nSupply);
						int64_t liquidity = vOutFractions[i].ptr->High(nSupply);
						if (liquidity < reserve) {
							if (fStake) {
								vRewardsInfoCached[PEG_REWARD_10].count++;
								vRewardsInfoCached[PEG_REWARD_10].amount += txout.nValue;
								vRewardsInfoCached[PEG_REWARD_10].stake++;
							} else if (fConfirmed) {
								vRewardsInfoCached[PEG_REWARD_10].count++;
								vRewardsInfoCached[PEG_REWARD_10].amount += txout.nValue;
							}
						} else {
							if (fStake) {
								vRewardsInfoCached[PEG_REWARD_5].count++;
								vRewardsInfoCached[PEG_REWARD_5].amount += txout.nValue;
								vRewardsInfoCached[PEG_REWARD_5].stake++;
							} else if (fConfirmed) {
								vRewardsInfoCached[PEG_REWARD_5].count++;
								vRewardsInfoCached[PEG_REWARD_5].amount += txout.nValue;
							}
						}
					}
				}
			}
		}

		// ready
		for (int i = 0; i < PEG_REWARD_LAST; i++) {
			vRewardsInfo[i].count += vRewardsInfoCached[i].count;
			vRewardsInfo[i].stake += vRewardsInfoCached[i].stake;
			vRewardsInfo[i].amount += vRewardsInfoCached[i].amount;
		}
		return true;
	}

	int64_t GetChange() const {
		if (fChangeCached)
			return nChangeCached;
		nChangeCached = pwallet->GetChange(*this);
		fChangeCached = true;
		return nChangeCached;
	}

	void GetAmounts(std::list<std::pair<CTxDestination, int64_t> >& listReceived,
	                std::list<std::pair<CTxDestination, int64_t> >& listSent,
	                int64_t&                                        nFee,
	                std::string&                                    strSentAccount) const;

	void GetAccountAmounts(const std::string& strAccount,
	                       int64_t&           nReceived,
	                       int64_t&           nSent,
	                       int64_t&           nFee) const;

	bool IsFromMe() const { return (GetDebit() > 0); }

	bool IsTrusted() const {
		// Quick answer in most cases
		if (!IsFinalTx(*this))
			return false;
		int nDepth = GetDepthInMainChain();
		if (nDepth >= 1)
			return true;
		if (nDepth < 0)
			return false;
		if (fConfChange || !IsFromMe())  // using wtx's cached debit
			return false;

		// If no confirmations but it's from us, we can still
		// consider it confirmed if all dependencies are confirmed
		std::map<uint256, const CMerkleTx*> mapPrev;
		std::vector<const CMerkleTx*>       vWorkQueue;
		vWorkQueue.reserve(vtxPrev.size() + 1);
		vWorkQueue.push_back(this);
		for (uint32_t i = 0; i < vWorkQueue.size(); i++) {
			const CMerkleTx* ptx = vWorkQueue[i];

			if (!IsFinalTx(*ptx))
				return false;
			int nPDepth = ptx->GetDepthInMainChain();
			if (nPDepth >= 1)
				continue;
			if (nPDepth < 0)
				return false;
			if (!pwallet->IsFromMe(*ptx))
				return false;

			if (mapPrev.empty()) {
				for (const CMerkleTx& tx : vtxPrev) {
					mapPrev[tx.GetHash()] = &tx;
				}
			}

			for (const CTxIn& txin : ptx->vin) {
				if (!mapPrev.count(txin.prevout.hash))
					return false;
				vWorkQueue.push_back(mapPrev[txin.prevout.hash]);
			}
		}

		return true;
	}

	bool WriteToDisk();

	int64_t GetTxTime() const;
	int     GetRequestCount() const;

	void AddSupportingTransactions(CTxDB& txdb);

	bool AcceptWalletTransaction(CTxDB& txdb);
	bool AcceptWalletTransaction();

	void RelayWalletTransaction(CTxDB& txdb);
	void RelayWalletTransaction();
};

class COutput {
public:
	const CWalletTx* tx;
	int              i;
	int              nDepth;
	bool             fSpendable;

	COutput(const CWalletTx* txIn, int iIn, int nDepthIn, bool fSpendableIn) {
		tx         = txIn;
		i          = iIn;
		nDepth     = nDepthIn;
		fSpendable = fSpendableIn;
	}

	std::string ToString() const {
		return strprintf("COutput(%s, %d, %d) [%s]", tx->GetHash().ToString(), i, nDepth,
		                 FormatMoney(tx->vout[i].nValue));
	}

	uint64_t FrozenUnlockTime() const;
	bool     IsFrozen(uint32_t nLastBlockTime) const;
	bool     IsFrozenMark() const;
	bool     IsColdMark() const;
};

class CSelectedCoin {
public:
	const CWalletTx* tx;
	uint32_t         i;
	int64_t          nAvailableValue;

	friend bool operator<(const CSelectedCoin& a, const CSelectedCoin& b) {
		if (a.tx < b.tx)
			return true;
		if (a.tx == b.tx && a.i < b.i)
			return true;
		if (a.tx == b.tx && a.i == b.i && a.nAvailableValue < b.nAvailableValue)
			return true;
		return false;
	}
};

/** Private key that includes an expiration date in case it never gets used. */
class CWalletKey {
public:
	CPrivKey    vchPrivKey;
	int64_t     nTimeCreated;
	int64_t     nTimeExpires;
	std::string strComment;
	//// todo: add something to note what created it (user, getnewaddress, change)
	////   maybe should have a map<string, string> property map

	CWalletKey(int64_t nExpires = 0) {
		nTimeCreated = (nExpires ? GetTime() : 0);
		nTimeExpires = nExpires;
	}

	IMPLEMENT_SERIALIZE(if (!(nType & SER_GETHASH)) READWRITE(nVersion); READWRITE(vchPrivKey);
	                    READWRITE(nTimeCreated);
	                    READWRITE(nTimeExpires);
	                    READWRITE(strComment);)
};

/** Account information.
 * Stored in wallet with key "acc"+string account name.
 */
class CAccount {
public:
	CPubKey vchPubKey;

	CAccount() { SetNull(); }

	void SetNull() { vchPubKey = CPubKey(); }

	IMPLEMENT_SERIALIZE(if (!(nType & SER_GETHASH)) READWRITE(nVersion); READWRITE(vchPubKey);)
};

/** Internal transfers.
 * Database key is acentry<account><counter>.
 */
class CAccountingEntry {
public:
	std::string strAccount;
	int64_t     nCreditDebit;
	int64_t     nTime;
	std::string strOtherAccount;
	std::string strComment;
	mapValue_t  mapValue;
	int64_t     nOrderPos;  // position in ordered transaction list
	uint64_t    nEntryNo;

	CAccountingEntry() { SetNull(); }

	void SetNull() {
		nCreditDebit = 0;
		nTime        = 0;
		strAccount.clear();
		strOtherAccount.clear();
		strComment.clear();
		nOrderPos = -1;
	}

	IMPLEMENT_SERIALIZE(
	    CAccountingEntry& me = *const_cast<CAccountingEntry*>(this);
	    if (!(nType & SER_GETHASH)) READWRITE(nVersion);
	    // Note: strAccount is serialized as part of the key, not here.
	    READWRITE(nCreditDebit);
	    READWRITE(nTime);
	    READWRITE(strOtherAccount);

	    if (!fRead) {
		    WriteOrderPos(nOrderPos, me.mapValue);

		    if (!(mapValue.empty() && _ssExtra.empty())) {
			    CDataStream ss(nType, nVersion);
			    ss.insert(ss.begin(), '\0');
			    ss << mapValue;
			    ss.insert(ss.end(), _ssExtra.begin(), _ssExtra.end());
			    me.strComment.append(ss.str());
		    }
	    }

	    READWRITE(strComment);

	    size_t nSepPos = strComment.find("\0", 0, 1);
	    if (fRead) {
		    me.mapValue.clear();
		    if (std::string::npos != nSepPos) {
			    CDataStream ss(
			        std::vector<char>(strComment.begin() + nSepPos + 1, strComment.end()), nType,
			        nVersion);
			    ss >> me.mapValue;
			    me._ssExtra = std::vector<char>(ss.begin(), ss.end());
		    }
		    ReadOrderPos(me.nOrderPos, me.mapValue);
	    } if (std::string::npos != nSepPos) me.strComment.erase(nSepPos);

	    me.mapValue.erase("n");)

private:
	std::vector<char> _ssExtra;
};

#endif
