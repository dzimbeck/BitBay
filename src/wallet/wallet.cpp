// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet.h"

#include "base58.h"
#include "coincontrol.h"
#include "kernel.h"
#include "net.h"
#include "timedata.h"
#include "txdb.h"
#include "ui_interface.h"
#include "walletdb.h"

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string.hpp>

#include <boost/range/algorithm.hpp>
#include <boost/numeric/ublas/matrix.hpp>

using namespace std;

// Settings
int64_t nTransactionFee = MIN_TX_FEE;
int64_t nNoStakeBalance = 0;
int64_t nMinimumInputValue = 0;

int64_t gcd(int64_t n,int64_t m) { return m == 0 ? n : gcd(m, n % m); }

//////////////////////////////////////////////////////////////////////////////
//
// mapWallet
//

struct CompareValueOnly
{
    bool operator()(const pair<int64_t, pair<const CWalletTx*, unsigned int> >& t1,
                    const pair<int64_t, pair<const CWalletTx*, unsigned int> >& t2) const
    {
        return t1.first < t2.first;
    }
    bool operator()(const pair<int64_t, CSelectedCoin>& t1,
                    const pair<int64_t, CSelectedCoin>& t2) const
    {
        return t1.first < t2.first;
    }
};

CPubKey CWallet::GenerateNewKey()
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    bool fCompressed = CanSupportFeature(FEATURE_COMPRPUBKEY); // default to compressed public keys if we want 0.6.0 wallets

    RandAddSeedPerfmon();
    CKey secret;
    secret.MakeNewKey(fCompressed);

    // Compressed public keys were introduced in version 0.6.0
    if (fCompressed)
        SetMinVersion(FEATURE_COMPRPUBKEY);

    CPubKey pubkey = secret.GetPubKey();

    // Create new metadata
    int64_t nCreationTime = GetTime();
    mapKeyMetadata[pubkey.GetID()] = CKeyMetadata(nCreationTime);
    if (!nTimeFirstKey || nCreationTime < nTimeFirstKey)
        nTimeFirstKey = nCreationTime;

    if (!AddKeyPubKey(secret, pubkey))
        throw std::runtime_error("CWallet::GenerateNewKey() : AddKey failed");
    return pubkey;
}

bool CWallet::AddKeyPubKey(const CKey& secret, const CPubKey &pubkey)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (!CCryptoKeyStore::AddKeyPubKey(secret, pubkey))
        return false;
    if (!fFileBacked)
        return true;
    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteKey(pubkey, secret.GetPrivKey(), mapKeyMetadata[pubkey.GetID()]);
    }
    return true;
}

bool CWallet::AddCryptedKey(const CPubKey &vchPubKey, const vector<unsigned char> &vchCryptedSecret)
{
    if (!CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret))
        return false;
    if (!fFileBacked)
        return true;
    {
        LOCK(cs_wallet);
        if (pwalletdbEncryption)
            return pwalletdbEncryption->WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
        else
            return CWalletDB(strWalletFile).WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
    }
    return false;
}

bool CWallet::LoadKeyMetadata(const CPubKey &pubkey, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
        nTimeFirstKey = meta.nCreateTime;

    mapKeyMetadata[pubkey.GetID()] = meta;
    return true;
}

bool CWallet::LoadCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret);
}

bool CWallet::AddCScript(const CScript& redeemScript)
{
    if (!CCryptoKeyStore::AddCScript(redeemScript))
        return false;
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteCScript(Hash160(redeemScript), redeemScript);
}

// optional setting to unlock wallet for staking only
// serves to disable the trivial sendmoney when OS account compromised
// provides no real security
bool fWalletUnlockStakingOnly = false;

bool CWallet::LoadCScript(const CScript& redeemScript)
{
    /* A sanity check was added in pull #3843 to avoid adding redeemScripts
     * that never can be redeemed. However, old wallets may still contain
     * these. Do not add them to the wallet and warn. */
    if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
    {
        std::string strAddr = CBitcoinAddress(redeemScript.GetID()).ToString();
        LogPrintf("%s: Warning: This wallet contains a redeemScript of size %u which exceeds maximum size %i thus can never be redeemed. Do not use address %s.\n",
            __func__, redeemScript.size(), MAX_SCRIPT_ELEMENT_SIZE, strAddr);
        return true;
    }

    return CCryptoKeyStore::AddCScript(redeemScript);
}

bool CWallet::AddWatchOnly(const CTxDestination &dest)
{
    if (!CCryptoKeyStore::AddWatchOnly(dest))
        return false;
    nTimeFirstKey = 1; // No birthday information for watch-only keys.
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteWatchOnly(dest);
}

bool CWallet::LoadWatchOnly(const CTxDestination &dest)
{
    LogPrintf("Loaded %s!\n", CBitcoinAddress(dest).ToString().c_str());
    return CCryptoKeyStore::AddWatchOnly(dest);
}

bool CWallet::Unlock(const SecureString& strWalletPassphrase)
{
    CCrypter crypter;
    CKeyingMaterial vMasterKey;

    {
        LOCK(cs_wallet);
        for(const MasterKeyMap::value_type& pMasterKey : mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
                continue; // try another master key
            if (CCryptoKeyStore::Unlock(vMasterKey))
                return true;
        }
    }
    return false;
}

bool CWallet::ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase)
{
    bool fWasLocked = IsLocked();

    {
        LOCK(cs_wallet);
        Lock();

        CCrypter crypter;
        CKeyingMaterial vMasterKey;
        for(MasterKeyMap::value_type& pMasterKey : mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strOldWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
                return false;
            if (CCryptoKeyStore::Unlock(vMasterKey))
            {
                int64_t nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = pMasterKey.second.nDeriveIterations * (100 / ((double)(GetTimeMillis() - nStartTime)));

                nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = (pMasterKey.second.nDeriveIterations + pMasterKey.second.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) / 2;

                if (pMasterKey.second.nDeriveIterations < 25000)
                    pMasterKey.second.nDeriveIterations = 25000;

                LogPrintf("Wallet passphrase changed to an nDeriveIterations of %i\n", pMasterKey.second.nDeriveIterations);

                if (!crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                    return false;
                if (!crypter.Encrypt(vMasterKey, pMasterKey.second.vchCryptedKey))
                    return false;
                CWalletDB(strWalletFile).WriteMasterKey(pMasterKey.first, pMasterKey.second);
                if (fWasLocked)
                    Lock();
                return true;
            }
        }
    }

    return false;
}

void CWallet::SetBestChain(const CBlockLocator& loc)
{
    CWalletDB walletdb(strWalletFile);
    walletdb.WriteBestBlock(loc);
    {
        LOCK(cs_main);
        CBlockIndex* pblockindex = mapBlockIndex.ref(hashBestChain);
        if (pblockindex && pblockindex->nPegSupplyIndex != nLastPegSupplyIndexToRecalc) {
            nLastPegSupplyIndexToRecalc = pblockindex->nPegSupplyIndex;
            nLastBlockTime = pblockindex->nTime;
            MarkDirty();
        }
    }
}

bool CWallet::SetMinVersion(enum WalletFeature nVersion, CWalletDB* pwalletdbIn, bool fExplicit)
{
    LOCK(cs_wallet); // nWalletVersion
    if (nWalletVersion >= nVersion)
        return true;

    // when doing an explicit upgrade, if we pass the max version permitted, upgrade all the way
    if (fExplicit && nVersion > nWalletMaxVersion)
            nVersion = FEATURE_LATEST;

    nWalletVersion = nVersion;

    if (nVersion > nWalletMaxVersion)
        nWalletMaxVersion = nVersion;

    if (fFileBacked)
    {
        CWalletDB* pwalletdb = pwalletdbIn ? pwalletdbIn : new CWalletDB(strWalletFile);
        if (nWalletVersion > 40000)
            pwalletdb->WriteMinVersion(nWalletVersion);
        if (!pwalletdbIn)
            delete pwalletdb;
    }

    return true;
}

bool CWallet::SetMaxVersion(int nVersion)
{
    LOCK(cs_wallet); // nWalletVersion, nWalletMaxVersion
    // cannot downgrade below current version
    if (nWalletVersion > nVersion)
        return false;

    nWalletMaxVersion = nVersion;

    return true;
}

bool CWallet::EncryptWallet(const SecureString& strWalletPassphrase)
{
    if (IsCrypted())
        return false;

    CKeyingMaterial vMasterKey;
    RandAddSeedPerfmon();

    vMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
    RAND_bytes(&vMasterKey[0], WALLET_CRYPTO_KEY_SIZE);

    CMasterKey kMasterKey(0);

    RandAddSeedPerfmon();
    kMasterKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
    RAND_bytes(&kMasterKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE);

    CCrypter crypter;
    int64_t nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, 25000, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = 2500000 / ((double)(GetTimeMillis() - nStartTime));

    nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = (kMasterKey.nDeriveIterations + kMasterKey.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) / 2;

    if (kMasterKey.nDeriveIterations < 25000)
        kMasterKey.nDeriveIterations = 25000;

    LogPrintf("Encrypting Wallet with an nDeriveIterations of %i\n", kMasterKey.nDeriveIterations);

    if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod))
        return false;
    if (!crypter.Encrypt(vMasterKey, kMasterKey.vchCryptedKey))
        return false;

    {
        LOCK(cs_wallet);
        mapMasterKeys[++nMasterKeyMaxID] = kMasterKey;
        if (fFileBacked)
        {
            pwalletdbEncryption = new CWalletDB(strWalletFile);
            if (!pwalletdbEncryption->TxnBegin())
                return false;
            pwalletdbEncryption->WriteMasterKey(nMasterKeyMaxID, kMasterKey);
        }

        if (!EncryptKeys(vMasterKey))
        {
            if (fFileBacked)
                pwalletdbEncryption->TxnAbort();
            exit(1); //We now probably have half of our keys encrypted in memory, and half not...die and let the user reload their unencrypted wallet.
        }

        // Encryption was introduced in version 0.4.0
        SetMinVersion(FEATURE_WALLETCRYPT, pwalletdbEncryption, true);

        if (fFileBacked)
        {
            if (!pwalletdbEncryption->TxnCommit())
                exit(1); //We now have keys encrypted in memory, but no on disk...die to avoid confusion and let the user reload their unencrypted wallet.

            delete pwalletdbEncryption;
            pwalletdbEncryption = NULL;
        }

        Lock();
        Unlock(strWalletPassphrase);
        NewKeyPool();
        Lock();

        // Need to completely rewrite the wallet file; if we don't, bdb might keep
        // bits of the unencrypted private key in slack space in the database file.
        CDB::Rewrite(strWalletFile);

    }
    NotifyStatusChanged(this);

    return true;
}

int64_t CWallet::IncOrderPosNext(CWalletDB *pwalletdb)
{
    AssertLockHeld(cs_wallet); // nOrderPosNext
    int64_t nRet = nOrderPosNext++;
    if (pwalletdb) {
        pwalletdb->WriteOrderPosNext(nOrderPosNext);
    } else {
        CWalletDB(strWalletFile).WriteOrderPosNext(nOrderPosNext);
    }
    return nRet;
}

CWallet::TxItems CWallet::OrderedTxItems(std::list<CAccountingEntry>& acentries, std::string strAccount)
{
    AssertLockHeld(cs_wallet); // mapWallet
    CWalletDB walletdb(strWalletFile);

    // First: get all CWalletTx and CAccountingEntry into a sorted-by-order multimap.
    TxItems txOrdered;

    // Note: maintaining indices in the database of (account,time) --> txid and (account, time) --> acentry
    // would make this much faster for applications that do this a lot.
    for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
    {
        CWalletTx* wtx = &((*it).second);
        txOrdered.insert(make_pair(wtx->nOrderPos, TxPair(wtx, (CAccountingEntry*)0)));
    }
    acentries.clear();
    walletdb.ListAccountCreditDebit(strAccount, acentries);
    for(CAccountingEntry& entry : acentries)
    {
        txOrdered.insert(make_pair(entry.nOrderPos, TxPair((CWalletTx*)0, &entry)));
    }

    return txOrdered;
}

void CWallet::WalletUpdateSpent(const CTransaction &tx, bool fBlock)
{
    // Anytime a signature is successfully verified, it's proof the outpoint is spent.
    // Update the wallet spent flag if it doesn't know due to wallet.dat being
    // restored from backup or the user making copies of wallet.dat.
    {
        LOCK(cs_wallet);
        for(const CTxIn& txin : tx.vin)
        {
            map<uint256, CWalletTx>::iterator mi = mapWallet.find(txin.prevout.hash);
            if (mi != mapWallet.end())
            {
                CWalletTx& wtx = (*mi).second;
                if (txin.prevout.n >= wtx.vout.size())
                    LogPrintf("WalletUpdateSpent: bad wtx %s\n", wtx.GetHash().ToString());
                else if (!wtx.IsSpent(txin.prevout.n) && IsMine(wtx.vout[txin.prevout.n]))
                {
                    LogPrintf("WalletUpdateSpent found spent coin %s BAY %s\n", FormatMoney(wtx.GetCredit()), wtx.GetHash().ToString());
                    wtx.MarkSpent(txin.prevout.n);
                    wtx.WriteToDisk();
                    NotifyTransactionChanged(this, txin.prevout.hash, CT_UPDATED);
                }
            }
        }

        if (fBlock)
        {
            uint256 hash = tx.GetHash();
            map<uint256, CWalletTx>::iterator mi = mapWallet.find(hash);
            CWalletTx& wtx = (*mi).second;

            for(const CTxOut& txout : tx.vout)
            {
                if (IsMine(txout))
                {
                    wtx.MarkUnspent(&txout - &tx.vout[0]);
                    wtx.WriteToDisk();
                    NotifyTransactionChanged(this, hash, CT_UPDATED);
                }
            }
        }

    }
}

void CWallet::MarkDirty()
{
    {
        LOCK(cs_wallet);
        for(std::pair<const uint256, CWalletTx> & item : mapWallet) {
            item.second.MarkDirty();
        }
    }
}

