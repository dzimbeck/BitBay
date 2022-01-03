#include "walletmodel.h"
#include "guiconstants.h"
#include "optionsmodel.h"
#include "addresstablemodel.h"
#include "transactiontablemodel.h"

#include "ui_interface.h"
#include "wallet.h"
#include "walletdb.h" // for BackupWallet
#include "base58.h"

#include <QSet>
#include <QTimer>
#include <QDebug>

#include <boost/bind/bind.hpp>
using namespace boost::placeholders;

WalletModel::WalletModel(CWallet *wallet, OptionsModel *optionsModel, QObject *parent) :
    QObject(parent), wallet(wallet), optionsModel(optionsModel), addressTableModel(0),
    transactionTableModel(0),
    cachedBalance(0), cachedReserve(0), cachedLiquidity(0), cachedFrozen(0),
    cachedStake(0), cachedUnconfirmedBalance(0), cachedImmatureBalance(0),
    cachedEncryptionStatus(Unencrypted),
    cachedNumBlocks(0)
{
    fForceCheckBalanceChanged = false;

    addressTableModel = new AddressTableModel(wallet, this);
    transactionTableModel = new TransactionTableModel(wallet, this);

    // This timer will be fired repeatedly to update the balance
    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(pollBalanceChanged()));
    pollTimer->start(MODEL_UPDATE_DELAY);

    subscribeToCoreSignals();
}

WalletModel::~WalletModel()
{
    unsubscribeFromCoreSignals();
}

int WalletModel::getPegSupplyIndex() const {
    return wallet->GetPegSupplyIndex();
}

int WalletModel::getPegSupplyNIndex() const {
    return wallet->GetPegSupplyNIndex();
}

int WalletModel::getPegSupplyNNIndex() const {
    return wallet->GetPegSupplyNNIndex();
}

qint64 WalletModel::getBalance(const CCoinControl *coinControl) const
{
    if (coinControl)
    {
        qint64 nBalance = 0;
        std::vector<COutput> vCoins;
        wallet->AvailableCoins(vCoins, true, true, coinControl);
        for(const COutput& out : vCoins) {
            if(out.fSpendable) {
                nBalance += out.tx->vout[out.i].nValue;
            }
        }

        return nBalance;
    }

    return wallet->GetBalance();
}

qint64 WalletModel::getReserve(const CCoinControl *coinControl) const
{
    if (coinControl)
    {
        qint64 nReserve = 0;
        std::vector<COutput> vCoins;
        wallet->AvailableCoins(vCoins, true, true, coinControl);
        for(const COutput& out : vCoins) {
            if(out.fSpendable && !out.IsFrozen(wallet->nLastBlockTime)) {
                nReserve += out.tx->vOutFractions[out.i].Ref().Low(getPegSupplyIndex());
            }
        }

        return nReserve;
    }

    return wallet->GetReserve();
}

qint64 WalletModel::getLiquidity(const CCoinControl *coinControl) const
{
    if (coinControl)
    {
        qint64 nLiquidity = 0;
        std::vector<COutput> vCoins;
        wallet->AvailableCoins(vCoins, true, true, coinControl);
        for(const COutput& out : vCoins) {
            if(out.fSpendable && !out.IsFrozen(wallet->nLastBlockTime)) {
                nLiquidity += out.tx->vOutFractions[out.i].Ref().High(getPegSupplyIndex());
            }
        }

        return nLiquidity;
    }

    return wallet->GetLiquidity();
}

qint64 WalletModel::getFrozen(const CCoinControl *coinControl, 
                              vector<CFrozenCoinInfo> *frozenCoins) const
{
    if (coinControl)
    {
        qint64 nFrozen = 0;
        std::vector<COutput> vCoins;
        wallet->AvailableCoins(vCoins, true, true, coinControl);
        for(const COutput& out : vCoins) {
            if(out.fSpendable && out.IsFrozen(wallet->nLastBlockTime)) {
                nFrozen += out.tx->vout[out.i].nValue;
                if (frozenCoins) {
                    CFrozenCoinInfo fcoin;
                    fcoin.txhash = out.tx->GetHash();
                    fcoin.n = out.i;
                    fcoin.nValue = out.tx->vout[out.i].nValue;
                    fcoin.nFlags = out.tx->vOutFractions[out.i].nFlags();
                    fcoin.nLockTime = out.tx->vOutFractions[out.i].nLockTime();
                    frozenCoins->push_back(fcoin);
                }
            }
        }

        return nFrozen;
    }

    return wallet->GetFrozen(frozenCoins);
}

