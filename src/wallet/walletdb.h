// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_WALLETDB_H
#define BITCOIN_WALLETDB_H

#include "db.h"
#include "key.h"
#include "keystore.h"

#include <list>
#include <string>
#include <utility>
#include <vector>

class CAccount;
class CAccountingEntry;
class CBlockLocator;
class CKeyPool;
class CMasterKey;
class CScript;
class CWallet;
class CWalletTx;
class uint160;
class uint256;

/** Error statuses for the wallet database */
enum DBErrors {
	DB_LOAD_OK,
	DB_CORRUPT,
	DB_NONCRITICAL_ERROR,
	DB_TOO_NEW,
	DB_LOAD_FAIL,
	DB_NEED_REWRITE
};

class CKeyMetadata {
public:
	static const int CURRENT_VERSION = 1;
	int              nVersion;
	int64_t          nCreateTime;  // 0 means unknown

	CKeyMetadata() { SetNull(); }
	CKeyMetadata(int64_t nCreateTime_) {
		nVersion    = CKeyMetadata::CURRENT_VERSION;
		nCreateTime = nCreateTime_;
	}

	IMPLEMENT_SERIALIZE(READWRITE(this->nVersion); nVersion = this->nVersion;
	                    READWRITE(nCreateTime);)

	void SetNull() {
		nVersion    = CKeyMetadata::CURRENT_VERSION;
		nCreateTime = 0;
	}
};

/** Access to the wallet database (wallet.dat) */
class CWalletDB : public CDB {
public:
	CWalletDB(const std::string& strFilename, const char* pszMode = "r+")
	    : CDB(strFilename, pszMode) {}

private:
	CWalletDB(const CWalletDB&);
	void operator=(const CWalletDB&);

public:
	bool WriteName(const std::string& strAddress, const std::string& strName);

	bool EraseName(const std::string& strAddress);

	bool WriteTx(uint256 hash, const CWalletTx& wtx);
	bool EraseTx(uint256 hash);

	bool WriteKey(const CPubKey&      vchPubKey,
	              const CPrivKey&     vchPrivKey,
	              const CKeyMetadata& keyMeta);
	bool WriteCryptedKey(const CPubKey&                    vchPubKey,
	                     const std::vector<unsigned char>& vchCryptedSecret,
	                     const CKeyMetadata&               keyMeta);
	bool WriteMasterKey(uint32_t nID, const CMasterKey& kMasterKey);

	bool WriteCScript(const uint160& hash, const CScript& redeemScript);

	bool WriteWatchOnly(const CTxDestination& dest);

	bool WriteBestBlock(const CBlockLocator& locator);
	bool ReadBestBlock(CBlockLocator& locator);

	bool WriteOrderPosNext(int64_t nOrderPosNext);

	bool WriteDefaultKey(const CPubKey& vchPubKey);

	bool ReadPool(int64_t nPool, CKeyPool& keypool);
	bool WritePool(int64_t nPool, const CKeyPool& keypool);
	bool ErasePool(int64_t nPool);

	bool WriteMinVersion(int nVersion);

	bool ReadAccount(const std::string& strAccount, CAccount& account);
	bool WriteAccount(const std::string& strAccount, const CAccount& account);

	bool ReadRewardAddress(std::string& addr);
	bool WriteRewardAddress(const std::string& addr);

	bool ReadSupportEnabled(bool& on);
	bool WriteSupportEnabled(bool on);

	bool ReadSupportAddress(std::string& addr);
	bool WriteSupportAddress(const std::string& addr);

	bool ReadSupportPart(uint32_t& addr);
	bool WriteSupportPart(const uint32_t& addr);

	bool ReadConsolidateEnabled(bool& on);
	bool WriteConsolidateEnabled(bool on);

	// votes/proposals

	// read/write proposals count?

	bool ReadProposals(std::vector<std::string>& ids);
	bool WriteProposals(const std::vector<std::string>& ids);
	bool ReadProposal(const std::string& phash, std::vector<std::string>& datas);
	bool WriteProposal(const std::string& phash, const std::vector<std::string>& datas);
    
    bool ReadCompletedMerkleOutIdx(const std::string& brhash, int idx, std::string& merklehash);
    bool WriteCompletedMerkleOutIdx(const std::string& brhash, int idx, std::string merklehash);
	bool ReadCompletedMerkleInNonce(const std::string& brhash, int nonce, bool& completed);
	bool WriteCompletedMerkleInNonce(const std::string& brhash, int nonce, bool completed);
	bool ReadCompletedMerkleInTxsNonce(const std::string& brhash, int nonce, bool& completed);
	bool WriteCompletedMerkleInTxsNonce(const std::string& brhash, int nonce, bool completed);

	bool ReadBridgeIsAutomated(const std::string& brhash,
	                           bool&              is_automated,
	                           double&            max_priority_fee_per_gas_gwei,
	                           double&            max_fee_per_gas_gwei);
	bool WriteBridgeIsAutomated(const std::string& brhash,
	                            bool               is_automated,
	                            double             max_priority_fee_per_gas_gwei,
	                            double             max_fee_per_gas_gwei);

private:
	bool WriteAccountingEntry(const uint64_t nAccEntryNum, const CAccountingEntry& acentry);

public:
	bool    WriteAccountingEntry(const CAccountingEntry& acentry);
	int64_t GetAccountCreditDebit(const std::string& strAccount);
	void    ListAccountCreditDebit(const std::string&           strAccount,
	                               std::list<CAccountingEntry>& acentries);

	DBErrors    ReorderTransactions(CWallet*);
	DBErrors    LoadWallet(CWallet* pwallet);
	static bool Recover(CDBEnv& dbenv, std::string filename, bool fOnlyKeys);
	static bool Recover(CDBEnv& dbenv, std::string filename);
};

bool BackupWallet(const CWallet& wallet, const std::string& strDest);

#endif  // BITCOIN_WALLETDB_H