bool CWallet::AddToWallet(const CWalletTx& wtxIn)
{
    uint256 hash = wtxIn.GetHash();
    {
        LOCK(cs_wallet);
        // Inserts only if not already there, returns tx inserted or tx found
        pair<map<uint256, CWalletTx>::iterator, bool> ret = mapWallet.insert(make_pair(hash, wtxIn));
        CWalletTx& wtx = (*ret.first).second;
        wtx.BindWallet(this);
        bool fInsertedNew = ret.second;
        if (fInsertedNew)
        {
            wtx.nTimeReceived = GetAdjustedTime();
            wtx.nOrderPos = IncOrderPosNext();

            wtx.nTimeSmart = wtx.nTimeReceived;
            if (wtxIn.hashBlock != 0)
            {
                if (mapBlockIndex.count(wtxIn.hashBlock))
                {
                    unsigned int latestNow = wtx.nTimeReceived;
                    unsigned int latestEntry = 0;
                    {
                        // Tolerate times up to the last timestamp in the wallet not more than 5 minutes into the future
                        int64_t latestTolerated = latestNow + 300;
                        std::list<CAccountingEntry> acentries;
                        TxItems txOrdered = OrderedTxItems(acentries);
                        for (TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
                        {
                            CWalletTx *const pwtx = (*it).second.first;
                            if (pwtx == &wtx)
                                continue;
                            CAccountingEntry *const pacentry = (*it).second.second;
                            int64_t nSmartTime;
                            if (pwtx)
                            {
                                nSmartTime = pwtx->nTimeSmart;
                                if (!nSmartTime)
                                    nSmartTime = pwtx->nTimeReceived;
                            }
                            else
                                nSmartTime = pacentry->nTime;
                            if (nSmartTime <= latestTolerated)
                            {
                                latestEntry = nSmartTime;
                                if (nSmartTime > latestNow)
                                    latestNow = nSmartTime;
                                break;
                            }
                        }
                    }

                    unsigned int& blocktime = mapBlockIndex.ref(wtxIn.hashBlock)->nTime;
                    wtx.nTimeSmart = std::max(latestEntry, std::min(blocktime, latestNow));
                }
                else
                    LogPrintf("AddToWallet() : found %s in block %s not in index\n",
                             wtxIn.GetHash().ToString(),
                             wtxIn.hashBlock.ToString());
            }
        }

        bool fUpdated = false;
        if (!fInsertedNew)
        {
            // Merge
            if (wtxIn.hashBlock != 0 && wtxIn.hashBlock != wtx.hashBlock)
            {
                wtx.hashBlock = wtxIn.hashBlock;
                fUpdated = true;
            }
            if (wtxIn.nIndex != -1 && (wtxIn.vMerkleBranch != wtx.vMerkleBranch || wtxIn.nIndex != wtx.nIndex))
            {
                wtx.vMerkleBranch = wtxIn.vMerkleBranch;
                wtx.nIndex = wtxIn.nIndex;
                fUpdated = true;
            }
            if (wtxIn.fFromMe && wtxIn.fFromMe != wtx.fFromMe)
            {
                wtx.fFromMe = wtxIn.fFromMe;
                fUpdated = true;
            }
            fUpdated |= wtx.UpdateSpent(wtxIn.vfSpent);
        }

        //// debug print
        LogPrintf("AddToWallet %s  %s%s\n", wtxIn.GetHash().ToString(), (fInsertedNew ? "new" : ""), (fUpdated ? "update" : ""));

        // Write to disk
        if (fInsertedNew || fUpdated)
            if (!wtx.WriteToDisk())
                return false;

        if (!fHaveGUI) {
            // If default receiving address gets used, replace it with a new one
            if (vchDefaultKey.IsValid()) {
                CScript scriptDefaultKey;
                scriptDefaultKey.SetDestination(vchDefaultKey.GetID());
                for(const CTxOut& txout : wtx.vout)
                {
                    if (txout.scriptPubKey == scriptDefaultKey)
                    {
                        CPubKey newDefaultKey;
                        if (GetKeyFromPool(newDefaultKey))
                        {
                            SetDefaultKey(newDefaultKey);
                            SetAddressBookName(vchDefaultKey.GetID(), "");
                        }
                    }
                }
            }
        }
        // since AddToWallet is called directly for self-originating transactions, check for consumption of own coins
        WalletUpdateSpent(wtx, (wtxIn.hashBlock != 0));

        // Notify UI of new or updated transaction
        NotifyTransactionChanged(this, hash, fInsertedNew ? CT_NEW : CT_UPDATED);

        // notify an external script when a wallet transaction comes in or is updated
        std::string strCmd = GetArg("-walletnotify", "");

        if ( !strCmd.empty())
        {
            boost::replace_all(strCmd, "%s", wtxIn.GetHash().GetHex());
            boost::thread t(runCommand, strCmd); // thread runs free
        }

    }
    return true;
}

// Add a transaction to the wallet, or update it.
// pblock is optional, but should be provided if the transaction is known to be in a block.
// If fUpdate is true, existing transactions will be updated.
bool CWallet::AddToWalletIfInvolvingMe(const CTransaction& tx, 
                                       const CBlock* pblock, 
                                       bool fUpdate,
                                       const MapFractions& mapOutputFractions)
{
    uint256 hash = tx.GetHash();
    {
        LOCK(cs_wallet);
        bool fExisted = mapWallet.count(hash);
        if (fExisted && !fUpdate) return false;
        if (fExisted || IsMine(tx) || IsFromMe(tx))
        {
            CWalletTx wtx(this,tx);
            uint256 txhash = wtx.GetHash();
            wtx.vOutFractions.resize(wtx.vout.size());
            for(size_t i=0; i < wtx.vout.size(); i++) {
                wtx.vOutFractions[i].Init(wtx.vout[i].nValue);
                auto fkey = uint320(txhash, i);
                if (mapOutputFractions.find(fkey) != mapOutputFractions.end()) {
                    CFractions& fractions = wtx.vOutFractions[i].Ref();
                    fractions = mapOutputFractions.at(fkey);
                }
            }
            // Get merkle branch if transaction was found in a block
            if (pblock)
                wtx.SetMerkleBranch(pblock);
            return AddToWallet(wtx);
        }
        else
            WalletUpdateSpent(tx);
    }
    return false;
}

void CWallet::CleanFractionsOfSpentTxouts(const CBlock* pBlockRef)
{
    if (!pBlockRef) 
        return;
    uint256 hashBlock = pBlockRef->GetHash();
    LOCK(cs_main);
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return;
    CBlockIndex* pblockindex = (*mi).second;
    if (!pblockindex)
        return;
    int nHeightRef = pblockindex->nHeight;
    int nHeight = nHeightRef - Params().MaxReorganizationDepth();
    if (nHeight <= 0)
        return;
    while(pblockindex->nHeight > nHeight) {
        pblockindex = pblockindex->pprev;
        if (!pblockindex) 
            return;
    }
    CBlock block;
    if (!block.ReadFromDisk(pblockindex, true))
        return;
    for(const CTransaction & tx : block.vtx) {
        LOCK(cs_wallet);
        // check if this tx spents mine wallet txout
        for(size_t i=0; i < tx.vin.size(); i++) {
            const CTxIn & txin = tx.vin[i];
            std::map<uint256, CWalletTx>::iterator it = mapWallet.find(txin.prevout.hash);
            if (it == mapWallet.end())
                continue;
            CWalletTx* wtx = &((*it).second);
            unsigned int nout = txin.prevout.n;
            if (nout >= wtx->vfSpent.size()) continue;
            if (nout >= wtx->vOutFractions.size()) continue;
            wtx->vOutFractions[nout].UnRef();
        }
    }
}

void CWallet::SyncTransaction(const CTransaction& tx, 
                              const CBlock* pblock, 
                              bool fConnect,
                              const MapFractions& mapOutputFractions) {
    if (!fConnect)
    {
        // wallets need to refund inputs when disconnecting coinstake
        if (tx.IsCoinStake())
        {
            if (IsFromMe(tx))
                DisableTransaction(tx);
        }
        return;
    }

    AddToWalletIfInvolvingMe(tx, pblock, true, mapOutputFractions);
    
    if (tx.IsCoinStake()) {
        // as once per block use coin stake tx
        CleanFractionsOfSpentTxouts(pblock);
    }
}

void CWallet::EraseFromWallet(const uint256 &hash)
{
    if (!fFileBacked)
        return;
    {
        LOCK(cs_wallet);
        if (mapWallet.erase(hash))
            CWalletDB(strWalletFile).EraseTx(hash);
    }
    return;
}


isminetype CWallet::IsMine(const CTxIn &txin) const
{
    {
        LOCK(cs_wallet);
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size())
                return IsMine(prev.vout[txin.prevout.n]);
        }
    }
    return MINE_NO;
}

int64_t CWallet::GetDebit(const CTxIn &txin) const
{
    {
        LOCK(cs_wallet);
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size())
                if (IsMine(prev.vout[txin.prevout.n]))
                    return prev.vout[txin.prevout.n].nValue;
        }
    }
    return 0;
}

int CWallet::GetPegCycle() const
{
    if (nLastHashBestChain != hashBestChain) {
        LOCK(cs_main);
        nLastPegCycle = pindexBest->nHeight / Params().PegInterval(pindexBest->nHeight);
        nLastPegSupplyIndex = pindexBest->nPegSupplyIndex;
        nLastPegSupplyNIndex = pindexBest->GetNextIntervalPegSupplyIndex();
        nLastPegSupplyNNIndex = pindexBest->GetNextNextIntervalPegSupplyIndex();
        nLastBlockTime = pindexBest->nTime;
        nLastHashBestChain = hashBestChain;
    }
    return nLastPegCycle;
}

int CWallet::GetPegSupplyIndex() const
{
    if (nLastHashBestChain != hashBestChain) {
        LOCK(cs_main);
        nLastPegCycle = pindexBest->nHeight / Params().PegInterval(pindexBest->nHeight);
        nLastPegSupplyIndex = pindexBest->nPegSupplyIndex;
        nLastPegSupplyNIndex = pindexBest->GetNextIntervalPegSupplyIndex();
        nLastPegSupplyNNIndex = pindexBest->GetNextNextIntervalPegSupplyIndex();
        nLastBlockTime = pindexBest->nTime;
        nLastHashBestChain = hashBestChain;
    }
    return nLastPegSupplyIndex;
}

int CWallet::GetPegSupplyNIndex() const
{
    if (nLastHashBestChain != hashBestChain) {
        LOCK(cs_main);
        nLastPegCycle = pindexBest->nHeight / Params().PegInterval(pindexBest->nHeight);
        nLastPegSupplyIndex = pindexBest->nPegSupplyIndex;
        nLastPegSupplyNIndex = pindexBest->GetNextIntervalPegSupplyIndex();
        nLastPegSupplyNNIndex = pindexBest->GetNextNextIntervalPegSupplyIndex();
        nLastBlockTime = pindexBest->nTime;
        nLastHashBestChain = hashBestChain;
    }
    return nLastPegSupplyNIndex;
}

int CWallet::GetPegSupplyNNIndex() const
{
    if (nLastHashBestChain != hashBestChain) {
        LOCK(cs_main);
        nLastPegCycle = pindexBest->nHeight / Params().PegInterval(pindexBest->nHeight);
        nLastPegSupplyIndex = pindexBest->nPegSupplyIndex;
        nLastPegSupplyNIndex = pindexBest->GetNextIntervalPegSupplyIndex();
        nLastPegSupplyNNIndex = pindexBest->GetNextNextIntervalPegSupplyIndex();
        nLastBlockTime = pindexBest->nTime;
        nLastHashBestChain = hashBestChain;
    }
    return nLastPegSupplyNNIndex;
}

int64_t CWallet::GetFrozen(uint256 txhash, long n, const CTxOut& txout,
                           std::vector<CFrozenCoinInfo>* pFrozenCoins) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error("CWallet::GetReserve() : value out of range");
    if (!IsMine(txout))
        return 0;
    
    {
        LOCK(cs_wallet);
        if (nLastHashBestChain != hashBestChain) {
            LOCK(cs_main);
            nLastPegCycle = pindexBest->nHeight / Params().PegInterval(pindexBest->nHeight);
            nLastPegSupplyIndex = pindexBest->nPegSupplyIndex;
            nLastBlockTime = pindexBest->nTime;
            nLastHashBestChain = hashBestChain;
        }
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txhash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& wtx = (*mi).second;
            if (0<=n && size_t(n) < wtx.vOutFractions.size()) {
                bool fF = wtx.vOutFractions[n].nFlags() & CFractions::NOTARY_F;
                bool fV = wtx.vOutFractions[n].nFlags() & CFractions::NOTARY_V;
                fF &= wtx.vOutFractions[n].nLockTime() >= nLastBlockTime;
                fV &= wtx.vOutFractions[n].nLockTime() >= nLastBlockTime;
                if (fF || fV) {
                    if (pFrozenCoins) {
                        CFrozenCoinInfo fcoin;
                        fcoin.txhash = wtx.GetHash();
                        fcoin.n = n;
                        fcoin.nValue = wtx.vout[n].nValue;
                        fcoin.nFlags = wtx.vOutFractions[n].nFlags();
                        fcoin.nLockTime = wtx.vOutFractions[n].nLockTime();
                        pFrozenCoins->push_back(fcoin);
                    }
                    return wtx.vout[n].nValue;
                }
            }
        }
    }
    
    return 0;
}

int64_t CWallet::GetReserve(uint256 txhash, long n, const CTxOut& txout) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error("CWallet::GetReserve() : value out of range");
    if (!IsMine(txout))
        return 0;
    
    {
        LOCK(cs_wallet);
        if (nLastHashBestChain != hashBestChain) {
            LOCK(cs_main);
            nLastPegCycle = pindexBest->nHeight / Params().PegInterval(pindexBest->nHeight);
            nLastPegSupplyIndex = pindexBest->nPegSupplyIndex;
            nLastBlockTime = pindexBest->nTime;
            nLastHashBestChain = hashBestChain;
        }
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txhash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& wtx = (*mi).second;
            if (0<=n && size_t(n) < wtx.vOutFractions.size()) {
                bool fF = wtx.vOutFractions[n].nFlags() & CFractions::NOTARY_F;
                bool fV = wtx.vOutFractions[n].nFlags() & CFractions::NOTARY_V;
                fF &= wtx.vOutFractions[n].nLockTime() >= nLastBlockTime;
                fV &= wtx.vOutFractions[n].nLockTime() >= nLastBlockTime;
                if (fF || fV)
                    return 0;
                
                int64_t nReserve = 0;
                wtx.vOutFractions[n].Ref().LowPart(nLastPegSupplyIndex, &nReserve);
                return nReserve;
            }
        }
    }
    
    return 0;
}

int64_t CWallet::GetLiquidity(uint256 txhash, long n, const CTxOut& txout) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error("CWallet::GetLiquidity() : value out of range");
    if (!IsMine(txout))
        return 0;
    
    {
        LOCK(cs_wallet);
        if (nLastHashBestChain != hashBestChain) {
            LOCK(cs_main);
            nLastPegCycle = pindexBest->nHeight / Params().PegInterval(pindexBest->nHeight);
            nLastPegSupplyIndex = pindexBest->nPegSupplyIndex;
            nLastBlockTime = pindexBest->nTime;
            nLastHashBestChain = hashBestChain;
        }
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txhash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& wtx = (*mi).second;
            if (0<=n && size_t(n) < wtx.vOutFractions.size()) {
                bool fF = wtx.vOutFractions[n].nFlags() & CFractions::NOTARY_F;
                bool fV = wtx.vOutFractions[n].nFlags() & CFractions::NOTARY_V;
                fF &= wtx.vOutFractions[n].nLockTime() >= nLastBlockTime;
                fV &= wtx.vOutFractions[n].nLockTime() >= nLastBlockTime;
                if (fF || fV)
                    return 0;
                
                int64_t nLiquidity = 0;
                wtx.vOutFractions[n].Ref().HighPart(nLastPegSupplyIndex, &nLiquidity);
                return nLiquidity;
            }
        }
    }
    
    return 0;
}

bool CWallet::IsChange(const CTxOut& txout) const
{
    CTxDestination address;

    // TODO: fix handling of 'change' outputs. The assumption is that any
    // payment to a TX_PUBKEYHASH that is mine but isn't in the address book
    // is change. That assumption is likely to break when we implement multisignature
    // wallets that return change back into a multi-signature-protected address;
    // a better way of identifying which outputs are 'the send' and which are
    // 'the change' will need to be implemented (maybe extend CWalletTx to remember
    // which output, if any, was change).
    if (ExtractDestination(txout.scriptPubKey, address) && ::IsMine(*this, address))
    {
        LOCK(cs_wallet);
        if (!mapAddressBook.count(address))
            return true;
    }
    return false;
}

