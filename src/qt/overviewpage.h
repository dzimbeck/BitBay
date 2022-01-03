#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

#include <QWidget>
#include <QAbstractItemDelegate>
#include "walletmodel.h"
#include "wallet.h"
#include "bitcoinunits.h"

namespace Ui {
    class OverviewPage;
}
class ClientModel;
class WalletModel;
class TxViewDelegate;
class TransactionFilterProxy;

QT_BEGIN_NAMESPACE
class QModelIndex;
class QStyleOptionViewItem;
QT_END_NAMESPACE

/** Overview ("home") page widget */
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPage(QWidget *parent = 0);
    ~OverviewPage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);
    void showOutOfSyncWarning(bool fShow);

public slots:
    void setBalance(qint64 balance, 
                    qint64 reserves, qint64 liquidity, qint64 frozen, 
                    std::vector<CFrozenCoinInfo> frozenCoins,
                    qint64 stake, qint64 unconfirmedBalance, qint64 immatureBalance);

signals:
    void transactionClicked(const QModelIndex &index);

private:
    Ui::OverviewPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    qint64 currentBalance;
    qint64 currentReserve;
    qint64 currentLiquidity;
    qint64 currentFrozen;
    qint64 currentStake;
    qint64 currentUnconfirmedBalance;
    qint64 currentImmatureBalance;
    std::vector<CFrozenCoinInfo> currentFrozenCoins;

    TxViewDelegate *txdelegate;
    TransactionFilterProxy *filter;

private slots:
    void updateDisplayUnit();
    void handleTransactionClicked(const QModelIndex &index);
    void updateAlerts();
    void updateAlerts(const QString &warnings);
    void openFrozenCoinsInfo();
};

#define DECORATION_SIZE 64
#define NUM_ITEMS 12

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(): QAbstractItemDelegate(), unit(BitcoinUnits::BTC)
    {

    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
			   const QModelIndex &index ) const;

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        //return QSize(DECORATION_SIZE, DECORATION_SIZE);
        return QSize(DECORATION_SIZE, DECORATION_SIZE/2);
    }

    int unit;

};

#endif // OVERVIEWPAGE_H
