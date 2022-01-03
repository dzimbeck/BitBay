#ifndef COINCONTROLDIALOG_H
#define COINCONTROLDIALOG_H

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTreeWidgetItem>
#include "peg.h"

namespace Ui {
    class CoinControlDialog;
}
class WalletModel;
class CCoinControl;

class CoinControlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CoinControlDialog(QWidget *parent = 0);
    ~CoinControlDialog();

    void setModel(WalletModel *model);
    void setTxType(PegTxType);

    // static because also called from sendcoinsdialog
    static void updateLabels(WalletModel*, QDialog*, PegTxType);
    static QString getPriorityLabel(double);

    static QList<qint64> payAmounts;
    static CCoinControl *coinControl;

private:
    Ui::CoinControlDialog *ui;
    WalletModel *model;
    int sortColumn;
    Qt::SortOrder sortOrder;
    PegTxType txType;
    QPixmap pmChange;
    QPixmap pmNotaryF;
    QPixmap pmNotaryV;

    QMenu *contextMenu;
    QTreeWidgetItem *contextMenuItem;
    QAction *copyTransactionHashAction;
    //QAction *lockAction;
    //QAction *unlockAction;

    QString strPad(QString, int, QString);
    void sortView(int, Qt::SortOrder);
    void updateView();

    enum
    {
        COLUMN_CHECKBOX,
        COLUMN_AMOUNT,
        COLUMN_LIQUIDITY,
        COLUMN_RESERVE,
        COLUMN_FRACTIONS,
        COLUMN_LABEL,
        COLUMN_ADDRESS,
        COLUMN_DATE,
        COLUMN_CONFIRMATIONS,
        COLUMN_PRIORITY,
        COLUMN_TXHASH,
        COLUMN_VOUT_INDEX,
        COLUMN_AMOUNT_INT64,
        COLUMN_RESERVE_INT64,
        COLUMN_LIQUIDITY_INT64,
        COLUMN_PRIORITY_INT64
    };

private slots:
    void showMenu(const QPoint &);
    void copyTotalAmount();
    void copyReserveAmount();
    void copySpendableAmount();
    void copyLabel();
    void copyAddress();
    void copyTransactionHash();
    //void lockCoin();
    //void unlockCoin();
    void clipboardQuantity();
    void clipboardAmount();
    void clipboardFee();
    void clipboardAfterFee();
    void clipboardBytes();
    void clipboardPriority();
    void clipboardLowOutput();
    void clipboardChange();
    void radioTreeMode(bool);
    void radioListMode(bool);
    void viewItemChanged(QTreeWidgetItem*, int);
    void openFractions(QTreeWidgetItem*,int);
    void openFractionsMenu(const QPoint &);
    void headerSectionClicked(int);
    void buttonBoxClicked(QAbstractButton*);
    void buttonSelectAllClicked();
    //void updateLabelLocked();
};

#endif // COINCONTROLDIALOG_H