int64_t CWalletTx::GetTxTime() const
{
    int64_t n = nTimeSmart;
    return n ? n : nTimeReceived;
}

int CWalletTx::GetRequestCount() const
{
    // Returns -1 if it wasn't being tracked
    int nRequests = -1;
    {
        LOCK(pwallet->cs_wallet);
        if (IsCoinBase() || IsCoinStake())
        {
            // Generated block
            if (hashBlock != 0)
            {
                map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(hashBlock);
                if (mi != pwallet->mapRequestCount.end())
                    nRequests = (*mi).second;
            }
        }
        else
        {
            // Did anyone request this transaction?
            map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(GetHash());
            if (mi != pwallet->mapRequestCount.end())
            {
                nRequests = (*mi).second;

                // How about the block it's in?
                if (nRequests == 0 && hashBlock != 0)
                {
                    map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(hashBlock);
                    if (mi != pwallet->mapRequestCount.end())
                        nRequests = (*mi).second;
                    else
                        nRequests = 1; // If it's in someone else's block it must have got out
                }
            }
        }
    }
    return nRequests;
}

void CWalletTx::GetAmounts(list<pair<CTxDestination, int64_t> >& listReceived,
                           list<pair<CTxDestination, int64_t> >& listSent, int64_t& nFee, string& strSentAccount) const
{
    nFee = 0;
    listReceived.clear();
    listSent.clear();
    strSentAccount = strFromAccount;

    // Compute fee:
    int64_t nDebit = GetDebit();
    if (nDebit > 0) // debit>0 means we signed/sent this transaction
    {
        int64_t nValueOut = GetValueOut();
        nFee = nDebit - nValueOut;
    }

    // Sent/received.
    for(const CTxOut& txout : vout)
    {
        // Skip special stake out
        if (txout.scriptPubKey.empty())
            continue;

        bool fIsMine;
        // Only need to handle txouts if AT LEAST one of these is true:
        //   1) they debit from us (sent)
        //   2) the output is to us (received)
        if (nDebit > 0)
        {
            // Don't report 'change' txouts
            if (pwallet->IsChange(txout))
                continue;
            fIsMine = pwallet->IsMine(txout);
        }
        else if (!(fIsMine = pwallet->IsMine(txout)))
            continue;

        // In either case, we need to get the destination address
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
        {
            LogPrintf("CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n",
                     this->GetHash().ToString());
            address = CNoDestination();
        }

        // If we are debited by the transaction, add the output as a "sent" entry
        if (nDebit > 0)
            listSent.push_back(make_pair(address, txout.nValue));

        // If we are receiving the output, add it as a "received" entry
        if (fIsMine)
            listReceived.push_back(make_pair(address, txout.nValue));
    }

}

void CWalletTx::GetAccountAmounts(const string& strAccount, int64_t& nReceived,
                                  int64_t& nSent, int64_t& nFee) const
{
    nReceived = nSent = nFee = 0;

    int64_t allFee;
    string strSentAccount;
    list<pair<CTxDestination, int64_t> > listReceived;
    list<pair<CTxDestination, int64_t> > listSent;
    GetAmounts(listReceived, listSent, allFee, strSentAccount);

    if (strAccount == strSentAccount)
    {
        for(const std::pair<CTxDestination,int64_t> & s : listSent) {
            nSent += s.second;
        }
        nFee = allFee;
    }
    {
        LOCK(pwallet->cs_wallet);
        for(const std::pair<CTxDestination,int64_t> & r : listReceived)
        {
            if (pwallet->mapAddressBook.count(r.first))
            {
                map<CTxDestination, string>::const_iterator mi = pwallet->mapAddressBook.find(r.first);
                if (mi != pwallet->mapAddressBook.end() && (*mi).second == strAccount)
                    nReceived += r.second;
            }
            else if (strAccount.empty())
            {
                nReceived += r.second;
            }
        }
    }
}

void CWalletTx::AddSupportingTransactions(CTxDB& txdb)
{
    vtxPrev.clear();

    const int COPY_DEPTH = 3;
    if (SetMerkleBranch() < COPY_DEPTH)
    {
        vector<uint256> vWorkQueue;
        for(const CTxIn& txin : vin) {
            vWorkQueue.push_back(txin.prevout.hash);
        }

        // This critsect is OK because txdb is already open
        {
            LOCK(pwallet->cs_wallet);
            map<uint256, const CMerkleTx*> mapWalletPrev;
            set<uint256> setAlreadyDone;
            for (unsigned int i = 0; i < vWorkQueue.size(); i++)
            {
                uint256 hash = vWorkQueue[i];
                if (setAlreadyDone.count(hash))
                    continue;
                setAlreadyDone.insert(hash);

                CMerkleTx tx;
                map<uint256, CWalletTx>::const_iterator mi = pwallet->mapWallet.find(hash);
                if (mi != pwallet->mapWallet.end())
                {
                    tx = (*mi).second;
                    for(const CMerkleTx& txWalletPrev : (*mi).second.vtxPrev) {
                        mapWalletPrev[txWalletPrev.GetHash()] = &txWalletPrev;
                    }
                }
                else if (mapWalletPrev.count(hash))
                {
                    tx = *mapWalletPrev[hash];
                }
                else if (txdb.ReadDiskTx(hash, tx))
                {
                    ;
                }
                else
                {
                    LogPrintf("ERROR: AddSupportingTransactions() : unsupported transaction\n");
                    continue;
                }

                int nDepth = tx.SetMerkleBranch();
                vtxPrev.push_back(tx);

                if (nDepth < COPY_DEPTH)
                {
                    for(const CTxIn& txin : tx.vin) {
                        vWorkQueue.push_back(txin.prevout.hash);
                    }
                }
            }
        }
    }

    reverse(vtxPrev.begin(), vtxPrev.end());
}

bool CWalletTx::WriteToDisk()
{
    return CWalletDB(pwallet->strWalletFile).WriteTx(GetHash(), *this);
}

// Scan the block chain (starting in pindexStart) for transactions
// from or to us. If fUpdate is true, found transactions that already
// exist in the wallet will be updated.
int CWallet::ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate)
{
    int ret = 0;

    CBlockIndex* pindex = pindexStart;
    {
        LOCK2(cs_main, cs_wallet);
        CPegDB pegdb("r");
        while (pindex)
        {
            // no need to read and scan block, if block was created before
            // our wallet birthday (as adjusted for block time variability)
            if (nTimeFirstKey && (pindex->nTime < (nTimeFirstKey - 7200))) {
                pindex = pindex->pnext;
                continue;
            }

            CBlock block;
            MapFractions mapOutputFractions;
            block.ReadFromDisk(pindex, true);
            for(const CTransaction& tx : block.vtx)
            {
                auto hash = tx.GetHash();
                for(size_t i =0; i< tx.vout.size(); i++) {
                    auto fkey = uint320(hash, i);
                    CFractions& fractions = mapOutputFractions[fkey];
                    fractions = CFractions(tx.vout[i].nValue, CFractions::STD);
                    pegdb.ReadFractions(fkey, fractions);
                    // if pruned, it is ok to use nValue dafault as it is spent
                }
                if (AddToWalletIfInvolvingMe(tx, &block, fUpdate, mapOutputFractions))
                    ret++;
            }
            pindex = pindex->pnext;
        }
    }
    return ret;
}

void CWallet::ReacceptWalletTransactions()
{
    CTxDB txdb("r");
    bool fRepeat = true;
    while (fRepeat)
    {
        LOCK2(cs_main, cs_wallet);
        fRepeat = false;
        vector<CDiskTxPos> vMissingTx;
        for(std::pair<const uint256, CWalletTx> & item : mapWallet)
        {
            CWalletTx& wtx = item.second;
            if ((wtx.IsCoinBase() && wtx.IsSpent(0)) || (wtx.IsCoinStake() && wtx.IsSpent(1)))
                continue;

            CTxIndex txindex;
            bool fUpdated = false;
            if (txdb.ReadTxIndex(wtx.GetHash(), txindex))
            {
                // Update fSpent if a tx got spent somewhere else by a copy of wallet.dat
                if (txindex.vSpent.size() != wtx.vout.size())
                {
                    LogPrintf("ERROR: ReacceptWalletTransactions() : txindex.vSpent.size() %u != wtx.vout.size() %u\n", txindex.vSpent.size(), wtx.vout.size());
                    continue;
                }
                for (unsigned int i = 0; i < txindex.vSpent.size(); i++)
                {
                    if (wtx.IsSpent(i))
                        continue;
                    if (!txindex.vSpent[i].IsNull() && IsMine(wtx.vout[i]))
                    {
                        wtx.MarkSpent(i);
                        fUpdated = true;
                        vMissingTx.push_back(txindex.vSpent[i]);
                    }
                }
                if (fUpdated)
                {
                    LogPrintf("ReacceptWalletTransactions found spent coin %s BAY %s\n", FormatMoney(wtx.GetCredit()), wtx.GetHash().ToString());
                    wtx.MarkDirty();
                    wtx.WriteToDisk();
                }
            }
            else
            {
                // Re-accept any txes of ours that aren't already in a block
                if (!(wtx.IsCoinBase() || wtx.IsCoinStake()))
                    wtx.AcceptWalletTransaction(txdb);
            }
        }
        if (!vMissingTx.empty())
        {
            // TODO: optimize this to scan just part of the block chain?
            if (ScanForWalletTransactions(pindexGenesisBlock))
                fRepeat = true;  // Found missing transactions: re-do re-accept.
        }
    }
}

void CWalletTx::RelayWalletTransaction(CTxDB& txdb)
{
    for(const CMerkleTx& tx : vtxPrev)
    {
        if (!(tx.IsCoinBase() || tx.IsCoinStake()))
        {
            uint256 hash = tx.GetHash();
            if (!txdb.ContainsTx(hash))
                RelayTransaction((CTransaction)tx, hash);
        }
    }
    if (!(IsCoinBase() || IsCoinStake()))
    {
        uint256 hash = GetHash();
        if (!txdb.ContainsTx(hash))
        {
            LogPrintf("Relaying wtx %s\n", hash.ToString());
            RelayTransaction((CTransaction)*this, hash);
        }
    }
}

void CWalletTx::RelayWalletTransaction()
{
   CTxDB txdb("r");
   RelayWalletTransaction(txdb);
}

void CWallet::ResendWalletTransactions(bool fForce)
{
    if (!fForce)
    {
        // Do this infrequently and randomly to avoid giving away
        // that these are our transactions.
        static int64_t nNextTime = 0;
        if (GetTime() < nNextTime)
            return;
        bool fFirst = (nNextTime == 0);
        nNextTime = GetTime() + GetRand(30 * 60);
        if (fFirst)
            return;

        // Only do it if there's been a new block since last time
        static int64_t nLastTime = 0;
        if (nTimeBestReceived < nLastTime)
            return;
        nLastTime = GetTime();
    }

    {
        LOCK(cs_main);
        if (!pindexBest) {
            // blockchain is out of sync yet
            return;
        }

        int64_t nBestTime = pindexBest->GetBlockTime();
        int64_t nLastTime = GetTime();
        if ((nLastTime - nBestTime) > 90*60) {
            // blockchain is out of sync yet
            return;
        }
    }
    
    // Rebroadcast any of our txes that aren't in a block yet
    LogPrintf("ResendWalletTransactions()\n");
    CTxDB txdb("r");
    {
        LOCK(cs_wallet);
        // Sort them in chronological order
        multimap<unsigned int, CWalletTx*> mapSorted;
        for(pair<const uint256, CWalletTx>& item : mapWallet)
        {
            CWalletTx& wtx = item.second;
            if (wtx.IsCoinBase()) continue;
            if (wtx.IsCoinStake()) continue;
            // Don't rebroadcast until it's had plenty of time that
            // it should have gotten in already by now.
            if (fForce || nTimeBestReceived - (int64_t)wtx.nTimeReceived > 5 * 60)
                mapSorted.insert(make_pair(wtx.nTimeReceived, &wtx));
        }
        for(const pair<const unsigned int, CWalletTx*>& item : mapSorted)
        {
            CWalletTx& wtx = *item.second;
            if (!wtx.CheckTransaction()) {
                LogPrintf("ResendWalletTransactions() : CheckTransaction failed for transaction %s\n", wtx.GetHash().ToString());
                continue;
            }
            
            MapPrevTx mapInputs;
            MapFractions mapInputsFractions;
            map<uint256, CTxIndex> mapUnused;
            MapFractions mapOutputsFractions;
            CFractions feesFractions;
            
            {
                CTxDB txdb("r");
                CPegDB pegdb("r");
                
                uint256 hash = wtx.GetHash();
                if (txdb.ContainsTx(hash)) {
                    continue;
                }
        
                bool fInvalid = false;
                if (!wtx.FetchInputs(txdb, pegdb, 
                                     mapUnused, mapOutputsFractions, 
                                     false /*block*/, false /*miner*/, 
                                     mapInputs, mapInputsFractions, 
                                     fInvalid))
                {
                    LogPrintf("ResendWalletTransactions() : FetchInputs failed for transaction %s\n", wtx.GetHash().ToString());
                    continue;
                }
            
                string sPegFailCause;
                bool peg_ok = CalculateStandardFractions(wtx, 
                                                         pindexBest->nPegSupplyIndex,
                                                         pindexBest->nTime,
                                                         mapInputs, mapInputsFractions,
                                                         mapOutputsFractions,
                                                         feesFractions,
                                                         sPegFailCause);
                if (!peg_ok) {
                    LogPrintf("ResendWalletTransactions() : CalculateStandardFractions failed for transaction %s, cause: %s\n", 
                              wtx.GetHash().ToString(),
                              sPegFailCause);
                    continue;
                }
            }
            
            
            wtx.RelayWalletTransaction(txdb);
        }
    }
}






//////////////////////////////////////////////////////////////////////////////
//
// Actions
//


int64_t CWallet::GetBalance() const
{
    int64_t nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAvailableCredit();
        }
    }

    return nTotal;
}

int64_t CWallet::GetReserve() const
{
    int64_t nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAvailableReserve();
        }
    }

    return nTotal;
}

int64_t CWallet::GetLiquidity() const
{
    int64_t nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAvailableLiquidity();
        }
    }

    return nTotal;
}

int64_t CWallet::GetFrozen(vector<CFrozenCoinInfo> * pFrozenCoins) const
{
    int64_t nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (pcoin->IsTrusted()) {
                nTotal += pcoin->GetAvailableFrozen(true, pFrozenCoins);
            }
        }
    }

    return nTotal;
}

bool CWallet::GetRewardInfo(std::vector<RewardInfo> & rewardsInfo) const
{
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (pcoin->IsTrusted()) {
                pcoin->GetRewardInfo(rewardsInfo);
            }
        }
    }

    return true;
}

int64_t CWallet::GetUnconfirmedBalance() const
{
    int64_t nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (!IsFinalTx(*pcoin) || (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0))
                nTotal += pcoin->GetAvailableCredit();
        }
    }
    return nTotal;
}

