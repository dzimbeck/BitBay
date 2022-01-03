#ifndef WALLETMODEL_H
#define WALLETMODEL_H

#include <QObject>
#include <QVector>
#include <vector>
#include <map>

#include "peg.h"
#include "wallet.h"
#include "allocators.h" /* for SecureString */

class OptionsModel;
class AddressTableModel;
class TransactionTableModel;
class CWallet;
class CKeyID;
class CPubKey;
class COutput;
class COutPoint;
class uint256;
class CCoinControl;
class CWalletTx;
class CFrozenCoinInfo;
struct RewardInfo;

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

class SendCoinsRecipient
{
public:
    QString address;
    QString label;
    qint64 amount;
};

/** Interface to Bitcoin wallet from Qt view code. */
class WalletModel : public QObject
{
    Q_OBJECT

public:
    explicit WalletModel(CWallet *wallet, OptionsModel *optionsModel, QObject *parent = 0);
    ~WalletModel();

    enum StatusCode // Returned by sendCoins
    {
        OK,
        InvalidAmount,
        InvalidAddress,
        InvalidTxType,
        AmountExceedsBalance,
        AmountWithFeeExceedsBalance,
        DuplicateAddress,
        TransactionCreationFailed, // Error returned when wallet is still locked
        TransactionCommitFailed,
        Aborted
    };

    enum EncryptionStatus
    {
        Unencrypted,  // !wallet->IsCrypted()
        Locked,       // wallet->IsCrypted() && wallet->IsLocked()
        Unlocked      // wallet->IsCrypted() && !wallet->IsLocked()
    };

    OptionsModel *getOptionsModel();
    AddressTableModel *getAddressTableModel();
    TransactionTableModel *getTransactionTableModel();

    int getPegSupplyIndex() const;
    int getPegSupplyNIndex() const;
    int getPegSupplyNNIndex() const;
    qint64 getBalance(const CCoinControl *coinControl=NULL) const;
    qint64 getReserve(const CCoinControl *coinControl=NULL) const;
    qint64 getLiquidity(const CCoinControl *coinControl=NULL) const;
    qint64 getFrozen(const CCoinControl *coinControl=NULL, 
                     std::vector<CFrozenCoinInfo> *pFrozenCoins=NULL) const;
    qint64 getStake() const;
    qint64 getUnconfirmedBalance() const;
    qint64 getImmatureBalance() const;
    bool getRewardInfo(std::vector<RewardInfo> &) const;
    EncryptionStatus getEncryptionStatus() const;

    // Check address for validity
    bool validateAddress(const QString &address);

    // Return status record for SendCoins, contains error id + information
    struct SendCoinsReturn
    {
        SendCoinsReturn(StatusCode status=Aborted,
                         qint64 fee=0,
                         QString hex=QString()):
            status(status), fee(fee), hex(hex) {}
        StatusCode status;
        qint64 fee; // is used in case status is "AmountWithFeeExceedsBalance"
        QString hex; // is filled with the transaction hash if status is "OK"
    };

    // Send coins to a list of recipients
    SendCoinsReturn sendCoins(const QList<SendCoinsRecipient> &recipients, 
                              PegTxType nTxType, 
                              const CCoinControl *coinControl,
                              std::string & sFailCause);
    SendCoinsReturn sendCoinsTest(CWalletTx& wtx,
                                  const QList<SendCoinsRecipient> &recipients, 
                                  PegTxType nTxType, 
                                  const CCoinControl *coinControl,
                                  std::string & sFailCause);

    // Wallet encryption
    bool setWalletEncrypted(bool encrypted, const SecureString &passphrase);
    // Passphrase only needed when unlocking
    bool setWalletLocked(bool locked, const SecureString &passPhrase=SecureString());
    bool changePassphrase(const SecureString &oldPass, const SecureString &newPass);
    // Wallet backup
    bool backupWallet(const QString &filename);

    // RAI object for unlocking wallet, returned by requestUnlock()
    class UnlockContext
    {
    public:
        UnlockContext(WalletModel *wallet, bool valid, bool relock);
        ~UnlockContext();

        bool isValid() const { return valid; }

        // Copy operator and constructor transfer the context
        UnlockContext(const UnlockContext& obj) { CopyFrom(obj); }
        UnlockContext& operator=(const UnlockContext& rhs) { CopyFrom(rhs); return *this; }
    private:
        WalletModel *wallet;
        bool valid;
        mutable bool relock; // mutable, as it can be set to false by copying

        void CopyFrom(const UnlockContext& rhs);
    };

    UnlockContext requestUnlock();

    bool getPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const;
    void getOutputs(const std::vector<COutPoint>& vOutpoints, std::vector<COutput>& vOutputs);
    void listCoins(std::map<QString, std::vector<COutput> >& mapCoins) const;

    void setBayRates(std::vector<double>);
    void setBtcRates(std::vector<double>);
    void setTrackerVote(PegVoteType, double dPeakRate);
    
private:
    CWallet *wallet;
    bool fForceCheckBalanceChanged;

    // Wallet has an options model for wallet-specific options
    // (transaction fee, for example)
    OptionsModel *optionsModel;

    AddressTableModel *addressTableModel;
    TransactionTableModel *transactionTableModel;

    // Cache some values to be able to detect changes
    qint64 cachedBalance;
    qint64 cachedReserve;
    qint64 cachedLiquidity;
    qint64 cachedFrozen;
    qint64 cachedStake;
    qint64 cachedUnconfirmedBalance;
    qint64 cachedImmatureBalance;
    EncryptionStatus cachedEncryptionStatus;
    int cachedNumBlocks;
    std::vector<RewardInfo> cachedRewardsInfo;
    std::vector<CFrozenCoinInfo> cachedFrozenCoins;

    QTimer *pollTimer;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();
    void checkBalanceChanged();


public slots:
    /* Wallet status might have changed */
    void updateStatus();
    /* New transaction, or transaction changed status */
    void updateTransaction(const QString &hash, int status);
    /* New, updated or removed address book entry */
    void updateAddressBook(const QString &address, const QString &label, bool isMine, int status);
    /* Current, immature or unconfirmed balance might have changed - emit 'balanceChanged' if so */
    void pollBalanceChanged();

signals:
    // Signal that balance in wallet changed
    void balanceChanged(qint64 balance, 
                        qint64 reserves, qint64 liquidity, qint64 frozen,
                        std::vector<CFrozenCoinInfo>,
                        qint64 stake, qint64 unconfirmedBalance, qint64 immatureBalance);

    // Signal that balance in wallet changed
    void rewardsInfoChanged(qint64 reward5, qint64 reward10, qint64 reward20, qint64 reward40, 
                            int count5, int count10, int count20, int count40,
                            int stake5, int stake10, int stake20, int stake40);
    
    // Encryption status of wallet changed
    void encryptionStatusChanged(int status);

    // Signal emitted when wallet needs to be unlocked
    // It is valid behaviour for listeners to keep the wallet locked after this signal;
    // this means that the unlocking failed or was cancelled.
    void requireUnlock();

    // Asynchronous message notification
    void message(const QString &title, const QString &message, bool modal, unsigned int style);
};

#endif // WALLETMODEL_H