bool WalletModel::getRewardInfo(std::vector<RewardInfo> & vRewardInfo) const
{
    return wallet->GetRewardInfo(vRewardInfo);
}

qint64 WalletModel::getUnconfirmedBalance() const
{
    return wallet->GetUnconfirmedBalance();
}

qint64 WalletModel::getStake() const
{
    return wallet->GetStake();
}

qint64 WalletModel::getImmatureBalance() const
{
    return wallet->GetImmatureBalance();
}

void WalletModel::updateStatus()
{
    EncryptionStatus newEncryptionStatus = getEncryptionStatus();

    if(cachedEncryptionStatus != newEncryptionStatus)
        emit encryptionStatusChanged(newEncryptionStatus);
}

void WalletModel::pollBalanceChanged()
{
    // Get required locks upfront. This avoids the GUI from getting stuck on
    // periodical polls if the core is holding the locks for a longer time -
    // for example, during a wallet rescan.
    TRY_LOCK(cs_main, lockMain);
    if(!lockMain)
        return;
    TRY_LOCK(wallet->cs_wallet, lockWallet);
    if(!lockWallet)
        return;

    if(fForceCheckBalanceChanged || nBestHeight != cachedNumBlocks)
    {
        fForceCheckBalanceChanged = false;

        // Balance and number of transactions might have changed
        cachedNumBlocks = nBestHeight;

        checkBalanceChanged();
        if(transactionTableModel)
            transactionTableModel->updateConfirmations();
    }
}

void WalletModel::checkBalanceChanged()
{
    vector<CFrozenCoinInfo> newFrozenCoins;
    qint64 newBalance = getBalance();
    qint64 newReserve = getReserve();
    qint64 newLiquidity = getLiquidity();
    qint64 newFrozen = getFrozen(NULL, &newFrozenCoins);
    qint64 newStake = getStake();
    qint64 newUnconfirmedBalance = getUnconfirmedBalance();
    qint64 newImmatureBalance = getImmatureBalance();

    if(cachedBalance != newBalance || 
            cachedReserve != newReserve || 
            cachedLiquidity != newLiquidity || 
            cachedFrozen != newFrozen || 
            cachedFrozenCoins != newFrozenCoins ||
            cachedStake != newStake || 
            cachedUnconfirmedBalance != newUnconfirmedBalance || 
            cachedImmatureBalance != newImmatureBalance)
    {
        cachedBalance = newBalance;
        cachedReserve = newReserve;
        cachedLiquidity = newLiquidity;
        cachedFrozen = newFrozen;
        cachedFrozenCoins = newFrozenCoins;
        cachedStake = newStake;
        cachedUnconfirmedBalance = newUnconfirmedBalance;
        cachedImmatureBalance = newImmatureBalance;
        emit balanceChanged(newBalance, 
                            newReserve, newLiquidity, newFrozen, newFrozenCoins,
                            newStake, newUnconfirmedBalance, newImmatureBalance);
    }
    
    vector<RewardInfo> vRewardsInfo;
    vRewardsInfo.push_back({PEG_REWARD_5 ,0,0,0});
    vRewardsInfo.push_back({PEG_REWARD_10,0,0,0});
    vRewardsInfo.push_back({PEG_REWARD_20,0,0,0});
    vRewardsInfo.push_back({PEG_REWARD_40,0,0,0});
    getRewardInfo(vRewardsInfo);
    
    bool changed = false;
    if (cachedRewardsInfo.size() != PEG_REWARD_LAST) {
        cachedRewardsInfo = vRewardsInfo;
        changed = true;
    }
    else {
        for(int i=0; i<PEG_REWARD_LAST; i++) {
            if (vRewardsInfo[i].amount != cachedRewardsInfo[i].amount) changed = true;
            if (vRewardsInfo[i].count != cachedRewardsInfo[i].count) changed = true;
            if (vRewardsInfo[i].stake != cachedRewardsInfo[i].stake) changed = true;
        }
    }
    
    if (changed) {
        emit rewardsInfoChanged(vRewardsInfo[PEG_REWARD_5 ].amount,
                                vRewardsInfo[PEG_REWARD_10].amount,
                                vRewardsInfo[PEG_REWARD_20].amount,
                                vRewardsInfo[PEG_REWARD_40].amount,
                               
                                vRewardsInfo[PEG_REWARD_5 ].count,
                                vRewardsInfo[PEG_REWARD_10].count,
                                vRewardsInfo[PEG_REWARD_20].count,
                                vRewardsInfo[PEG_REWARD_40].count,
                                
                                vRewardsInfo[PEG_REWARD_5 ].stake,
                                vRewardsInfo[PEG_REWARD_10].stake,
                                vRewardsInfo[PEG_REWARD_20].stake,
                                vRewardsInfo[PEG_REWARD_40].stake);
    }
}