int64_t CWallet::GetImmatureBalance() const
{
    int64_t nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx& pcoin = (*it).second;
            if (pcoin.IsCoinBase() && pcoin.GetBlocksToMaturity() > 0 && pcoin.IsInMainChain())
                nTotal += GetCredit(pcoin);
        }
    }
    return nTotal;
}

// populate vCoins with vector of available COutputs.
void CWallet::AvailableCoins(vector<COutput>& vCoins, 
                             bool fOnlyConfirmed, 
                             bool fUseFrozenUnlocked, 
                             const CCoinControl *coinControl) const
{
    vCoins.clear();

    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;

            if (!IsFinalTx(*pcoin))
                continue;

            if (fOnlyConfirmed && !pcoin->IsTrusted())
                continue;

            if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0)
                continue;

            if(pcoin->IsCoinStake() && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < 0)
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
                isminetype mine = IsMine(pcoin->vout[i]);
                if (!pcoin->IsSpent(i) 
                        && mine != MINE_NO 
                        && pcoin->vout[i].nValue >= nMinimumInputValue 
                        && (
                            !coinControl 
                            || !coinControl->HasSelected() 
                            || coinControl->IsSelected((*it).first, i))
                        ) {
                    COutput cout(pcoin, i, nDepth, mine & MINE_SPENDABLE);
                    if (cout.IsFrozen(nLastBlockTime)) continue;
                    if (cout.IsFrozenMark() && !fUseFrozenUnlocked) continue;
                    if (cout.IsColdMark()) continue;
                    vCoins.push_back(cout);
                }
            }
        }
    }
}

void CWallet::FrozenCoins(vector<COutput>& vCoins,
                          bool fOnlyConfirmed, 
                          bool fClearArray,
                          const CCoinControl *coinControl) const
{
    if (fClearArray) {
        vCoins.clear();
    }

    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;

            if (!IsFinalTx(*pcoin))
                continue;

            if (fOnlyConfirmed && !pcoin->IsTrusted())
                continue;

            if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0)
                continue;

            if(pcoin->IsCoinStake() && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < 0)
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
                isminetype mine = IsMine(pcoin->vout[i]);
                if (!pcoin->IsSpent(i) 
                        && mine != MINE_NO 
                        && pcoin->vout[i].nValue >= nMinimumInputValue 
                        && (
                            !coinControl 
                            || !coinControl->HasSelected() 
                            || coinControl->IsSelected((*it).first, i))
                        ) {
                    COutput cout(pcoin, i, nDepth, mine & MINE_SPENDABLE);
                    if (!cout.IsFrozen(nLastBlockTime)) continue;
                    vCoins.push_back(cout);
                }
            }
        }
    }
}

void CWallet::AvailableCoinsForStaking(vector<COutput>& vCoins, unsigned int nSpendTime) const
{
    vCoins.clear();

    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < 1)
                continue;

            if (IsProtocolV3(nSpendTime))
            {
                if (nDepth < nStakeMinConfirmations)
                    continue;
            }
            else
            {
                // Filtering by tx timestamp instead of block timestamp may give false positives but never false negatives
                if (pcoin->nTime + nStakeMinAge > nSpendTime)
                    continue;
            }

            if (pcoin->GetBlocksToMaturity() > 0)
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
                isminetype mine = IsMine(pcoin->vout[i]);
                if (!(pcoin->IsSpent(i)) && mine != MINE_NO && pcoin->vout[i].nValue >= nMinimumInputValue)
                    vCoins.push_back(COutput(pcoin, i, nDepth, mine & MINE_SPENDABLE));
            }
        }
    }
}

static void ApproximateBestSubset(vector<pair<int64_t, CSelectedCoin> >vValue, int64_t nTotalLower, int64_t nTargetValue,
                                  vector<char>& vfBest, int64_t& nBest, int iterations = 1000)
{
    vector<char> vfIncluded;

    vfBest.assign(vValue.size(), true);
    nBest = nTotalLower;

    seed_insecure_rand();

    for (int nRep = 0; nRep < iterations && nBest != nTargetValue; nRep++)
    {
        vfIncluded.assign(vValue.size(), false);
        int64_t nTotal = 0;
        bool fReachedTarget = false;
        for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++)
        {
            for (unsigned int i = 0; i < vValue.size(); i++)
            {
                //The solver here uses a randomized algorithm,
                //the randomness serves no real security purpose but is just
                //needed to prevent degenerate behavior and it is important
                //that the rng fast. We do not use a constant random sequence,
                //because there may be some privacy improvement by making
                //the selection random.
                if (nPass == 0 ? insecure_rand()&1 : !vfIncluded[i])
                {
                    nTotal += vValue[i].first;
                    vfIncluded[i] = true;
                    if (nTotal >= nTargetValue)
                    {
                        fReachedTarget = true;
                        if (nTotal < nBest)
                        {
                            nBest = nTotal;
                            vfBest = vfIncluded;
                        }
                        nTotal -= vValue[i].first;
                        vfIncluded[i] = false;
                    }
                }
            }
        }
    }
}

// ppcoin: total coins staked (non-spendable until maturity)
int64_t CWallet::GetStake() const
{
    int64_t nTotal = 0;
    LOCK2(cs_main, cs_wallet);
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
    {
        const CWalletTx* pcoin = &(*it).second;
        if (pcoin->IsCoinStake() && pcoin->GetBlocksToMaturity() > 0 && pcoin->GetDepthInMainChain() > 0)
            nTotal += CWallet::GetCredit(*pcoin);
    }
    return nTotal;
}

int64_t CWallet::GetNewMint() const
{
    int64_t nTotal = 0;
    LOCK2(cs_main, cs_wallet);
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
    {
        const CWalletTx* pcoin = &(*it).second;
        if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0 && pcoin->GetDepthInMainChain() > 0)
            nTotal += CWallet::GetCredit(*pcoin);
    }
    return nTotal;
}

struct LargerOrEqualThanThreshold
{
    int64_t threshold;
    LargerOrEqualThanThreshold(int64_t threshold) : threshold(threshold) {}
    bool operator()(pair<pair<int64_t,int64_t>,CSelectedCoin> const &v) const { return v.first.first >= threshold; }
    bool operator()(pair<pair<int64_t,int64_t>,pair<const CWalletTx*,unsigned int> > const &v) const { return v.first.first >= threshold; }
};

bool CWallet::SelectCoinsMinConf(PegTxType txType,
                                 int64_t nTargetValue, 
                                 unsigned int nSpendTime, 
                                 int nConfMine, 
                                 int nConfTheirs, 
                                 vector<COutput> vCoins, 
                                 set<CSelectedCoin>& setCoinsRet, 
                                 int64_t& nValueRet) const
{
    setCoinsRet.clear();
    nValueRet = 0;

    // List of values less than target
    pair<int64_t, CSelectedCoin> coinLowestLarger;
    coinLowestLarger.first = std::numeric_limits<int64_t>::max();
    coinLowestLarger.second.tx = NULL;
    vector<pair<int64_t, CSelectedCoin> > vValue;
    int64_t nTotalLower = 0;

    random_shuffle(vCoins.begin(), vCoins.end(), GetRandInt);

    for(const COutput &output : vCoins)
    {
        if (!output.fSpendable)
            continue;

        const CWalletTx *pcoin = output.tx;

        if (output.nDepth < (pcoin->IsFromMe() ? nConfMine : nConfTheirs))
            continue;

        unsigned int i = output.i;

        // Follow the timestamp rules
        if (pcoin->nTime > nSpendTime)
            continue;
        
        // take liquidity or reserves
        int64_t nValue = 0;
        if (txType == PEG_MAKETX_SEND_RESERVE ||
            txType == PEG_MAKETX_FREEZE_RESERVE) {
            nValue = pcoin->vOutFractions[i].Ref().Low(GetPegSupplyIndex());
        } else if (txType == PEG_MAKETX_SEND_LIQUIDITY ||
                   txType == PEG_MAKETX_FREEZE_LIQUIDITY) {
            nValue = pcoin->vOutFractions[i].Ref().High(GetPegSupplyIndex());
        }
        if (nValue == 0) continue;
        
        CSelectedCoin selectedCoin = {pcoin, i, nValue};
        pair<int64_t,CSelectedCoin> coin = make_pair(nValue,selectedCoin);

        if (nValue == nTargetValue)
        {
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
            return true;
        }
        else if (nValue < nTargetValue + CENT)
        {
            vValue.push_back(coin);
            nTotalLower += nValue;
        }
        else if (nValue < coinLowestLarger.first)
        {
            coinLowestLarger = coin;
        }
    }

    if (nTotalLower == nTargetValue)
    {
        for (unsigned int i = 0; i < vValue.size(); ++i)
        {
            setCoinsRet.insert(vValue[i].second);
            nValueRet += vValue[i].first;
        }
        return true;
    }

    if (nTotalLower < nTargetValue)
    {
        if (coinLowestLarger.second.tx == NULL)
            return false;
        CSelectedCoin selectedCoin = coinLowestLarger.second;
        setCoinsRet.insert(selectedCoin);
        nValueRet += coinLowestLarger.first;
        return true;
    }

    // Solve subset sum by stochastic approximation
    sort(vValue.rbegin(), vValue.rend(), CompareValueOnly());
    vector<char> vfBest;
    int64_t nBest;

    ApproximateBestSubset(vValue, nTotalLower, nTargetValue, vfBest, nBest, 1000);
    if (nBest != nTargetValue && nTotalLower >= nTargetValue + CENT)
        ApproximateBestSubset(vValue, nTotalLower, nTargetValue + CENT, vfBest, nBest, 1000);

    // If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
    //                                   or the next bigger coin is closer), return the bigger coin
    if (coinLowestLarger.second.tx &&
        ((nBest != nTargetValue && nBest < nTargetValue + CENT) || coinLowestLarger.first <= nBest))
    {
        CSelectedCoin selectedCoin = coinLowestLarger.second;
        setCoinsRet.insert(selectedCoin);
        nValueRet += coinLowestLarger.first;
    }
    else {
        for (unsigned int i = 0; i < vValue.size(); i++) {
            if (vfBest[i])
            {
                CSelectedCoin selectedCoin = vValue[i].second;
                setCoinsRet.insert(selectedCoin);
                nValueRet += vValue[i].first;
            }
        }

        LogPrint("selectcoins", "SelectCoins() best subset: ");
        for (unsigned int i = 0; i < vValue.size(); i++)
            if (vfBest[i])
                LogPrint("selectcoins", "%s ", FormatMoney(vValue[i].first));
        LogPrint("selectcoins", "total %s\n", FormatMoney(nBest));
    }

    return true;
}

bool COutput::IsFrozen(unsigned int nLastBlockTime) const
{
    bool fF = tx->vOutFractions[i].nFlags() & CFractions::NOTARY_F;
    bool fV = tx->vOutFractions[i].nFlags() & CFractions::NOTARY_V;
    bool fC = tx->vOutFractions[i].nFlags() & CFractions::NOTARY_C;
    fF &= tx->vOutFractions[i].nLockTime() >= nLastBlockTime;
    fV &= tx->vOutFractions[i].nLockTime() >= nLastBlockTime;
    return fF || fV || fC;
}

bool COutput::IsFrozenMark() const
{
    bool fF = tx->vOutFractions[i].nFlags() & CFractions::NOTARY_F;
    bool fV = tx->vOutFractions[i].nFlags() & CFractions::NOTARY_V;
    return fF || fV;
}

bool COutput::IsColdMark() const
{
    return tx->vOutFractions[i].nFlags() & CFractions::NOTARY_C;
}

uint64_t COutput::FrozenUnlockTime() const
{
    return tx->vOutFractions[i].nLockTime();
}

bool CWallet::SelectCoins(PegTxType txType,
                          int64_t nTargetValue, 
                          unsigned int nSpendTime, 
                          set<CSelectedCoin>& setCoinsRet, 
                          int64_t& nValueRet, 
                          bool fUseFrozenUnlocked, 
                          const CCoinControl* coinControl) const
{
    vector<COutput> vCoins;
    AvailableCoins(vCoins, true, fUseFrozenUnlocked, coinControl);

    // coin control -> return all selected outputs (we want all selected to go into the transaction for sure)
    if (coinControl && coinControl->HasSelected())
    {
        int64_t nValueLeft = nTargetValue;
        for(const COutput& out : vCoins)
        {
            if(!out.fSpendable)
                continue;
            
            // skip all frozen outputs
            if (out.IsFrozen(nLastBlockTime))
                continue;

            // take liquidity or reserve part
            int64_t nValue = 0;
            if (txType == PEG_MAKETX_SEND_RESERVE ||
                txType == PEG_MAKETX_FREEZE_RESERVE) {
                nValue = out.tx->vOutFractions[out.i].Ref().Low(GetPegSupplyIndex());
            } else if (txType == PEG_MAKETX_SEND_LIQUIDITY ||
                       txType == PEG_MAKETX_FREEZE_LIQUIDITY) {
                nValue = out.tx->vOutFractions[out.i].Ref().High(GetPegSupplyIndex());
            }
            nValueRet += nValue;
            
            int64_t nValueTake = nValue;
            if (nValueTake > nValueLeft) {
                nValueTake = nValueLeft;
            }
            nValueLeft -= nValueTake;
            unsigned int i = out.i;
            CSelectedCoin selectedCoin = {out.tx, i, nValueTake};
            setCoinsRet.insert(selectedCoin);
        }
        return (nValueRet >= nTargetValue);
    }

    boost::function<bool (const CWallet*, PegTxType, int64_t, unsigned int, int, int, std::vector<COutput>, std::set<CSelectedCoin>&, int64_t&)> f = &CWallet::SelectCoinsMinConf;

    return (f(this, txType, nTargetValue, nSpendTime, 1, 10, vCoins, setCoinsRet, nValueRet) ||
            f(this, txType, nTargetValue, nSpendTime, 1, 1, vCoins, setCoinsRet, nValueRet) ||
            f(this, txType, nTargetValue, nSpendTime, 0, 1, vCoins, setCoinsRet, nValueRet));
}

// Select some coins without random shuffle or best subset approximation
bool CWallet::SelectCoinsForStaking(int64_t nTargetValue, 
                                    unsigned int nSpendTime, 
                                    set<pair<const CWalletTx*,unsigned int> >& setCoinsRet, 
                                    int64_t& nValueRet) const
{
    vector<COutput> vCoins;
    AvailableCoinsForStaking(vCoins, nSpendTime);

    setCoinsRet.clear();
    nValueRet = 0;

    for(COutput output : vCoins)
    {
        if (!output.fSpendable)
            continue;

        const CWalletTx *pcoin = output.tx;
        int i = output.i;

        // Stop if we've chosen enough inputs
        if (nValueRet >= nTargetValue)
            break;

        int64_t n = pcoin->vout[i].nValue;

        pair<int64_t,pair<const CWalletTx*,unsigned int> > coin = make_pair(n,make_pair(pcoin, i));

        if (n >= nTargetValue)
        {
            // If input value is greater or equal to target then simply insert
            //    it into the current subset and exit
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
            break;
        }
        else if (n < nTargetValue + CENT)
        {
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
        }
    }

    return true;
}

