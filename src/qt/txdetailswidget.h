// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TXDETAILSWIDGET_H
#define TXDETAILSWIDGET_H

#include <QDialog>
#include <QPixmap>
#include <QItemDelegate>
#include <QStyledItemDelegate>
#include "bignum.h"

namespace Ui {
    class TxDetails;
}

class QTreeView;
class QTreeWidget;
class QModelIndex;
class QTreeWidgetItem;
class QwtPlot;
class QwtPlotCurve;
class BlockchainModel;
class CTransaction;
class CBlockIndex;
class CFractions;
class CPegLevel;

class TxDetailsWidget : public QWidget
{
    Q_OBJECT

    enum {
        COL_INP_N = 0,
        COL_INP_TX,
        COL_INP_ADDR,
        COL_INP_VALUE,
        COL_INP_FRACTIONS
    };
    enum {
        COL_OUT_N = 0,
        COL_OUT_TX,
        COL_OUT_ADDR,
        COL_OUT_VALUE,
        COL_OUT_FRACTIONS
    };

public:
    explicit TxDetailsWidget(QWidget *parent = nullptr);
    ~TxDetailsWidget();

public slots:
    void openTx(QTreeWidgetItem*,int);
    void openTx(uint256 blockhash, uint txidx);
    void openTx(CTransaction & tx, 
                CBlockIndex* pblockindex, 
                uint txidx, 
                int nCycle, 
                int nSupply, 
                int nSupplyN, 
                int nSupplyNN, 
                unsigned int nTime);
    void showNotFound();

signals:
    void openAddressBalance(QString address);
    
private slots:
    void openPegVotes(QTreeWidgetItem*,int);
    void openFractions(QTreeWidgetItem*,int);
    void openFractionsMenu(const QPoint &);
    void openTxMenu(const QPoint &);
    void openInpMenu(const QPoint &);
    void openOutMenu(const QPoint &);
    void plotFractions(QTreeWidget *, 
                       const CFractions &, 
                       const CPegLevel &,
                       int64_t nLiquidSave = -1,
                       int64_t nReserveSave = -1,
                       int64_t nID = 0);
    void copyPegData(QTreeWidget *);

private:
    Ui::TxDetails *ui;
    QwtPlot * fplot;
    QwtPlotCurve * curvePeg;
    QwtPlotCurve * curveLiquid;
    QwtPlotCurve * curveReserve;
    QPixmap pmChange;
    QPixmap pmNotaryF;
    QPixmap pmNotaryV;
};

class TxDetailsWidgetTxEvents : public QObject
{
    Q_OBJECT
    QTreeWidget* treeWidget;
public:
    TxDetailsWidgetTxEvents(QTreeWidget* w, QObject* parent)
        :QObject(parent), treeWidget(w) {}
    ~TxDetailsWidgetTxEvents() override {}
protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};

class FractionsDialogEvents : public QObject
{
    Q_OBJECT
    QTreeWidget* treeWidget;
public:
    FractionsDialogEvents(QTreeWidget* w, QObject* parent)
        :QObject(parent), treeWidget(w) {}
    ~FractionsDialogEvents() override {}
protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};

#endif // TXDETAILSWIDGET_H