void WalletModel::updateTransaction(const QString &hash, int status)
{
    if(transactionTableModel)
        transactionTableModel->updateTransaction(hash, status);

    // Balance and number of transactions might have changed
    fForceCheckBalanceChanged = true;
}

void WalletModel::updateAddressBook(const QString &address, const QString &label, bool isMine, int status)
{
    if(addressTableModel)
        addressTableModel->updateEntry(address, label, isMine, status);
}

bool WalletModel::validateAddress(const QString &address)
{
    CBitcoinAddress addressParsed(address.toStdString());
    return addressParsed.IsValid();
}

WalletModel::SendCoinsReturn WalletModel::sendCoins(const QList<SendCoinsRecipient> &recipients, 
                                                    PegTxType txType, 
                                                    const CCoinControl *coinControl,
                                                    std::string & sFailCause)
{
    qint64 total = 0;
    QSet<QString> setAddress;
    QString hex;

    if(recipients.empty())
    {
        return OK;
    }

    if (txType == PEG_MAKETX_SEND_RESERVE) {
    }
    else if (txType == PEG_MAKETX_FREEZE_RESERVE) {
    }
    else if (txType == PEG_MAKETX_SEND_LIQUIDITY) {
    }
    else if (txType == PEG_MAKETX_FREEZE_LIQUIDITY) {
    }
    else {
        return InvalidTxType;
    }
    
    // Pre-check input data for validity
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        if(!validateAddress(rcp.address))
        {
            return InvalidAddress;
        }
        setAddress.insert(rcp.address);

        if(rcp.amount <= 0)
        {
            return InvalidAmount;
        }
        total += rcp.amount;
    }

    if(recipients.size() > setAddress.size())
    {
        return DuplicateAddress;
    }

    qint64 nBalance = 0;
    qint64 nBalanceAll = getBalance(coinControl);
    
    if (txType == PEG_MAKETX_SEND_RESERVE ||
        txType == PEG_MAKETX_FREEZE_RESERVE) {
        nBalance = getReserve(coinControl);
    }else if (txType == PEG_MAKETX_SEND_LIQUIDITY ||
              txType == PEG_MAKETX_FREEZE_LIQUIDITY) {
        nBalance = getLiquidity(coinControl);
    }

    if(total > nBalance)
    {
        return AmountExceedsBalance;
    }

    if((total + nTransactionFee) > nBalanceAll) // fee can be paid from both reserves and liquidity
    {
        return SendCoinsReturn(AmountWithFeeExceedsBalance, nTransactionFee);
    }

    {
        LOCK2(cs_main, wallet->cs_wallet);

        // Sendmany
        std::vector<std::pair<CScript, int64_t> > vecSend;
        foreach(const SendCoinsRecipient &rcp, recipients)
        {
            CScript scriptPubKey;
            scriptPubKey.SetDestination(CBitcoinAddress(rcp.address.toStdString()).Get());
            vecSend.push_back(make_pair(scriptPubKey, rcp.amount));
        }

        CWalletTx wtx;
        CReserveKey keyChange(wallet);
        int64_t nFeeRequired = 0;
        bool fCreated = wallet->CreateTransaction(txType, 
                                                  vecSend, 
                                                  wtx, 
                                                  keyChange, 
                                                  nFeeRequired, 
                                                  coinControl, 
                                                  false, 
                                                  sFailCause);

        if(!fCreated)
        {
            if((total + nFeeRequired) > nBalance) // FIXME: could cause collisions in the future
            {
                return SendCoinsReturn(AmountWithFeeExceedsBalance, nFeeRequired);
            }
            return TransactionCreationFailed;
        }
        if(!uiInterface.ThreadSafeAskFee(nFeeRequired, tr("Sending...").toStdString()))
        {
            return Aborted;
        }
        if(!wallet->CommitTransaction(wtx, keyChange))
        {
            return TransactionCommitFailed;
        }
        hex = QString::fromStdString(wtx.GetHash().GetHex());
    }

    // Add addresses / update labels that we've sent to to the address book
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        std::string strAddress = rcp.address.toStdString();
        CTxDestination dest = CBitcoinAddress(strAddress).Get();
        std::string strLabel = rcp.label.toStdString();
        {
            LOCK(wallet->cs_wallet);

            std::map<CTxDestination, std::string>::iterator mi = wallet->mapAddressBook.find(dest);

            // Check if we have a new address or an updated label
            if (mi == wallet->mapAddressBook.end() || mi->second != strLabel)
            {
                wallet->SetAddressBookName(dest, strLabel);
            }
        }
    }
    checkBalanceChanged(); // update balance immediately, otherwise there could be a short noticeable delay until pollBalanceChanged hits

    return SendCoinsReturn(OK, 0, hex);
}