static bool sortByAddress(const CSelectedCoin &lhs, const CSelectedCoin &rhs) { 
    CScript lhs_script = lhs.tx->vout[lhs.i].scriptPubKey;
    CScript rhs_script = rhs.tx->vout[rhs.i].scriptPubKey;
    
    CTxDestination lhs_dst;
    CTxDestination rhs_dst;
    bool lhs_ok1 = ExtractDestination(lhs_script, lhs_dst);
    bool rhs_ok1 = ExtractDestination(rhs_script, rhs_dst);
    
    if (!lhs_ok1 || !rhs_ok1) {
        if (lhs_ok1 == rhs_ok1) 
            return lhs_script < rhs_script;
        return lhs_ok1 < rhs_ok1;
    }
    
    string lhs_addr = CBitcoinAddress(lhs_dst).ToString();
    string rhs_addr = CBitcoinAddress(rhs_dst).ToString();
    
    return lhs_addr < rhs_addr;
}
static bool sortByDestination(const CTxDestination &lhs, const CTxDestination &rhs) { 
    string lhs_addr = CBitcoinAddress(lhs).ToString();
    string rhs_addr = CBitcoinAddress(rhs).ToString();
    return lhs_addr < rhs_addr;
}

bool CWallet::CreateTransaction(PegTxType txType, 
                                const vector<pair<CScript, int64_t> >& vecSend, 
                                CWalletTx& wtxNew, 
                                CReserveKey& reservekey, 
                                int64_t& nFeeRet, 
                                const CCoinControl* coinControl,
                                bool fTest,
                                std::string & sFailCause)
{
    int64_t nValue = 0;
    
    if (vecSend.empty()) {
        sFailCause = "CT-1, no recepients";
        return false;
    }
    for(const std::pair<CScript, int64_t> & s : vecSend)
    {
        if (nValue < 0) {
            sFailCause = "CT-2, negative value to send";
            return false;
        }
        nValue += s.second;
    }
    if (nValue < 0) {
        sFailCause = "CT-3, negative value to send";
        return false;
    }

    wtxNew.BindWallet(this);

    // Discourage fee sniping.
    //
    // However because of a off-by-one-error in previous versions we need to
    // neuter it by setting nLockTime to at least one less than nBestHeight.
    // Secondly currently propagation of transactions created for block heights
    // corresponding to blocks that were just mined may be iffy - transactions
    // aren't re-accepted into the mempool - we additionally neuter the code by
    // going ten blocks back. Doesn't yet do anything for sniping, but does act
    // to shake out wallet bugs like not showing nLockTime'd transactions at
    // all.
    wtxNew.nLockTime = std::max(0, nBestHeight - 10);

    // Secondly occasionally randomly pick a nLockTime even further back, so
    // that transactions that are delayed after signing for whatever reason,
    // e.g. high-latency mix networks and some CoinJoin implementations, have
    // better privacy.
    if (GetRandInt(10) == 0)
        wtxNew.nLockTime = std::max(0, (int)wtxNew.nLockTime - GetRandInt(100));

    assert(wtxNew.nLockTime <= (unsigned int)nBestHeight);
    assert(wtxNew.nLockTime < LOCKTIME_THRESHOLD);

    {
        LOCK2(cs_main, cs_wallet);
        // txdb must be opened before the mapWallet lock
        CTxDB txdb("r");
        {
            nFeeRet = nTransactionFee;
            size_t nNumInputs = 1;
            while (true)
            {
                wtxNew.vin.clear();
                wtxNew.vout.clear();
                wtxNew.fFromMe = true;

                int64_t nTotalValue = nValue + nFeeRet;
                double dPriority = 0;
                
                if (txType == PEG_MAKETX_SEND_RESERVE ||
                    txType == PEG_MAKETX_FREEZE_RESERVE) {
                    nTotalValue += PEG_MAKETX_FREEZE_VALUE * nNumInputs;
                }
                else if (txType == PEG_MAKETX_FREEZE_LIQUIDITY) {
                    nTotalValue += PEG_MAKETX_VFREEZE_VALUE * nNumInputs;
                }
                
                // Choose coins to use
                set<CSelectedCoin> setCoins;
                int64_t nValueIn = 0;
                bool fUseFrozenUnlocked = false;
                if (!SelectCoins(txType, nTotalValue, wtxNew.nTime, setCoins, nValueIn, fUseFrozenUnlocked, coinControl)) {
                    nValueIn = 0;
                    setCoins.clear();
                    fUseFrozenUnlocked = true;
                    if (!SelectCoins(txType, nTotalValue, wtxNew.nTime, setCoins, nValueIn, fUseFrozenUnlocked, coinControl)) {
                        sFailCause = "CT-4, failed to select coins";
                        return false;
                    }
                }
                
                nNumInputs = setCoins.size();
                if (!nNumInputs) {
                    sFailCause = "CT-5, no selected coins";
                    return false;
                }
                
                // Inputs to be sorted by address
                vector<CSelectedCoin> vCoins;
                for(const CSelectedCoin& coin : setCoins) {
                    vCoins.push_back(coin);
                }
                sort(vCoins.begin(), vCoins.end(), sortByAddress);
                
                // Collect input addresses
                // Prepare maps for input,available,take
                set<CTxDestination> setInputAddresses;
                vector<CTxDestination> vInputAddresses;
                map<CTxDestination, int64_t> mapAvailableValuesAt;
                map<CTxDestination, int64_t> mapInputValuesAt;
                map<CTxDestination, int64_t> mapTakeValuesAt;
                int64_t nValueToTakeFromChange = 0;
                for(const CSelectedCoin& coin : vCoins) {
                    CTxDestination address;
                    if(!ExtractDestination(coin.tx->vout[coin.i].scriptPubKey, address))
                        continue;
                    setInputAddresses.insert(address); // sorted due to vCoins
                    mapAvailableValuesAt[address] = 0;
                    mapInputValuesAt[address] = 0;
                    mapTakeValuesAt[address] = 0;
                }
                // Get sorted list of input addresses
                for(const CTxDestination& address : setInputAddresses) {
                    vInputAddresses.push_back(address);
                }
                sort(vInputAddresses.begin(), vInputAddresses.end(), sortByDestination);
                // Input and available values can be filled in
                for(const CSelectedCoin& coin : vCoins) {
                    CTxDestination address;
                    if(!ExtractDestination(coin.tx->vout[coin.i].scriptPubKey, address))
                        continue;
                    int64_t& nValueAvailableAt = mapAvailableValuesAt[address];
                    nValueAvailableAt += coin.nAvailableValue;
                    int64_t& nValueInputAt = mapInputValuesAt[address];
                    nValueInputAt += coin.tx->vout[coin.i].nValue;
                }
                
                // Notations for frozen **F**
                if (txType == PEG_MAKETX_SEND_RESERVE ||
                    txType == PEG_MAKETX_FREEZE_RESERVE) {
                    // prepare indexes to freeze
                    size_t nCoins = vCoins.size();
                    size_t nPayees = vecSend.size();
                    string out_indexes;
                    if (nPayees == 1) { // trick to have triple to use sort
                        auto out_index = std::to_string(0+nCoins);
                        out_indexes = out_index+":"+out_index+":"+out_index;
                    }
                    else if (nPayees == 2) { // trick to have triple to use sort
                        auto out_index1 = std::to_string(0+nCoins);
                        auto out_index2 = std::to_string(1+nCoins);
                        out_indexes = out_index1+":"+out_index1+":"+out_index2+":"+out_index2;
                    }
                    else {
                        for(size_t i=0; i<nPayees; i++) {
                            if (!out_indexes.empty())
                                out_indexes += ":";
                            out_indexes += std::to_string(i+nCoins);
                        }
                    }
                    // Fill vout with freezing instructions
                    for(size_t i=0; i<nCoins; i++) {
                        CScript scriptPubKey;
                        scriptPubKey.push_back(OP_RETURN);
                        unsigned char len_bytes = out_indexes.size();
                        scriptPubKey.push_back(len_bytes+5);
                        scriptPubKey.push_back('*');
                        scriptPubKey.push_back('*');
                        scriptPubKey.push_back('F');
                        scriptPubKey.push_back('*');
                        scriptPubKey.push_back('*');
                        for (size_t j=0; j< out_indexes.size(); j++) {
                            scriptPubKey.push_back(out_indexes[j]);
                        }
                        wtxNew.vout.push_back(CTxOut(PEG_MAKETX_FREEZE_VALUE, scriptPubKey));
                    }
                    // Value for notary is first taken from reserves sorted by address
                    int64_t nValueLeft = nCoins*PEG_MAKETX_FREEZE_VALUE;
                    // take reserves in defined order
                    for(const CTxDestination& address : vInputAddresses) {
                        int64_t nValueAvailableAt = mapAvailableValuesAt[address];
                        int64_t& nValueTakeAt = mapTakeValuesAt[address];
                        int64_t nValueLeftAt = nValueAvailableAt-nValueTakeAt;
                        if (nValueAvailableAt ==0) continue;
                        int64_t nValueToTake = nValueLeft;
                        if (nValueToTake > nValueLeftAt)
                            nValueToTake = nValueLeftAt;
                        
                        nValueTakeAt += nValueToTake;
                        nValueLeft -= nValueToTake;
                        
                        if (nValueLeft == 0) break;
                    }
                    // if nValueLeft is left - need to be taken from change (liquidity)
                    nValueToTakeFromChange += nValueLeft;
                }
                
                // Notations for frozen **V**
                if (txType == PEG_MAKETX_FREEZE_LIQUIDITY) {
                    // prepare indexes to freeze
                    size_t nCoins = vCoins.size();
                    size_t nPayees = vecSend.size();
                    string out_indexes;
                    if (nPayees == 1) { // trick to have triple to use sort
                        auto out_index = std::to_string(0+nCoins);
                        out_indexes = out_index+":"+out_index+":"+out_index;
                    }
                    else if (nPayees == 2) { // trick to have triple to use sort
                        auto out_index1 = std::to_string(0+nCoins);
                        auto out_index2 = std::to_string(1+nCoins);
                        out_indexes = out_index1+":"+out_index1+":"+out_index2+":"+out_index2;
                    }
                    else {
                        for(size_t i=0; i<nPayees; i++) {
                            if (!out_indexes.empty())
                                out_indexes += ":";
                            out_indexes += std::to_string(i+nCoins);
                        }
                    }
                    // Fill vout with freezing instructions
                    for(size_t i=0; i<nCoins; i++) {
                        CScript scriptPubKey;
                        scriptPubKey.push_back(OP_RETURN);
                        unsigned char len_bytes = out_indexes.size();
                        scriptPubKey.push_back(len_bytes+5);
                        scriptPubKey.push_back('*');
                        scriptPubKey.push_back('*');
                        scriptPubKey.push_back('V');
                        scriptPubKey.push_back('*');
                        scriptPubKey.push_back('*');
                        for (size_t j=0; j< out_indexes.size(); j++) {
                            scriptPubKey.push_back(out_indexes[j]);
                        }
                        wtxNew.vout.push_back(CTxOut(PEG_MAKETX_VFREEZE_VALUE, scriptPubKey));
                    }
                    // Value for notary is first taken from reserves sorted by address
                    int64_t nValueLeft = nCoins*PEG_MAKETX_VFREEZE_VALUE;
                    // take values in defined order
                    for(const CTxDestination& address : vInputAddresses) {
                        int64_t nValueAvailableAt = mapAvailableValuesAt[address];
                        int64_t& nValueTakeAt = mapTakeValuesAt[address];
                        int64_t nValueLeftAt = nValueAvailableAt-nValueTakeAt;
                        if (nValueAvailableAt ==0) continue;
                        int64_t nValueToTake = nValueLeft;
                        if (nValueToTake > nValueLeftAt)
                            nValueToTake = nValueLeftAt;
                        
                        nValueTakeAt += nValueToTake;
                        nValueLeft -= nValueToTake;
                        
                        if (nValueLeft == 0) break;
                    }
                    // if nValueLeft is left - need to be taken from change (liquidity)
                    nValueToTakeFromChange += nValueLeft;
                }
                
                // vouts to the payees
                for(const std::pair<CScript, int64_t> & s : vecSend) {
                    wtxNew.vout.push_back(CTxOut(s.second, s.first));
                }
                
                for(const CSelectedCoin & pcoin : setCoins)
                {
                    int64_t nCredit = pcoin.tx->vout[pcoin.i].nValue;
                    dPriority += (double)nCredit * pcoin.tx->GetDepthInMainChain();
                }

                reservekey.ReturnKey();

                // Logic is different depend on txType
                if (txType == PEG_MAKETX_SEND_LIQUIDITY) {
                    // Available values - liquidity
                    // Compute values to take from each address (liquidity is common)
                    int64_t nValueLeft = nValue;
                    for(const CSelectedCoin& coin : vCoins) {
                        CTxDestination address;
                        if(!ExtractDestination(coin.tx->vout[coin.i].scriptPubKey, address))
                            continue;
                        int64_t nValueAvailable = coin.nAvailableValue;
                        int64_t nValueTake = nValueAvailable;
                        if (nValueTake > nValueLeft) {
                            nValueTake = nValueLeft;
                        }
                        int64_t& nValueTakeAt = mapTakeValuesAt[address];
                        nValueTakeAt += nValueTake;
                        nValueLeft -= nValueTake;
                    }
                }
                else if (txType == PEG_MAKETX_SEND_RESERVE ||
                         txType == PEG_MAKETX_FREEZE_RESERVE) {
                    // Available values - reserves per address
                    // vecSend - outputs to be frozen reserve parts
                    
                    // Prepare order of inputs
                    // For **F** the first is referenced (last input) then others are sorted
                    vector<CTxDestination> vAddressesForFrozen;
                    CTxDestination addressFrozenRef = vInputAddresses.back();
                    vAddressesForFrozen.push_back(addressFrozenRef);
                    for(const CTxDestination & address : vInputAddresses) {
                        if (address == addressFrozenRef) continue;
                        vAddressesForFrozen.push_back(address);
                    }
                    
                    // Follow outputs and compute taken values
                    for(const pair<CScript, int64_t>& s : vecSend) {
                        int64_t nValueLeft = s.second;
                        // take reserves in defined order
                        for(const CTxDestination& address : vAddressesForFrozen) {
                            int64_t nValueAvailableAt = mapAvailableValuesAt[address];
                            int64_t& nValueTakeAt = mapTakeValuesAt[address];
                            int64_t nValueLeftAt = nValueAvailableAt-nValueTakeAt;
                            if (nValueAvailableAt ==0) continue;
                            int64_t nValueToTake = nValueLeft;
                            if (nValueToTake > nValueLeftAt)
                                nValueToTake = nValueLeftAt;
    
                            nValueTakeAt += nValueToTake;
                            nValueLeft -= nValueToTake;
                            
                            if (nValueLeft == 0) break;
                        }
                        // if nValueLeft is left then is taken from change (liquidity)
                        nValueToTakeFromChange += nValueLeft;
                    }
                }
                else if (txType == PEG_MAKETX_FREEZE_LIQUIDITY) {
                    // Available values - liquidities per address
                    // vecSend - outputs to be frozen liquidity parts
                    
                    // Follow outputs and compute taken values
                    // Logic is same as for sending liquidity
                    int64_t nValueLeft = nValue;
                    for(const CSelectedCoin& coin : vCoins) {
                        CTxDestination address;
                        if(!ExtractDestination(coin.tx->vout[coin.i].scriptPubKey, address))
                            continue;
                        int64_t nValueAvailable = coin.nAvailableValue;
                        int64_t nValueTake = nValueAvailable;
                        if (nValueTake > nValueLeft) {
                            nValueTake = nValueLeft;
                        }
                        int64_t& nValueTakeAt = mapTakeValuesAt[address];
                        nValueTakeAt += nValueTake;
                        nValueLeft -= nValueTake;
                    }
                }
                
                // Calculate change (minus fee and part taken from change)
                int64_t nTakeFromChangeLeft = nValueToTakeFromChange + nFeeRet;
                for (const CTxDestination& address : vInputAddresses) {
                    CScript scriptPubKey;
                    scriptPubKey.SetDestination(address);
                    int64_t nValueTake = mapTakeValuesAt[address];
                    int64_t nValueInput = mapInputValuesAt[address];
                    int64_t nValueChange = nValueInput - nValueTake;
                    if (nValueChange > nTakeFromChangeLeft) {
                        nValueChange -= nTakeFromChangeLeft;
                        nTakeFromChangeLeft = 0;
                    }
                    if (nValueChange < nTakeFromChangeLeft) {
                        nTakeFromChangeLeft -= nValueChange;
                        nValueChange = 0;
                    }
                    if (nValueChange == 0) continue;
                    wtxNew.vout.push_back(CTxOut(nValueChange, scriptPubKey));
                }
                
                // Fill vin
                //
                // Note how the sequence number is set to max()-1 so that the
                // nLockTime set above actually works.
                for(const CSelectedCoin& coin : vCoins) {
                    wtxNew.vin.push_back(CTxIn(coin.tx->GetHash(),coin.i,CScript(),
                                              std::numeric_limits<unsigned int>::max()-1));
                }

                // Sign
                if (!fTest) {
                    int nIn = 0;
                    for(const CSelectedCoin& coin : vCoins) {
                        if (!SignSignature(*this, *coin.tx, wtxNew, nIn++)) {
                            sFailCause = "CT-6, failed to sign";
                            return false;
                        }
                    }
                }

                // Limit size
                unsigned int nBytes = ::GetSerializeSize(*(CTransaction*)&wtxNew, SER_NETWORK, PROTOCOL_VERSION);
                if (nBytes >= MAX_STANDARD_TX_SIZE) {
                    sFailCause = "CT-7, oversize standard tx size";
                    return false;
                }
                dPriority /= nBytes;

                // Check that enough fee is included
                int64_t nPayFee = nTransactionFee * (1 + (int64_t)nBytes / 1000);
                int64_t nMinFee = GetMinFee(wtxNew, pindexBest->nHeight, 1, GMF_SEND, nBytes);

                if (nFeeRet < max(nPayFee, nMinFee))
                {
                    nFeeRet = max(nPayFee, nMinFee);
                    continue;
                }

                // Fill vtxPrev by copying from previous transactions vtxPrev
                wtxNew.AddSupportingTransactions(txdb);
                wtxNew.fTimeReceivedIsTxTime = true;

                break;
            }
        }
        // now everything is ready to calculate output fractions
        if (!fTest) {
            CPegDB pegdb("r");
            
            MapPrevTx mapInputs;
            MapFractions mapInputsFractions;
            map<uint256, CTxIndex> mapUnused;
            MapFractions mapOutputFractions;
            CFractions feesFractions;
            string sPegFailCause;
            bool fInvalid = false;
            if (!wtxNew.FetchInputs(txdb, 
                                    pegdb, 
                                    mapUnused, mapOutputFractions, 
                                    false, false, 
                                    mapInputs, mapInputsFractions, 
                                    fInvalid)) {
                sFailCause = "CT-8, failed to fetch inputs";
                return false;
            }

            bool peg_ok = CalculateStandardFractions(wtxNew, 
                                                     GetPegSupplyIndex(),
                                                     wtxNew.nTime,
                                                     mapInputs, mapInputsFractions,
                                                     mapOutputFractions,
                                                     feesFractions,
                                                     sPegFailCause);
            if (!peg_ok) {
                sFailCause = "CT-9, failed peg: "+sPegFailCause;
                return false;
            }
            
            auto txhash = wtxNew.GetHash();
            wtxNew.vOutFractions.resize(wtxNew.vout.size());
            for(size_t i=0; i < wtxNew.vout.size(); i++) {
                wtxNew.vOutFractions[i].Init(wtxNew.vout[i].nValue);
                auto fkey = uint320(txhash, i);
                if (mapOutputFractions.find(fkey) == mapOutputFractions.end()) {
                    sFailCause = "CT-10, no out fractions";
                    return false;
                } 
                CFractions& fractions = wtxNew.vOutFractions[i].Ref();
                fractions = mapOutputFractions.at(fkey);
            }
        }
    }
    return true;
}

bool CWallet::CreateTransaction(CScript scriptPubKey, int64_t nValue, CWalletTx& wtxNew, CReserveKey& reservekey, int64_t& nFeeRet, const CCoinControl* coinControl)
{
    string sFailCause;
    vector< pair<CScript, int64_t> > vecSend;
    vecSend.push_back(make_pair(scriptPubKey, nValue));
    return CreateTransaction(PEG_MAKETX_SEND_LIQUIDITY, vecSend, wtxNew, reservekey, nFeeRet, coinControl, false /*fTest*/, sFailCause);
}

uint64_t CWallet::GetStakeWeight() const
{
    // Choose coins to use
    int64_t nBalance = GetBalance();

    if (nBalance <= nNoStakeBalance)
        return 0;

    set<pair<const CWalletTx*,unsigned int> > setCoins;
    int64_t nValueIn = 0;

    if (!SelectCoinsForStaking(nBalance - nNoStakeBalance, GetTime(), setCoins, nValueIn))
        return 0;

    if (setCoins.empty())
        return 0;

    uint64_t nWeight = 0;

    int64_t nCurrentTime = GetTime();
    CTxDB txdb("r");

    LOCK2(cs_main, cs_wallet);
    for(const pair<const CWalletTx*, unsigned int> & pcoin : setCoins)
    {
        if (IsProtocolV3(nCurrentTime))
        {
            if (pcoin.first->GetDepthInMainChain() >= nStakeMinConfirmations)
                nWeight += pcoin.first->vout[pcoin.second].nValue;
        }
        else
        {
            CTxIndex txindex;
            if (!txdb.ReadTxIndex(pcoin.first->GetHash(), txindex))
                continue;

            if (nCurrentTime - pcoin.first->nTime > nStakeMinAge)
                nWeight += pcoin.first->vout[pcoin.second].nValue;
        }
    }

    return nWeight;
}

bool CWallet::CreateCoinStake(const CKeyStore& keystore, 
                              unsigned int nBits, 
                              int64_t nSearchInterval, 
                              int64_t nFees, 
                              CTransaction& txCoinStake, 
                              CTransaction& txConsolidate,
                              CKey& key,
                              PegVoteType voteType)
{
    LOCK2(cs_main, cs_wallet);
    CBlockIndex* pindexPrev = pindexBest;
    CBigNum bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);

    txCoinStake.vin.clear();
    txCoinStake.vout.clear();

    // Mark coin stake transaction
    CScript scriptEmpty;
    scriptEmpty.clear();
    txCoinStake.vout.push_back(CTxOut(0, scriptEmpty));

    // Choose coins to use
    int64_t nBalance = GetBalance();

    if (nBalance <= nNoStakeBalance)
        return false;

    vector<const CWalletTx*> vwtxPrev;

    set<pair<const CWalletTx*,unsigned int> > setCoins;
    int64_t nValueIn = 0;

    // Select coins with suitable depth
    if (!SelectCoinsForStaking(nBalance - nNoStakeBalance, txCoinStake.nTime, setCoins, nValueIn))
        return false;

    if (setCoins.empty())
        return false;

    int64_t nCredit = 0;
    CScript scriptPubKeyKernel;
    
    map<string,int> mapCountForConsolidate;
    map<string,vector<pair<const CTransaction*,CTxIn>>> mapCollectForConsolidate;
    
    bool fKernelFound = false;
    for(const pair<const CWalletTx*, unsigned int> & pcoin : setCoins)
    {
        COutPoint prevoutStake = COutPoint(pcoin.first->GetHash(), pcoin.second);
        static int nMaxStakeSearchInterval = 60;
        
        bool fKernelFoundForCoin = false;
        if (!fKernelFound) {
            for (unsigned int n=0; n<min(nSearchInterval,(int64_t)nMaxStakeSearchInterval) && !fKernelFound && pindexPrev == pindexBest; n++)
            {
                boost::this_thread::interruption_point();
                // Search backward in time from the given txNew timestamp
                // Search nSearchInterval seconds back up to nMaxStakeSearchInterval
                int64_t nBlockTime;
                if (CheckKernel(pindexPrev, nBits, txCoinStake.nTime - n, prevoutStake, &nBlockTime))
                {
                    // Found a kernel
                    LogPrint("coinstake", "CreateCoinStake : kernel found\n");
                    vector<valtype> vSolutions;
                    txnouttype whichType;
                    CScript scriptPubKeyOut;
                    scriptPubKeyKernel = pcoin.first->vout[pcoin.second].scriptPubKey;
                    if (!Solver(scriptPubKeyKernel, whichType, vSolutions))
                    {
                        LogPrint("coinstake", "CreateCoinStake : failed to parse kernel\n");
                        break;
                    }
                    LogPrint("coinstake", "CreateCoinStake : parsed kernel type=%d\n", whichType);
                    if (whichType != TX_PUBKEY && whichType != TX_PUBKEYHASH)
                    {
                        LogPrint("coinstake", "CreateCoinStake : no support for kernel type=%d\n", whichType);
                        break;  // only support pay to public key and pay to address
                    }
                    if (whichType == TX_PUBKEYHASH) // pay to address type
                    {
                        // convert to pay to public key type
                        if (!keystore.GetKey(uint160(vSolutions[0]), key))
                        {
                            LogPrint("coinstake", "CreateCoinStake : failed to get key for kernel type=%d\n", whichType);
                            break;  // unable to find corresponding public key
                        }
                        scriptPubKeyOut << key.GetPubKey() << OP_CHECKSIG;
                    }
                    if (whichType == TX_PUBKEY)
                    {
                        valtype& vchPubKey = vSolutions[0];
                        if (!keystore.GetKey(Hash160(vchPubKey), key))
                        {
                            LogPrint("coinstake", "CreateCoinStake : failed to get key for kernel type=%d\n", whichType);
                            break;  // unable to find corresponding public key
                        }
    
                        if (key.GetPubKey() != vchPubKey)
                        {
                            LogPrint("coinstake", "CreateCoinStake : invalid key for kernel type=%d\n", whichType);
                            break; // keys mismatch
                        }
    
                        scriptPubKeyOut = scriptPubKeyKernel;
                    }
    
                    txCoinStake.nTime -= n;
                    txCoinStake.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
                    nCredit += pcoin.first->vout[pcoin.second].nValue;
                    vwtxPrev.push_back(pcoin.first);
                    txCoinStake.vout.push_back(CTxOut(0, scriptPubKeyOut));
    
                    LogPrint("coinstake", "CreateCoinStake : added kernel type=%d\n", whichType);
                    fKernelFoundForCoin = true;
                    fKernelFound = true;
                    break;
                }
            }
        }
        
        if (consolidateEnabled && !fKernelFoundForCoin) {
            if (pcoin.first->vout.size() <= pcoin.second) continue; // fail ref
            if (pcoin.first->vout[pcoin.second].nValue > nConsolidateMaxAmount) continue;
            if (pcoin.first->vOutFractions.size() > pcoin.second) {
                const CFractions & fractions = pcoin.first->vOutFractions[pcoin.second].Ref();
                if (fractions.nFlags & CFractions::NOTARY_F) continue;
                if (fractions.nFlags & CFractions::NOTARY_V) continue;
                if (fractions.nFlags & CFractions::NOTARY_C) continue;
            }
            int nRequired;
            string sAddress;
            txnouttype type;
            vector<CTxDestination> addresses;
            if (ExtractDestinations(pcoin.first->vout[pcoin.second].scriptPubKey, type, addresses, nRequired)) {
                if (addresses.size()==1) {
                    sAddress = CBitcoinAddress(addresses.front()).ToString();
                }
            }
            if (!sAddress.empty()) {
                CTxIn txin(pcoin.first->GetHash(), pcoin.second);
                mapCollectForConsolidate[sAddress].push_back(make_pair(pcoin.first, txin));
                if (mapCountForConsolidate.find(sAddress) == mapCountForConsolidate.end())
                    mapCountForConsolidate[sAddress] = 0;
                mapCountForConsolidate[sAddress]++;
            }
        }

        if (fKernelFound && !consolidateEnabled)
            break; // if kernel is found stop searching
    }

    if (nCredit == 0 || nCredit > nBalance - nNoStakeBalance)
        return false;

    // consolidate tx
    if (consolidateEnabled) {
        int nCount = 0;
        string sAddress;
        for(const pair<string,int>& entry : mapCountForConsolidate) {
            if (entry.first.empty()) continue;
            if (entry.second >= nConsolidateMin && entry.second > nCount) {
                sAddress = entry.first;
                nCount = entry.second;
            }
        }
        
        if (!sAddress.empty()) {
            int64_t nConsolidateFees = 0;
            int nCount = 0;
            int nCountTodo = mapCollectForConsolidate[sAddress].size();
            int64_t nValue = 0;
            vector<const CTransaction*> vConsolidatePrev;
            for(const pair<const CTransaction*,CTxIn>& entry : mapCollectForConsolidate[sAddress]) {
                vConsolidatePrev.push_back(entry.first);
                txConsolidate.vin.push_back(entry.second);
                nValue += entry.first->vout[entry.second.prevout.n].nValue;
                nValue -= PEG_MAKETX_FEE_INP_OUT;
                nConsolidateFees += PEG_MAKETX_FEE_INP_OUT;
                nCount++;
                if (nCount >= nConsolidateMax)
                    break;
                if ((nCountTodo - nCount) < nConsolidateLeast)
                    break; // keep least 5 inputs
            }
            nValue -= PEG_MAKETX_FEE_INP_OUT; // 1out
            nConsolidateFees += PEG_MAKETX_FEE_INP_OUT;
            if (nValue >0) {
                CScript scriptPubKey;
                scriptPubKey.SetDestination(CBitcoinAddress(sAddress).Get());
                txConsolidate.vout.push_back(CTxOut(nValue, scriptPubKey));
                int nIn = 0;
                for(const CTransaction* pcoin : vConsolidatePrev)
                {
                    if (!SignSignature(*this, *pcoin, txConsolidate, nIn++)) {
                        error("CreateCoinStake : failed to sign consolidate");
                        txConsolidate.vin.clear();
                        break;
                    }
                }
                nFees += nConsolidateFees; // all ok, add fees
            }
            else {
                txConsolidate.vin.clear();
            }
        }
    }
    
    // Calculate coin age reward
    {
        uint64_t nCoinAge;
        CTxDB txdb("r");
        if (!txCoinStake.GetCoinAge(txdb, pindexPrev, nCoinAge))
            return error("CreateCoinStake : failed to calculate coin age");

        CPegDB pegdb("r");
        const COutPoint & prevout = txCoinStake.vin.front().prevout;
        auto fkey = uint320(prevout.hash, prevout.n);
        CFractions fractions(vwtxPrev[0]->vout[prevout.n].nValue, CFractions::VALUE);
        if (!pegdb.ReadFractions(fkey, fractions, false /*must_have*/)) {
            return false;
        }
        
        int64_t nReward = GetProofOfStakeReward(pindexPrev, nCoinAge, nFees, fractions);
        if (nReward <= 0)
            return false;

        if (supportEnabled && !supportAddress.empty() && CBitcoinAddress(supportAddress).IsValid()) {
            CScript scriptPubKey;
            scriptPubKey.SetDestination(CBitcoinAddress(supportAddress).Get());
            if (supportPart >0 && supportPart <=100) {
                int64_t nSupport = (nReward * supportPart) / 100;
                txCoinStake.vout.push_back(CTxOut(nSupport, scriptPubKey)); // add support
                nReward -= nSupport;
            }
        }
        
        if (nReward > PEG_MAKETX_VOTE_VALUE &&  !rewardAddress.empty() && CBitcoinAddress(rewardAddress).IsValid()) {
            CScript scriptPubKey;
            scriptPubKey.SetDestination(CBitcoinAddress(rewardAddress).Get());
            txCoinStake.vout.push_back(CTxOut(nReward, scriptPubKey)); // add reward
        }
        else {
            nCredit += nReward;
        }
        
        txCoinStake.vout[1].nValue = nCredit; // return stake
    }
    
    // Add vote output
    lastAutoPegVoteType = PEG_VOTE_NONE;
    if (voteType != PEG_VOTE_NONE) {
        CScript scriptPubKey;
        string address = "";
        if (voteType == PEG_VOTE_INFLATE) {
            address = Params().PegInflateAddr();
        } else if (voteType == PEG_VOTE_DEFLATE) {
            address = Params().PegDeflateAddr();
        } else if (voteType == PEG_VOTE_NOCHANGE) {
            address = Params().PegNochangeAddr();
        } else if (voteType == PEG_VOTE_AUTO) {
            if (trackerVoteType != PEG_VOTE_NONE) {
                if (trackerVoteType == PEG_VOTE_INFLATE) {
                    lastAutoPegVoteType = PEG_VOTE_INFLATE;
                    address = Params().PegInflateAddr();
                } else if (trackerVoteType == PEG_VOTE_DEFLATE) {
                    lastAutoPegVoteType = PEG_VOTE_DEFLATE;
                    address = Params().PegDeflateAddr();
                } else if (trackerVoteType == PEG_VOTE_NOCHANGE) {
                    lastAutoPegVoteType = PEG_VOTE_NOCHANGE;
                    address = Params().PegNochangeAddr();
                }
            }
            else if (dBayPeakPrice >0 && !vBayRates.empty()) {
                double dLastBayPrice = vBayRates.back();
                if ((dLastBayPrice * 1.05) < dBayPeakPrice) {
                    address = Params().PegDeflateAddr();
                    lastAutoPegVoteType = PEG_VOTE_DEFLATE;
                }
                else if ((dLastBayPrice * 0.95) > dBayPeakPrice) {
                    address = Params().PegInflateAddr();
                    lastAutoPegVoteType = PEG_VOTE_INFLATE;
                }
                else {
                    address = Params().PegNochangeAddr();
                    lastAutoPegVoteType = PEG_VOTE_NOCHANGE;
                }
            }
        }
        if (!address.empty()) {
            if (!CBitcoinAddress(address).IsValid()) {
                LogPrint("coinstake", "CreateCoinStake : vote address=%s is not valid\n", address);
            } else {
                txCoinStake.vout.push_back(CTxOut(0, txCoinStake.vout[1].scriptPubKey)); //add vote
                scriptPubKey.SetDestination(CBitcoinAddress(address).Get());
                txCoinStake.vout[txCoinStake.vout.size()-1].scriptPubKey = scriptPubKey;
                txCoinStake.vout[txCoinStake.vout.size()-1].nValue = PEG_MAKETX_VOTE_VALUE;
                txCoinStake.vout[txCoinStake.vout.size()-2].nValue -= PEG_MAKETX_VOTE_VALUE; // from reward
            }
        }
    }

    // Sign
    int nIn = 0;
    for(const CWalletTx* pcoin : vwtxPrev)
    {
        if (!SignSignature(*this, *pcoin, txCoinStake, nIn++))
            return error("CreateCoinStake : failed to sign coinstake");
    }

    // Limit size
    unsigned int nBytes = ::GetSerializeSize(txCoinStake, SER_NETWORK, PROTOCOL_VERSION);
    if (nBytes >= MAX_BLOCK_SIZE_GEN/5)
        return error("CreateCoinStake : exceeded coinstake size limit");
    
    // Successfully generated coinstake
    return true;
}