WalletModel::SendCoinsReturn WalletModel::sendCoinsTest(CWalletTx& wtx,
                                                        const QList<SendCoinsRecipient>& recipients, 
                                                        PegTxType txType, 
                                                        const CCoinControl *coinControl,
                                                        std::string & sFailCause)
{
    qint64 total = 0;
    QSet<QString> setAddress;
    QString hex;

    if(recipients.empty())
    {
        return OK;
    }

    if (txType == PEG_MAKETX_SEND_RESERVE) {
    }
    else if (txType == PEG_MAKETX_FREEZE_RESERVE) {
    }
    else if (txType == PEG_MAKETX_SEND_LIQUIDITY) {
    }
    else if (txType == PEG_MAKETX_FREEZE_LIQUIDITY) {
    }
    else {
        return InvalidTxType;
    }
    
    // Pre-check input data for validity
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        if(!validateAddress(rcp.address))
        {
            return InvalidAddress;
        }
        setAddress.insert(rcp.address);

        if(rcp.amount <= 0)
        {
            return InvalidAmount;
        }
        total += rcp.amount;
    }

    if(recipients.size() > setAddress.size())
    {
        return DuplicateAddress;
    }

    qint64 nBalance = 0;
    qint64 nBalanceAll = getBalance(coinControl);
    
    if (txType == PEG_MAKETX_SEND_RESERVE ||
        txType == PEG_MAKETX_FREEZE_RESERVE) {
        nBalance = getReserve(coinControl);
    }else if (txType == PEG_MAKETX_SEND_LIQUIDITY ||
              txType == PEG_MAKETX_FREEZE_LIQUIDITY) {
        nBalance = getLiquidity(coinControl);
    }

    if(total > nBalance)
    {
        return AmountExceedsBalance;
    }

    if((total + nTransactionFee) > nBalanceAll) // fee can be paid from both reserves and liquidity
    {
        return SendCoinsReturn(AmountWithFeeExceedsBalance, nTransactionFee);
    }

    {
        LOCK2(cs_main, wallet->cs_wallet);

        // Sendmany
        std::vector<std::pair<CScript, int64_t> > vecSend;
        foreach(const SendCoinsRecipient &rcp, recipients)
        {
            CScript scriptPubKey;
            scriptPubKey.SetDestination(CBitcoinAddress(rcp.address.toStdString()).Get());
            vecSend.push_back(make_pair(scriptPubKey, rcp.amount));
        }

        CReserveKey keyChange(wallet);
        int64_t nFeeRequired = 0;
        bool fCreated = wallet->CreateTransaction(txType, 
                                                  vecSend, 
                                                  wtx, 
                                                  keyChange, 
                                                  nFeeRequired, 
                                                  coinControl, 
                                                  true /*fTest*/, 
                                                  sFailCause);

        if(!fCreated)
        {
            if((total + nFeeRequired) > nBalance) // FIXME: could cause collisions in the future
            {
                return SendCoinsReturn(AmountWithFeeExceedsBalance, nFeeRequired);
            }
            return TransactionCreationFailed;
        }

        hex = QString::fromStdString(wtx.GetHash().GetHex());
    }

    return SendCoinsReturn(OK, 0, hex);
}