// Call after CreateTransaction unless you want to abort
bool CWallet::CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey)
{
    {
        LOCK2(cs_main, cs_wallet);
        LogPrintf("CommitTransaction:\n%s", wtxNew.ToString());
        {
            // This is only to keep the database open to defeat the auto-flush for the
            // duration of this scope.  This is the only place where this optimization
            // maybe makes sense; please don't do it anywhere else.
            CWalletDB* pwalletdb = fFileBacked ? new CWalletDB(strWalletFile,"r") : NULL;

            // Take key pair from key pool so it won't be used again
            reservekey.KeepKey();

            // Add tx to wallet, because if it has change it's also ours,
            // otherwise just for transaction history.
            AddToWallet(wtxNew);

            // Mark old coins as spent
            set<CWalletTx*> setCoins;
            for(const CTxIn& txin : wtxNew.vin)
            {
                CWalletTx &coin = mapWallet[txin.prevout.hash];
                coin.BindWallet(this);
                coin.MarkSpent(txin.prevout.n);
                coin.WriteToDisk();
                NotifyTransactionChanged(this, coin.GetHash(), CT_UPDATED);
            }

            if (fFileBacked)
                delete pwalletdb;
        }

        // Track how many getdata requests our transaction gets
        mapRequestCount[wtxNew.GetHash()] = 0;

        // Broadcast
        if (!wtxNew.AcceptToMemoryPool(true))
        {
            // This must not fail. The transaction has already been signed and recorded.
            LogPrintf("CommitTransaction() : Error: Transaction not valid\n");
            return false;
        }
        wtxNew.RelayWalletTransaction();
    }
    return true;
}




string CWallet::SendMoney(CScript scriptPubKey, int64_t nValue, CWalletTx& wtxNew, bool fAskFee)
{
    CReserveKey reservekey(this);
    int64_t nFeeRequired;

    if (IsLocked())
    {
        string strError = _("Error: Wallet locked, unable to create transaction!");
        LogPrintf("SendMoney() : %s", strError);
        return strError;
    }
    if (fWalletUnlockStakingOnly)
    {
        string strError = _("Error: Wallet unlocked for staking only, unable to create transaction.");
        LogPrintf("SendMoney() : %s", strError);
        return strError;
    }
    if (!CreateTransaction(scriptPubKey, nValue, wtxNew, reservekey, nFeeRequired))
    {
        string strError;
        if (nValue + nFeeRequired > GetBalance())
            strError = strprintf(_("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!"), FormatMoney(nFeeRequired));
        else
            strError = _("Error: Transaction creation failed!");
        LogPrintf("SendMoney() : %s\n", strError);
        return strError;
    }

    if (fAskFee && !uiInterface.ThreadSafeAskFee(nFeeRequired, _("Sending...")))
        return "ABORTED";

    if (!CommitTransaction(wtxNew, reservekey))
        return _("Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");

    return "";
}



string CWallet::SendMoneyToDestination(const CTxDestination& address, int64_t nValue, CWalletTx& wtxNew, bool fAskFee)
{
    // Check amount
    if (nValue <= 0)
        return _("Invalid amount");
    if (nValue + nTransactionFee > GetBalance())
        return _("Insufficient funds");

    // Parse Bitcoin address
    CScript scriptPubKey;
    scriptPubKey.SetDestination(address);

    return SendMoney(scriptPubKey, nValue, wtxNew, fAskFee);
}




DBErrors CWallet::LoadWallet(bool& fFirstRunRet)
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    fFirstRunRet = false;
    DBErrors nLoadWalletRet = CWalletDB(strWalletFile,"cr+").LoadWallet(this);
    if (nLoadWalletRet == DB_NEED_REWRITE)
    {
        if (CDB::Rewrite(strWalletFile, "\x04pool"))
        {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // the requires a new key.
        }
    }

    if (nLoadWalletRet != DB_LOAD_OK)
        return nLoadWalletRet;
    fFirstRunRet = !vchDefaultKey.IsValid();

    return DB_LOAD_OK;
}


bool CWallet::SetAddressBookName(const CTxDestination& address, const string& strName)
{
    bool fUpdated = false;
    {
        LOCK(cs_wallet); // mapAddressBook
        std::map<CTxDestination, std::string>::iterator mi = mapAddressBook.find(address);
        fUpdated = mi != mapAddressBook.end();
        mapAddressBook[address] = strName;
    }
    NotifyAddressBookChanged(this, address, strName, ::IsMine(*this, address),
                             (fUpdated ? CT_UPDATED : CT_NEW) );
    if (!fFileBacked)
        return false;
    return CWalletDB(strWalletFile).WriteName(CBitcoinAddress(address).ToString(), strName);
}

bool CWallet::DelAddressBookName(const CTxDestination& address)
{
    {
        LOCK(cs_wallet); // mapAddressBook

        mapAddressBook.erase(address);
    }

    NotifyAddressBookChanged(this, address, "", ::IsMine(*this, address), CT_DELETED);

    if (!fFileBacked)
        return false;
    return CWalletDB(strWalletFile).EraseName(CBitcoinAddress(address).ToString());
}

bool CWallet::SetDefaultKey(const CPubKey &vchPubKey)
{
    if (fFileBacked)
    {
        if (!CWalletDB(strWalletFile).WriteDefaultKey(vchPubKey))
            return false;
    }
    vchDefaultKey = vchPubKey;
    return true;
}

//
// Mark old keypool keys as used,
// and generate all new keys
//
bool CWallet::NewKeyPool()
{
    {
        LOCK(cs_wallet);
        CWalletDB walletdb(strWalletFile);
        for(int64_t nIndex : setKeyPool) {
            walletdb.ErasePool(nIndex);
        }
        setKeyPool.clear();

        if (IsLocked())
            return false;

        int64_t nKeys = max(GetArg("-keypool", 100), (int64_t)0);
        for (int i = 0; i < nKeys; i++)
        {
            int64_t nIndex = i+1;
            walletdb.WritePool(nIndex, CKeyPool(GenerateNewKey()));
            setKeyPool.insert(nIndex);
        }
        LogPrintf("CWallet::NewKeyPool wrote %d new keys\n", nKeys);
    }
    return true;
}

bool CWallet::TopUpKeyPool(unsigned int nSize)
{
    {
        LOCK(cs_wallet);

        if (IsLocked())
            return false;

        CWalletDB walletdb(strWalletFile);

        // Top up key pool
        unsigned int nTargetSize;
        if (nSize > 0)
            nTargetSize = nSize;
        else
            nTargetSize = max(GetArg("-keypool", 100), (int64_t)0);

        while (setKeyPool.size() < (nTargetSize + 1))
        {
            int64_t nEnd = 1;
            if (!setKeyPool.empty())
                nEnd = *(--setKeyPool.end()) + 1;
            if (!walletdb.WritePool(nEnd, CKeyPool(GenerateNewKey())))
                throw runtime_error("TopUpKeyPool() : writing generated key failed");
            setKeyPool.insert(nEnd);
            LogPrintf("keypool added key %d, size=%u\n", nEnd, setKeyPool.size());
        }
    }
    return true;
}

void CWallet::ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool)
{
    nIndex = -1;
    keypool.vchPubKey = CPubKey();
    {
        LOCK(cs_wallet);

        if (!IsLocked())
            TopUpKeyPool();

        // Get the oldest key
        if(setKeyPool.empty())
            return;

        CWalletDB walletdb(strWalletFile);

        nIndex = *(setKeyPool.begin());
        setKeyPool.erase(setKeyPool.begin());
        if (!walletdb.ReadPool(nIndex, keypool))
            throw runtime_error("ReserveKeyFromKeyPool() : read failed");
        if (!HaveKey(keypool.vchPubKey.GetID()))
            throw runtime_error("ReserveKeyFromKeyPool() : unknown key in key pool");
        assert(keypool.vchPubKey.IsValid());
        LogPrintf("keypool reserve %d\n", nIndex);
    }
}

int64_t CWallet::AddReserveKey(const CKeyPool& keypool)
{
    {
        LOCK2(cs_main, cs_wallet);
        CWalletDB walletdb(strWalletFile);

        int64_t nIndex = 1 + *(--setKeyPool.end());
        if (!walletdb.WritePool(nIndex, keypool))
            throw runtime_error("AddReserveKey() : writing added key failed");
        setKeyPool.insert(nIndex);
        return nIndex;
    }
    return -1;
}

void CWallet::KeepKey(int64_t nIndex)
{
    // Remove from key pool
    if (fFileBacked)
    {
        CWalletDB walletdb(strWalletFile);
        walletdb.ErasePool(nIndex);
    }
    LogPrintf("keypool keep %d\n", nIndex);
}

void CWallet::ReturnKey(int64_t nIndex)
{
    // Return to key pool
    {
        LOCK(cs_wallet);
        setKeyPool.insert(nIndex);
    }
    LogPrintf("keypool return %d\n", nIndex);
}

bool CWallet::GetKeyFromPool(CPubKey& result)
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    {
        LOCK(cs_wallet);
        ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex == -1)
        {
            if (IsLocked()) return false;
            result = GenerateNewKey();
            return true;
        }
        KeepKey(nIndex);
        result = keypool.vchPubKey;
    }
    return true;
}

int64_t CWallet::GetOldestKeyPoolTime()
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    ReserveKeyFromKeyPool(nIndex, keypool);
    if (nIndex == -1)
        return GetTime();
    ReturnKey(nIndex);
    return keypool.nTime;
}

std::map<CTxDestination, int64_t> CWallet::GetAddressBalances()
{
    map<CTxDestination, int64_t> balances;

    {
        LOCK(cs_wallet);
        for(std::pair<uint256, CWalletTx> walletEntry : mapWallet)
        {
            CWalletTx *pcoin = &walletEntry.second;

            if (!IsFinalTx(*pcoin) || !pcoin->IsTrusted())
                continue;

            if ((pcoin->IsCoinBase() || pcoin->IsCoinStake()) && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < (pcoin->IsFromMe() ? 0 : 1))
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            {
                CTxDestination addr;
                if (!IsMine(pcoin->vout[i]))
                    continue;
                if(!ExtractDestination(pcoin->vout[i].scriptPubKey, addr))
                    continue;

                int64_t n = pcoin->IsSpent(i) ? 0 : pcoin->vout[i].nValue;

                if (!balances.count(addr))
                    balances[addr] = 0;
                balances[addr] += n;
            }
        }
    }

    return balances;
}

set< set<CTxDestination> > CWallet::GetAddressGroupings()
{
    AssertLockHeld(cs_wallet); // mapWallet
    set< set<CTxDestination> > groupings;
    set<CTxDestination> grouping;

    for(std::pair<uint256, CWalletTx> walletEntry : mapWallet)
    {
        CWalletTx *pcoin = &walletEntry.second;

        if (pcoin->vin.size() > 0 && IsMine(pcoin->vin[0]))
        {
            // group all input addresses with each other
            for(CTxIn txin : pcoin->vin)
            {
                CTxDestination address;
                if(!ExtractDestination(mapWallet[txin.prevout.hash].vout[txin.prevout.n].scriptPubKey, address))
                    continue;
                grouping.insert(address);
            }

            // group change with input addresses
            for(CTxOut txout : pcoin->vout) {
                if (IsChange(txout))
                {
                    CWalletTx tx = mapWallet[pcoin->vin[0].prevout.hash];
                    CTxDestination txoutAddr;
                    if(!ExtractDestination(txout.scriptPubKey, txoutAddr))
                        continue;
                    grouping.insert(txoutAddr);
                }
            }
            groupings.insert(grouping);
            grouping.clear();
        }

        // group lone addrs by themselves
        for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            if (IsMine(pcoin->vout[i]))
            {
                CTxDestination address;
                if(!ExtractDestination(pcoin->vout[i].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                groupings.insert(grouping);
                grouping.clear();
            }
    }

    set< set<CTxDestination>* > uniqueGroupings; // a set of pointers to groups of addresses
    map< CTxDestination, set<CTxDestination>* > setmap;  // map addresses to the unique group containing it
    for(set<CTxDestination> grouping : groupings)
    {
        // make a set of all the groups hit by this new group
        set< set<CTxDestination>* > hits;
        map< CTxDestination, set<CTxDestination>* >::iterator it;
        for(CTxDestination address : grouping) {
            if ((it = setmap.find(address)) != setmap.end())
                hits.insert((*it).second);
        }

        // merge all hit groups into a new single group and delete old groups
        set<CTxDestination>* merged = new set<CTxDestination>(grouping);
        for(set<CTxDestination>* hit : hits)
        {
            merged->insert(hit->begin(), hit->end());
            uniqueGroupings.erase(hit);
            delete hit;
        }
        uniqueGroupings.insert(merged);

        // update setmap
        for(CTxDestination element : *merged) {
            setmap[element] = merged;
        }
    }

    set< set<CTxDestination> > ret;
    for(set<CTxDestination>* uniqueGrouping : uniqueGroupings)
    {
        ret.insert(*uniqueGrouping);
        delete uniqueGrouping;
    }

    return ret;
}

// ppcoin: check 'spent' consistency between wallet and txindex
// ppcoin: fix wallet spent state according to txindex
void CWallet::FixSpentCoins(int& nMismatchFound, int64_t& nBalanceInQuestion, bool fCheckOnly)
{
    nMismatchFound = 0;
    nBalanceInQuestion = 0;

    LOCK(cs_wallet);
    vector<CWalletTx*> vCoins;
    vCoins.reserve(mapWallet.size());
    for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        vCoins.push_back(&(*it).second);

    CTxDB txdb("r");
    for(CWalletTx* pcoin : vCoins)
    {
        // Find the corresponding transaction index
        CTxIndex txindex;
        if (!txdb.ReadTxIndex(pcoin->GetHash(), txindex))
            continue;
        for (unsigned int n=0; n < pcoin->vout.size(); n++)
        {
            if (IsMine(pcoin->vout[n]) && pcoin->IsSpent(n) && (txindex.vSpent.size() <= n || txindex.vSpent[n].IsNull()))
            {
                LogPrintf("FixSpentCoins found lost coin %s BAY %s[%d], %s\n",
                    FormatMoney(pcoin->vout[n].nValue), pcoin->GetHash().ToString(), n, fCheckOnly? "repair not attempted" : "repairing");
                nMismatchFound++;
                nBalanceInQuestion += pcoin->vout[n].nValue;
                if (!fCheckOnly)
                {
                    pcoin->MarkUnspent(n);
                    pcoin->WriteToDisk();
                }
            }
            else if (IsMine(pcoin->vout[n]) && !pcoin->IsSpent(n) && (txindex.vSpent.size() > n && !txindex.vSpent[n].IsNull()))
            {
                LogPrintf("FixSpentCoins found spent coin %s BAY %s[%d], %s\n",
                    FormatMoney(pcoin->vout[n].nValue), pcoin->GetHash().ToString(), n, fCheckOnly? "repair not attempted" : "repairing");
                nMismatchFound++;
                nBalanceInQuestion += pcoin->vout[n].nValue;
                if (!fCheckOnly)
                {
                    pcoin->MarkSpent(n);
                    pcoin->WriteToDisk();
                }
            }
        }
    }
}

// ppcoin: disable transaction (only for coinstake)
void CWallet::DisableTransaction(const CTransaction &tx)
{
    if (!tx.IsCoinStake() || !IsFromMe(tx))
        return; // only disconnecting coinstake requires marking input unspent

    LOCK(cs_wallet);
    for(const CTxIn& txin : tx.vin)
    {
        map<uint256, CWalletTx>::iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size() && IsMine(prev.vout[txin.prevout.n]))
            {
                prev.MarkUnspent(txin.prevout.n);
                prev.WriteToDisk();
            }
        }
    }
}

bool CReserveKey::GetReservedKey(CPubKey& pubkey)
{
    if (nIndex == -1)
    {
        CKeyPool keypool;
        pwallet->ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex != -1)
            vchPubKey = keypool.vchPubKey;
        else {
            if (pwallet->vchDefaultKey.IsValid()) {
                LogPrintf("CReserveKey::GetReservedKey(): Warning: Using default key instead of a new key, top up your keypool!");
                vchPubKey = pwallet->vchDefaultKey;
            } else
                return false;
        }
    }
    assert(vchPubKey.IsValid());
    pubkey = vchPubKey;
    return true;
}

void CReserveKey::KeepKey()
{
    if (nIndex != -1)
        pwallet->KeepKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CReserveKey::ReturnKey()
{
    if (nIndex != -1)
        pwallet->ReturnKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CWallet::GetAllReserveKeys(set<CKeyID>& setAddress) const
{
    setAddress.clear();

    CWalletDB walletdb(strWalletFile);

    LOCK2(cs_main, cs_wallet);
    for(const int64_t& id : setKeyPool)
    {
        CKeyPool keypool;
        if (!walletdb.ReadPool(id, keypool))
            throw runtime_error("GetAllReserveKeyHashes() : read failed");
        assert(keypool.vchPubKey.IsValid());
        CKeyID keyID = keypool.vchPubKey.GetID();
        if (!HaveKey(keyID))
            throw runtime_error("GetAllReserveKeyHashes() : unknown key in key pool");
        setAddress.insert(keyID);
    }
}

void CWallet::UpdatedTransaction(const uint256 &hashTx)
{
    {
        LOCK(cs_wallet);
        // Only notify UI if this transaction is in this wallet
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(hashTx);
        if (mi != mapWallet.end())
            NotifyTransactionChanged(this, hashTx, CT_UPDATED);
    }
}

void CWallet::GetKeyBirthTimes(std::map<CKeyID, int64_t> &mapKeyBirth) const {
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    mapKeyBirth.clear();

    // get birth times for keys with metadata
    for (std::map<CKeyID, CKeyMetadata>::const_iterator it = mapKeyMetadata.begin(); it != mapKeyMetadata.end(); it++)
        if (it->second.nCreateTime)
            mapKeyBirth[it->first] = it->second.nCreateTime;

    // map in which we'll infer heights of other keys
    CBlockIndex *pindexMax = FindBlockByHeight(std::max(0, nBestHeight - 144)); // the tip can be reorganised; use a 144-block safety margin
    std::map<CKeyID, CBlockIndex*> mapKeyFirstBlock;
    std::set<CKeyID> setKeys;
    GetKeys(setKeys);
    for(const CKeyID &keyid : setKeys) {
        if (mapKeyBirth.count(keyid) == 0)
            mapKeyFirstBlock[keyid] = pindexMax;
    }
    setKeys.clear();

    // if there are no such keys, we're done
    if (mapKeyFirstBlock.empty())
        return;

    // find first block that affects those keys, if there are any left
    std::vector<CKeyID> vAffected;
    for (std::map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); it++) {
        // iterate over all wallet transactions...
        const CWalletTx &wtx = (*it).second;
        std::map<uint256, CBlockIndex*>::const_iterator blit = mapBlockIndex.find(wtx.hashBlock);
        if (blit != mapBlockIndex.end() && blit->second->IsInMainChain()) {
            // ... which are already in a block
            int nHeight = blit->second->nHeight;
            for(const CTxOut &txout : wtx.vout) {
                // iterate over all their outputs
                ::ExtractAffectedKeys(*this, txout.scriptPubKey, vAffected);
                for(const CKeyID &keyid : vAffected) {
                    // ... and all their affected keys
                    std::map<CKeyID, CBlockIndex*>::iterator rit = mapKeyFirstBlock.find(keyid);
                    if (rit != mapKeyFirstBlock.end() && nHeight < rit->second->nHeight)
                        rit->second = blit->second;
                }
                vAffected.clear();
            }
        }
    }

    // Extract block timestamps for those keys
    for (std::map<CKeyID, CBlockIndex*>::const_iterator it = mapKeyFirstBlock.begin(); it != mapKeyFirstBlock.end(); it++)
        mapKeyBirth[it->first] = it->second->nTime - 7200; // block times can be 2h off
}

void CWallet::SetBayRates(std::vector<double> bay_rates) {
    LOCK2(cs_main, cs_wallet);
    vBayRates = bay_rates;
    
    if (trackerVoteType != PEG_VOTE_NONE) {
        return;
    }
    
    double dPeakRate = 0.;
    
    CPegDB pegdb("cr+");
    bool ok = pegdb.ReadPegBayPeakRate(dPeakRate);
    if (!ok) {
        dPeakRate = 0.;
    }
    
    if (dPeakRate == 0) {
        // just max if no previous peak
        for(double r : vBayRates) {
            if (r>0) {
                if (dPeakRate < r)
                    dPeakRate = r;
            }
        }
    }
    
    int all_over_peak_in_serie = 0;
    bool all_over_peak = false;
    for(double r : vBayRates) {
        if (r>0) {
            if (dPeakRate < (r * 0.35)) {
                all_over_peak_in_serie++;
            } else {
                all_over_peak_in_serie = 0;
            }
        }
        if (TestNet() && all_over_peak_in_serie >= 50) {
            all_over_peak = true;
        } else if (all_over_peak_in_serie >= 250) {
            all_over_peak = true;
        }
    }
    
    if (all_over_peak) {
        dPeakRate *= 1.5;
    }
    
    if (dPeakRate > 0) {
        dBayPeakPrice = dPeakRate;
        pegdb.TxnBegin();
        pegdb.WritePegBayPeakRate(dPeakRate);
        pegdb.TxnCommit();
    }
    pegdb.Close();
}

void CWallet::SetBtcRates(std::vector<double> btc_rates) {
    LOCK2(cs_main, cs_wallet);
    vBtcRates = btc_rates;
    
    if (trackerVoteType != PEG_VOTE_NONE) {
        return;
    }
    
    double dPeakRate = 0.;
    
    CPegDB pegdb("cr+");
    bool ok = pegdb.ReadPegBayPeakRate(dPeakRate);
    if (!ok) {
        dPeakRate = 0.;
    }
    
    for(double r : vBtcRates) {
        if (r>0) {
            // as min follow btc price as 1/100'000
            double r_bay = r / 100000.;
            if (dPeakRate < r_bay)
                dPeakRate = r_bay;
        }
    }
    
    if (dPeakRate > 0) {
        dBayPeakPrice = dPeakRate;
        pegdb.TxnBegin();
        pegdb.WritePegBayPeakRate(dPeakRate);
        pegdb.TxnCommit();
    }
    pegdb.Close();
}

void CWallet::SetTrackerVote(PegVoteType vote, double dPeakRate)
{
    LOCK2(cs_main, cs_wallet);
    dBayPeakPrice = dPeakRate;
    trackerVoteType = vote;
    lastAutoPegVoteType = vote;
}

bool CWallet::SetRewardAddress(std::string addr, bool write_wallet)
{ 
    LOCK(cs_wallet);
    if (rewardAddress == addr) {
        return true;
    }
    
    if (!addr.empty()) {
        CBitcoinAddress address(addr);
        if (!address.IsValid()) {
            return false;
        }
    }
    
    if (write_wallet) {
        CWalletDB walletdb(strWalletFile);
        if (!walletdb.WriteRewardAddress(addr)) {
            return false;
        }
    }
    
    rewardAddress = addr; 
    return true; 
}

std::string CWallet::GetRewardAddress() const
{
    LOCK(cs_wallet); 
    return rewardAddress;
}

bool CWallet::SetConsolidateEnabled(bool on, bool write_wallet)
{
    LOCK(cs_wallet);
    if (consolidateEnabled == on) {
        return true;
    }
    
    if (write_wallet) {
        CWalletDB walletdb(strWalletFile);
        if (!walletdb.WriteConsolidateEnabled(on)) {
            return false;
        }
    }
    
    consolidateEnabled = on; 
    return true; 
}

bool CWallet::GetConsolidateEnabled() const
{
    LOCK(cs_wallet); 
    return consolidateEnabled;
}

bool CWallet::SetSupportEnabled(bool on, bool write_wallet)
{
    LOCK(cs_wallet);
    if (supportEnabled == on) {
        return true;
    }
    
    if (write_wallet) {
        CWalletDB walletdb(strWalletFile);
        if (!walletdb.WriteSupportEnabled(on)) {
            return false;
        }
    }
    
    supportEnabled = on; 
    return true; 
}

bool CWallet::GetSupportEnabled() const
{
    LOCK(cs_wallet); 
    return supportEnabled;
}

bool CWallet::SetSupportAddress(std::string addr, bool write_wallet)
{ 
    LOCK(cs_wallet);
    if (supportAddress == addr) {
        return true;
    }
    
    if (!addr.empty()) {
        CBitcoinAddress address(addr);
        if (!address.IsValid()) {
            return false;
        }
    }
    
    if (write_wallet) {
        CWalletDB walletdb(strWalletFile);
        if (!walletdb.WriteSupportAddress(addr)) {
            return false;
        }
    }
    
    supportAddress = addr; 
    return true; 
}

std::string CWallet::GetSupportAddress() const
{
    LOCK(cs_wallet); 
    if (supportAddress.empty()) {
        CWalletDB walletdb(strWalletFile);
        string sSupportAddr;
        if (walletdb.ReadSupportAddress(sSupportAddr)) {
            return sSupportAddr;
        }
    }
    return supportAddress;
}

bool CWallet::SetSupportPart(uint32_t percent, bool write_wallet)
{ 
    LOCK(cs_wallet);
    percent = std::min(uint32_t(100), percent);
    if (supportPart == percent) {
        return true;
    }
    
    if (write_wallet) {
        CWalletDB walletdb(strWalletFile);
        if (!walletdb.WriteSupportPart(percent)) {
            return false;
        }
    }
    
    supportPart = percent; 
    return true; 
}

uint32_t CWallet::GetSupportPart() const
{
    LOCK(cs_wallet); 
    return supportPart;
}