OptionsModel *WalletModel::getOptionsModel()
{
    return optionsModel;
}

AddressTableModel *WalletModel::getAddressTableModel()
{
    return addressTableModel;
}

TransactionTableModel *WalletModel::getTransactionTableModel()
{
    return transactionTableModel;
}

WalletModel::EncryptionStatus WalletModel::getEncryptionStatus() const
{
    if(!wallet->IsCrypted())
    {
        return Unencrypted;
    }
    else if(wallet->IsLocked())
    {
        return Locked;
    }
    else
    {
        return Unlocked;
    }
}

bool WalletModel::setWalletEncrypted(bool encrypted, const SecureString &passphrase)
{
    if(encrypted)
    {
        // Encrypt
        return wallet->EncryptWallet(passphrase);
    }
    else
    {
        // Decrypt -- TODO; not supported yet
        return false;
    }
}

bool WalletModel::setWalletLocked(bool locked, const SecureString &passPhrase)
{
    if(locked)
    {
        // Lock
        return wallet->Lock();
    }
    else
    {
        // Unlock
        return wallet->Unlock(passPhrase);
    }
}

bool WalletModel::changePassphrase(const SecureString &oldPass, const SecureString &newPass)
{
    bool retval;
    {
        LOCK(wallet->cs_wallet);
        wallet->Lock(); // Make sure wallet is locked before attempting pass change
        retval = wallet->ChangeWalletPassphrase(oldPass, newPass);
    }
    return retval;
}

bool WalletModel::backupWallet(const QString &filename)
{
    return BackupWallet(*wallet, filename.toLocal8Bit().data());
}

// Handlers for core signals
static void NotifyKeyStoreStatusChanged(WalletModel *walletmodel, CCryptoKeyStore *wallet)
{
    qDebug() << "NotifyKeyStoreStatusChanged";
    QMetaObject::invokeMethod(walletmodel, "updateStatus", Qt::QueuedConnection);
}

static void NotifyAddressBookChanged(WalletModel *walletmodel, CWallet *wallet, const CTxDestination &address, const std::string &label, bool isMine, ChangeType status)
{
    QString strAddress = QString::fromStdString(CBitcoinAddress(address).ToString());
    QString strLabel = QString::fromStdString(label);

    qDebug() << "NotifyAddressBookChanged : " + strAddress + " " + strLabel + " isMine=" + QString::number(isMine) + " status=" + QString::number(status);
    QMetaObject::invokeMethod(walletmodel, "updateAddressBook", Qt::QueuedConnection,
                              Q_ARG(QString, strAddress),
                              Q_ARG(QString, strLabel),
                              Q_ARG(bool, isMine),
                              Q_ARG(int, status));
}

static void NotifyTransactionChanged(WalletModel *walletmodel, CWallet *wallet, const uint256 &hash, ChangeType status)
{
    QString strHash = QString::fromStdString(hash.GetHex());

    qDebug() << "NotifyTransactionChanged : " + strHash + " status= " + QString::number(status);
    QMetaObject::invokeMethod(walletmodel, "updateTransaction", Qt::QueuedConnection,
                              Q_ARG(QString, strHash),
                              Q_ARG(int, status));
}

void WalletModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyStatusChanged.connect(boost::bind(&NotifyKeyStoreStatusChanged, this, _1));
    wallet->NotifyAddressBookChanged.connect(boost::bind(NotifyAddressBookChanged, this, _1, _2, _3, _4, _5));
    wallet->NotifyTransactionChanged.connect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
}

void WalletModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyStatusChanged.disconnect(boost::bind(&NotifyKeyStoreStatusChanged, this, _1));
    wallet->NotifyAddressBookChanged.disconnect(boost::bind(NotifyAddressBookChanged, this, _1, _2, _3, _4, _5));
    wallet->NotifyTransactionChanged.disconnect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
}

// WalletModel::UnlockContext implementation
WalletModel::UnlockContext WalletModel::requestUnlock()
{
    bool was_locked = getEncryptionStatus() == Locked;

    if ((!was_locked) && fWalletUnlockStakingOnly)
    {
       setWalletLocked(true);
       was_locked = getEncryptionStatus() == Locked;

    }
    if(was_locked)
    {
        // Request UI to unlock wallet
        emit requireUnlock();
    }
    // If wallet is still locked, unlock was failed or cancelled, mark context as invalid
    bool valid = getEncryptionStatus() != Locked;

    return UnlockContext(this, valid, was_locked && !fWalletUnlockStakingOnly);
}

WalletModel::UnlockContext::UnlockContext(WalletModel *wallet, bool valid, bool relock):
        wallet(wallet),
        valid(valid),
        relock(relock)
{
}

WalletModel::UnlockContext::~UnlockContext()
{
    if(valid && relock)
    {
        wallet->setWalletLocked(true);
    }
}

void WalletModel::UnlockContext::CopyFrom(const UnlockContext& rhs)
{
    // Transfer context; old object no longer relocks wallet
    *this = rhs;
    rhs.relock = false;
}

bool WalletModel::getPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    return wallet->GetPubKey(address, vchPubKeyOut);
}

// returns a list of COutputs from COutPoints
void WalletModel::getOutputs(const std::vector<COutPoint>& vOutpoints, std::vector<COutput>& vOutputs)
{
    LOCK2(cs_main, wallet->cs_wallet);
    for(const COutPoint& outpoint : vOutpoints)
    {
        if (!wallet->mapWallet.count(outpoint.hash)) continue;
        int nDepth = wallet->mapWallet[outpoint.hash].GetDepthInMainChain();
        if (nDepth < 0) continue;
        COutput out(&wallet->mapWallet[outpoint.hash], outpoint.n, nDepth, true);
        vOutputs.push_back(out);
    }
}

// AvailableCoins + LockedCoins grouped by wallet address (put change in one group with wallet address)
void WalletModel::listCoins(std::map<QString, std::vector<COutput> >& mapCoins) const
{
    std::vector<COutput> vCoins;
    wallet->AvailableCoins(vCoins, /*fOnlyConfirmed*/ true, /*fUseFrozenUnlocked*/ true, /*coinControl*/ NULL);

    LOCK2(cs_main, wallet->cs_wallet); // ListLockedCoins, mapWallet

    for(const COutput& out : vCoins)
    {
        COutput cout = out;

        while (wallet->IsChange(cout.tx->vout[cout.i]) && cout.tx->vin.size() > 0 && wallet->IsMine(cout.tx->vin[0]))
        {
            if (!wallet->mapWallet.count(cout.tx->vin[0].prevout.hash)) break;
            cout = COutput(&wallet->mapWallet[cout.tx->vin[0].prevout.hash], cout.tx->vin[0].prevout.n, 0, true);
        }

        CTxDestination address;
        if(!out.fSpendable || !ExtractDestination(cout.tx->vout[cout.i].scriptPubKey, address))
            continue;
        mapCoins[CBitcoinAddress(address).ToString().c_str()].push_back(out);
    }
}


void WalletModel::setBayRates(std::vector<double> vRates) {
    wallet->SetBayRates(vRates);
}

void WalletModel::setBtcRates(std::vector<double> vRates) {
    wallet->SetBtcRates(vRates);
}

void WalletModel::setTrackerVote(PegVoteType vote, double dPeakRate) {
    wallet->SetTrackerVote(vote, dPeakRate);
}

