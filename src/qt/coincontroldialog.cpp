#include "coincontroldialog.h"
#include "ui_coincontroldialog.h"
#include "ui_fractionsdialog.h"

#include "init.h"
#include "base58.h"
#include "bitcoinunits.h"
#include "walletmodel.h"
#include "addresstablemodel.h"
#include "optionsmodel.h"
#include "coincontrol.h"
#include "guiutil.h"

#include "metatypes.h" // for fractions delegate
#include "blockchainpage.h" // for fractions delegate
#include "blockchainmodel.h" // for fractions delegate
#include "itemdelegates.h" // for fractions delegate

#include "qwt/qwt_plot.h"
#include "qwt/qwt_plot_item.h"
#include "qwt/qwt_plot_curve.h"
#include "qwt/qwt_plot_barchart.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QCursor>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFlags>
#include <QIcon>
#include <QPen>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>

using namespace std;
QList<qint64> CoinControlDialog::payAmounts;
CCoinControl* CoinControlDialog::coinControl = new CCoinControl();

CoinControlDialog::CoinControlDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CoinControlDialog),
    model(0)
{
    ui->setupUi(this);
    GUIUtil::SetBitBayFonts(this);

    QFont font = GUIUtil::bitcoinAddressFont();
    qreal pt = font.pointSizeF()*0.8;
    if (pt != .0) {
        font.setPointSizeF(pt);
    } else {
        int px = font.pixelSize()*8/10;
        font.setPixelSize(px);
    }
    ui->treeWidget->setFont(font);

    font = ui->treeWidget->header()->font();
    pt = font.pointSizeF()*0.9;
    if (pt != .0) {
        font.setPointSizeF(pt);
    } else {
        int px = font.pixelSize()*9/10;
        font.setPixelSize(px);
    }
    font.setBold(true);
    ui->treeWidget->header()->setFont(font);

    // context menu actions
    QAction *copyAddressAction = new QAction(tr("Copy address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy label"), this);
    QAction *copyTotalAmountAction = new QAction(tr("Copy total amount"), this);
    QAction *copyReserveAmountAction = new QAction(tr("Copy reserve amount"), this);
    QAction *copySpendableAmountAction = new QAction(tr("Copy spendable amount"), this);
    copyTransactionHashAction = new QAction(tr("Copy transaction ID"), this);  // we need to enable/disable this
    //lockAction = new QAction(tr("Lock unspent"), this);                        // we need to enable/disable this
    //unlockAction = new QAction(tr("Unlock unspent"), this);                    // we need to enable/disable this

    // context menu
    contextMenu = new QMenu();
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copySpendableAmountAction);
    contextMenu->addAction(copyReserveAmountAction);
    contextMenu->addAction(copyTotalAmountAction);
    contextMenu->addAction(copyTransactionHashAction);
    //contextMenu->addSeparator();
    //contextMenu->addAction(lockAction);
    //contextMenu->addAction(unlockAction);

    // context menu signals
    connect(ui->treeWidget, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showMenu(QPoint)));
    connect(ui->treeWidget, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)), this, SLOT(openFractions(QTreeWidgetItem*,int)));
    
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyTotalAmountAction, SIGNAL(triggered()), this, SLOT(copyTotalAmount()));
    connect(copyReserveAmountAction, SIGNAL(triggered()), this, SLOT(copyReserveAmount()));
    connect(copySpendableAmountAction, SIGNAL(triggered()), this, SLOT(copySpendableAmount()));
    connect(copyTransactionHashAction, SIGNAL(triggered()), this, SLOT(copyTransactionHash()));
    //connect(lockAction, SIGNAL(triggered()), this, SLOT(lockCoin()));
    //connect(unlockAction, SIGNAL(triggered()), this, SLOT(unlockCoin()));

    // clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardPriorityAction = new QAction(tr("Copy priority"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy low output"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);

    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(clipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(clipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(clipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(clipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(clipboardBytes()));
    connect(clipboardPriorityAction, SIGNAL(triggered()), this, SLOT(clipboardPriority()));
    connect(clipboardLowOutputAction, SIGNAL(triggered()), this, SLOT(clipboardLowOutput()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(clipboardChange()));

    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);
    ui->labelCoinControlFee->addAction(clipboardFeeAction);
    ui->labelCoinControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelCoinControlBytes->addAction(clipboardBytesAction);
    ui->labelCoinControlPriority->addAction(clipboardPriorityAction);
    ui->labelCoinControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelCoinControlChange->addAction(clipboardChangeAction);

    // toggle tree/list mode
    connect(ui->radioTreeMode, SIGNAL(toggled(bool)), this, SLOT(radioTreeMode(bool)));
    connect(ui->radioListMode, SIGNAL(toggled(bool)), this, SLOT(radioListMode(bool)));

    // click on checkbox
    connect(ui->treeWidget, SIGNAL(itemChanged( QTreeWidgetItem*, int)), this, SLOT(viewItemChanged( QTreeWidgetItem*, int)));

    // click on header
    ui->treeWidget->header()->setClickable(true);
    connect(ui->treeWidget->header(), SIGNAL(sectionClicked(int)), this, SLOT(headerSectionClicked(int)));

    // ok button
    connect(ui->buttonBox, SIGNAL(clicked( QAbstractButton*)), this, SLOT(buttonBoxClicked(QAbstractButton*)));

    // (un)select all
    connect(ui->pushButtonSelectAll, SIGNAL(clicked()), this, SLOT(buttonSelectAllClicked()));

    ui->treeWidget->setColumnWidth(COLUMN_CHECKBOX, 84);
    ui->treeWidget->setColumnWidth(COLUMN_AMOUNT, 180);
    ui->treeWidget->setColumnWidth(COLUMN_LIQUIDITY, 180);
    ui->treeWidget->setColumnWidth(COLUMN_RESERVE, 180);
    ui->treeWidget->setColumnWidth(COLUMN_FRACTIONS, 80);
    ui->treeWidget->setColumnWidth(COLUMN_LABEL, 170);
    ui->treeWidget->setColumnWidth(COLUMN_ADDRESS, 290);
    ui->treeWidget->setColumnWidth(COLUMN_DATE, 130);
    ui->treeWidget->setColumnWidth(COLUMN_CONFIRMATIONS, 100);
    ui->treeWidget->setColumnWidth(COLUMN_PRIORITY, 100);
    ui->treeWidget->setColumnHidden(COLUMN_TXHASH, true);         // store transacton hash in this column, but dont show it
    ui->treeWidget->setColumnHidden(COLUMN_VOUT_INDEX, true);     // store vout index in this column, but dont show it
    ui->treeWidget->setColumnHidden(COLUMN_AMOUNT, true);         // dont show it, as there are liquidity and reserves
    ui->treeWidget->setColumnHidden(COLUMN_AMOUNT_INT64, true);   // store amount int64_t in this column, but dont show it
    ui->treeWidget->setColumnHidden(COLUMN_RESERVE_INT64, true);  // store reserve int64_t in this column, but dont show it
    ui->treeWidget->setColumnHidden(COLUMN_LIQUIDITY_INT64, true);// store liquidity int64_t in this column, but dont show it
    ui->treeWidget->setColumnHidden(COLUMN_PRIORITY_INT64, true); // store priority int64_t in this column, but dont show it

    // default view is sorted by amount desc
    sortView(COLUMN_LIQUIDITY_INT64, Qt::DescendingOrder);
    
    // fractions views
    auto txOutFractionsDelegate = new FractionsItemDelegate(ui->treeWidget);
    ui->treeWidget->setItemDelegateForColumn(COLUMN_FRACTIONS, txOutFractionsDelegate);
    auto reserveDelegate = new  LeftSideIconItemDelegate(ui->treeWidget);
    ui->treeWidget->setItemDelegateForColumn(COLUMN_RESERVE, reserveDelegate);
    auto liquidityDelegate = new  LeftSideIconItemDelegate(ui->treeWidget);
    ui->treeWidget->setItemDelegateForColumn(COLUMN_LIQUIDITY, liquidityDelegate);
    
    pmChange = QPixmap(":/icons/change");
    pmChange = pmChange.scaled(32,32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    pmNotaryF = QPixmap(":/icons/frostr");
    pmNotaryF = pmNotaryF.scaled(32,32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    pmNotaryV = QPixmap(":/icons/frostl");
    pmNotaryV = pmNotaryV.scaled(32,32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

CoinControlDialog::~CoinControlDialog()
{
    delete ui;
}

void CoinControlDialog::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel() && model->getAddressTableModel())
    {
        updateView();
        //updateLabelLocked();
        CoinControlDialog::updateLabels(model, this, txType);
    }
}

void CoinControlDialog::setTxType(PegTxType txType)
{
    this->txType = txType;
}

// helper function str_pad
QString CoinControlDialog::strPad(QString s, int nPadLength, QString sPadding)
{
    while (s.length() < nPadLength)
        s = sPadding + s;

    return s;
}

// ok button
void CoinControlDialog::buttonBoxClicked(QAbstractButton* button)
{
    if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole)
        done(QDialog::Accepted); // closes the dialog
}

// (un)select all
void CoinControlDialog::buttonSelectAllClicked()
{
    Qt::CheckState state = Qt::Checked;
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
    {
        if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) != Qt::Unchecked)
        {
            state = Qt::Unchecked;
            break;
        }
    }
    ui->treeWidget->setEnabled(false);
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
            if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) != state)
                ui->treeWidget->topLevelItem(i)->setCheckState(COLUMN_CHECKBOX, state);
    ui->treeWidget->setEnabled(true);
    CoinControlDialog::updateLabels(model, this, txType);
}

// context menu
void CoinControlDialog::showMenu(const QPoint &point)
{
    QTreeWidgetItem *item = ui->treeWidget->itemAt(point);
    if(item)
    {
        contextMenuItem = item;

        // disable some items (like Copy Transaction ID, lock, unlock) for tree roots in context menu
        if (item->text(COLUMN_TXHASH).length() == 64) // transaction hash is 64 characters (this means its a child node, so its not a parent node in tree mode)
        {
            copyTransactionHashAction->setEnabled(true);
            //if (model->isLockedCoin(uint256(item->text(COLUMN_TXHASH).toStdString()), item->text(COLUMN_VOUT_INDEX).toUInt()))
            //{
            //    lockAction->setEnabled(false);
            //    unlockAction->setEnabled(true);
            //}
            //else
            //{
            //    lockAction->setEnabled(true);
            //    unlockAction->setEnabled(false);
            //}
        }
        else // this means click on parent node in tree mode -> disable all
        {
            copyTransactionHashAction->setEnabled(false);
            //lockAction->setEnabled(false);
            //unlockAction->setEnabled(false);
        }

        // show context menu
        contextMenu->exec(QCursor::pos());
    }
}

// context menu action: copy total amount
void CoinControlDialog::copyTotalAmount()
{
    QApplication::clipboard()->setText(contextMenuItem->text(COLUMN_AMOUNT));
}

// context menu action: copy reserve amount
void CoinControlDialog::copyReserveAmount()
{
    QApplication::clipboard()->setText(contextMenuItem->text(COLUMN_RESERVE));
}

// context menu action: copy spendable amount
void CoinControlDialog::copySpendableAmount()
{
    QApplication::clipboard()->setText(contextMenuItem->text(COLUMN_LIQUIDITY));
}

// context menu action: copy label
void CoinControlDialog::copyLabel()
{
    if (ui->radioTreeMode->isChecked() && contextMenuItem->text(COLUMN_LABEL).length() == 0 && contextMenuItem->parent())
        QApplication::clipboard()->setText(contextMenuItem->parent()->text(COLUMN_LABEL));
    else
        QApplication::clipboard()->setText(contextMenuItem->text(COLUMN_LABEL));
}

// context menu action: copy address
void CoinControlDialog::copyAddress()
{
    if (ui->radioTreeMode->isChecked() && contextMenuItem->text(COLUMN_ADDRESS).length() == 0 && contextMenuItem->parent())
        QApplication::clipboard()->setText(contextMenuItem->parent()->text(COLUMN_ADDRESS));
    else
        QApplication::clipboard()->setText(contextMenuItem->text(COLUMN_ADDRESS));
}

// context menu action: copy transaction id
void CoinControlDialog::copyTransactionHash()
{
    QApplication::clipboard()->setText(contextMenuItem->text(COLUMN_TXHASH));
}

// context menu action: lock coin
/*void CoinControlDialog::lockCoin()
{
    if (contextMenuItem->checkState(COLUMN_CHECKBOX) == Qt::Checked)
        contextMenuItem->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

    COutPoint outpt(uint256(contextMenuItem->text(COLUMN_TXHASH).toStdString()), contextMenuItem->text(COLUMN_VOUT_INDEX).toUInt());
    model->lockCoin(outpt);
    contextMenuItem->setDisabled(true);
    contextMenuItem->setIcon(COLUMN_CHECKBOX, QIcon(":/icons/lock_closed"));
    updateLabelLocked();
}*/

// context menu action: unlock coin
/*void CoinControlDialog::unlockCoin()
{
    COutPoint outpt(uint256(contextMenuItem->text(COLUMN_TXHASH).toStdString()), contextMenuItem->text(COLUMN_VOUT_INDEX).toUInt());
    model->unlockCoin(outpt);
    contextMenuItem->setDisabled(false);
    contextMenuItem->setIcon(COLUMN_CHECKBOX, QIcon());
    updateLabelLocked();
}*/

// copy label "Quantity" to clipboard
void CoinControlDialog::clipboardQuantity()
{
    QApplication::clipboard()->setText(ui->labelCoinControlQuantity->text());
}

// copy label "Amount" to clipboard
void CoinControlDialog::clipboardAmount()
{
    QApplication::clipboard()->setText(ui->labelCoinControlAmount->text().left(ui->labelCoinControlAmount->text().indexOf(" ")));
}

// copy label "Fee" to clipboard
void CoinControlDialog::clipboardFee()
{
    QApplication::clipboard()->setText(ui->labelCoinControlFee->text().left(ui->labelCoinControlFee->text().indexOf(" ")));
}

// copy label "After fee" to clipboard
void CoinControlDialog::clipboardAfterFee()
{
    QApplication::clipboard()->setText(ui->labelCoinControlAfterFee->text().left(ui->labelCoinControlAfterFee->text().indexOf(" ")));
}

// copy label "Bytes" to clipboard
void CoinControlDialog::clipboardBytes()
{
    QApplication::clipboard()->setText(ui->labelCoinControlBytes->text());
}

// copy label "Priority" to clipboard
void CoinControlDialog::clipboardPriority()
{
    QApplication::clipboard()->setText(ui->labelCoinControlPriority->text());
}

// copy label "Low output" to clipboard
void CoinControlDialog::clipboardLowOutput()
{
    QApplication::clipboard()->setText(ui->labelCoinControlLowOutput->text());
}

// copy label "Change" to clipboard
void CoinControlDialog::clipboardChange()
{
    QApplication::clipboard()->setText(ui->labelCoinControlChange->text().left(ui->labelCoinControlChange->text().indexOf(" ")));
}

// treeview: sort
void CoinControlDialog::sortView(int column, Qt::SortOrder order)
{
    sortColumn = column;
    sortOrder = order;
    ui->treeWidget->sortItems(column, order);
    
    int columnIndicator = COLUMN_LIQUIDITY_INT64;
    if (sortColumn == COLUMN_AMOUNT_INT64) {
        columnIndicator = COLUMN_AMOUNT;
    }
    else if (sortColumn == COLUMN_RESERVE_INT64) {
        columnIndicator = COLUMN_RESERVE;
    }    
    else if (sortColumn == COLUMN_LIQUIDITY_INT64) {
        columnIndicator = COLUMN_LIQUIDITY;
    }    
    else if (sortColumn == COLUMN_PRIORITY_INT64) {
        columnIndicator = COLUMN_PRIORITY;
    }
    else {
        columnIndicator = sortColumn;
    }
    ui->treeWidget->header()->setSortIndicator(columnIndicator, sortOrder);
}

// treeview: clicked on header
void CoinControlDialog::headerSectionClicked(int logicalIndex)
{
    if (logicalIndex == COLUMN_CHECKBOX) // click on most left column -> do nothing
    {
        int columnIndicator = COLUMN_LIQUIDITY_INT64;
        if (sortColumn == COLUMN_AMOUNT_INT64) {
            columnIndicator = COLUMN_AMOUNT;
        }
        else if (sortColumn == COLUMN_RESERVE_INT64) {
            columnIndicator = COLUMN_RESERVE;
        }    
        else if (sortColumn == COLUMN_LIQUIDITY_INT64) {
            columnIndicator = COLUMN_LIQUIDITY;
        }    
        else if (sortColumn == COLUMN_PRIORITY_INT64) {
            columnIndicator = COLUMN_PRIORITY;
        }
        else {
            columnIndicator = sortColumn;
        }
        ui->treeWidget->header()->setSortIndicator(columnIndicator, sortOrder);
    }
    else
    {
        if (logicalIndex == COLUMN_AMOUNT) // sort by amount
            logicalIndex = COLUMN_AMOUNT_INT64;

        if (logicalIndex == COLUMN_RESERVE) // sort by reserve
            logicalIndex = COLUMN_RESERVE_INT64;

        if (logicalIndex == COLUMN_LIQUIDITY) // sort by liquidity
            logicalIndex = COLUMN_LIQUIDITY_INT64;
        
        if (logicalIndex == COLUMN_PRIORITY) // sort by priority
            logicalIndex = COLUMN_PRIORITY_INT64;

        if (sortColumn == logicalIndex)
            sortOrder = ((sortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder);
        else
        {
            sortColumn = logicalIndex;
            sortOrder = ((sortColumn == COLUMN_AMOUNT_INT64 || 
                          sortColumn == COLUMN_RESERVE_INT64 || 
                          sortColumn == COLUMN_LIQUIDITY_INT64 || 
                          sortColumn == COLUMN_PRIORITY_INT64 || 
                          sortColumn == COLUMN_DATE || 
                          sortColumn == COLUMN_CONFIRMATIONS) 
                         ? Qt::DescendingOrder 
                         : Qt::AscendingOrder); // if amount,date,conf,priority then default => desc, else default => asc
        }

        sortView(sortColumn, sortOrder);
    }
}

// toggle tree mode
void CoinControlDialog::radioTreeMode(bool checked)
{
    if (checked && model)
        updateView();
}

// toggle list mode
void CoinControlDialog::radioListMode(bool checked)
{
    if (checked && model)
        updateView();
}

// checkbox clicked by user
void CoinControlDialog::viewItemChanged(QTreeWidgetItem* item, int column)
{
    if (column == COLUMN_CHECKBOX && item->text(COLUMN_TXHASH).length() == 64) // transaction hash is 64 characters (this means its a child node, so its not a parent node in tree mode)
    {
        COutPoint outpt(uint256(item->text(COLUMN_TXHASH).toStdString()), item->text(COLUMN_VOUT_INDEX).toUInt());

        if (item->checkState(COLUMN_CHECKBOX) == Qt::Unchecked)
            coinControl->UnSelect(outpt);
        else if (item->isDisabled()) // locked (this happens if "check all" through parent node)
            item->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
        else
            coinControl->Select(outpt);

        // selection changed -> update labels
        if (ui->treeWidget->isEnabled()) // do not update on every click for (un)select all
            CoinControlDialog::updateLabels(model, this, txType);
    }
}

// helper function, return human readable label for priority number
QString CoinControlDialog::getPriorityLabel(double dPriority)
{
    if (dPriority > 576000ULL) // at least medium, this number is from AllowFree(), the other thresholds are kinda random
    {
        if      (dPriority > 5760000000ULL)   return tr("highest");
        else if (dPriority > 576000000ULL)    return tr("high");
        else if (dPriority > 57600000ULL)     return tr("medium-high");
        else                                    return tr("medium");
    }
    else
    {
        if      (dPriority > 5760ULL) return tr("low-medium");
        else if (dPriority > 58ULL)   return tr("low");
        else                            return tr("lowest");
    }
}

// shows count of locked unspent outputs
/*void CoinControlDialog::updateLabelLocked()
{
    vector<COutPoint> vOutpts;
    model->listLockedCoins(vOutpts);
    if (vOutpts.size() > 0)
    {
       ui->labelLocked->setText(tr("(%1 locked)").arg(vOutpts.size()));
       ui->labelLocked->setVisible(true);
    }
    else ui->labelLocked->setVisible(false);
}*/

void CoinControlDialog::updateLabels(WalletModel *model, QDialog* dialog, PegTxType txType)
{
    if (!model) return;

    // nPayAmount
    qint64 nPayAmount = 0;
    bool fLowOutput = false;
    bool fDust = false;
    CTransaction txDummy;
    foreach(const qint64 &amount, CoinControlDialog::payAmounts)
    {
        nPayAmount += amount;

        if (amount > 0)
        {
            if (amount < CENT)
                fLowOutput = true;

            CTxOut txout(amount, (CScript)vector<unsigned char>(24, 0));
            txDummy.vout.push_back(txout);
        }
    }

    QString sPriorityLabel      = "";
    int64_t nAmount             = 0;
    int64_t nPayFee             = 0;
    int64_t nAfterFee           = 0;
    int64_t nChange             = 0;
    unsigned int nBytes         = 0;
    unsigned int nBytesInputs   = 0;
    double dPriority            = 0;
    double dPriorityInputs      = 0;
    unsigned int nQuantity      = 0;

    vector<COutPoint> vCoinControl;
    vector<COutput>   vOutputs;
    coinControl->ListSelected(vCoinControl);
    model->getOutputs(vCoinControl, vOutputs);

    for(const COutput& out : vOutputs)
    {
        // Quantity
        nQuantity++;

        // Amount
        int64_t nValue = 0;
        if (txType == PEG_MAKETX_SEND_RESERVE ||
            txType == PEG_MAKETX_FREEZE_RESERVE) {
            nValue = out.tx->vOutFractions[out.i].Ref().Low(model->getPegSupplyIndex());
        } else {
            nValue = out.tx->vOutFractions[out.i].Ref().High(model->getPegSupplyIndex());
        }
        nAmount += nValue;

        // Priority
        dPriorityInputs += (double)nValue * (out.nDepth+1);

        // Bytes
        CTxDestination address;
        if(ExtractDestination(out.tx->vout[out.i].scriptPubKey, address))
        {
            CPubKey pubkey;
            CKeyID *keyid = boost::get< CKeyID >(&address);
            if (keyid && model->getPubKey(*keyid, pubkey))
                //for tx with peg, multiply by 2 to have output to itself
                nBytesInputs += (pubkey.IsCompressed() ? 148 : 180) * 2;
            else
                nBytesInputs += 148; // in all error cases, simply assume 148 here
        }
        else nBytesInputs += 148;
    }

    // calculation
    if (nQuantity > 0)
    {
        // Bytes
        nBytes = nBytesInputs + ((CoinControlDialog::payAmounts.size() > 0 ? CoinControlDialog::payAmounts.size() + 1 : 2) * 34) + 10; // always assume +1 output for change here

        // Priority
        dPriority = dPriorityInputs / nBytes;
        sPriorityLabel = CoinControlDialog::getPriorityLabel(dPriority);

        // Fee
        int64_t nFee = nTransactionFee * (1 + (int64_t)nBytes / 1000);

        // Min Fee
        int64_t nMinFee = GetMinFee(txDummy, 
                                    nPegStartHeight /*gui: enforce peg fees*/, 
                                    1, GMF_SEND, nBytes);

        nPayFee = max(nFee, nMinFee);

        if (nPayAmount > 0)
        {
            nChange = nAmount - nPayFee - nPayAmount;

            if (nChange == 0)
                nBytes -= 34;
        }

        // after fee
        nAfterFee = nAmount - nPayFee;
        if (nAfterFee < 0)
            nAfterFee = 0;
    }

    // actually update labels
    int nDisplayUnit = BitcoinUnits::BTC;
    if (model && model->getOptionsModel())
        nDisplayUnit = model->getOptionsModel()->getDisplayUnit();

    QLabel *l1 = dialog->findChild<QLabel *>("labelCoinControlQuantity");
    QLabel *l2 = dialog->findChild<QLabel *>("labelCoinControlAmount");
    QLabel *l3 = dialog->findChild<QLabel *>("labelCoinControlFee");
    QLabel *l4 = dialog->findChild<QLabel *>("labelCoinControlAfterFee");
    QLabel *l5 = dialog->findChild<QLabel *>("labelCoinControlBytes");
    QLabel *l6 = dialog->findChild<QLabel *>("labelCoinControlPriority");
    QLabel *l7 = dialog->findChild<QLabel *>("labelCoinControlLowOutput");
    QLabel *l8 = dialog->findChild<QLabel *>("labelCoinControlChange");

    // enable/disable "low output" and "change"
    dialog->findChild<QLabel *>("labelCoinControlLowOutputText")->setEnabled(nPayAmount > 0);
    dialog->findChild<QLabel *>("labelCoinControlLowOutput")    ->setEnabled(nPayAmount > 0);
    dialog->findChild<QLabel *>("labelCoinControlChangeText")   ->setEnabled(nPayAmount > 0);
    dialog->findChild<QLabel *>("labelCoinControlChange")       ->setEnabled(nPayAmount > 0);

    // stats
    l1->setText(QString::number(nQuantity));                                 // Quantity
    l2->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, nAmount));        // Amount
    l3->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, nPayFee));        // Fee
    l4->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, nAfterFee));      // After Fee
    l5->setText(((nBytes > 0) ? "~" : "") + QString::number(nBytes));                                    // Bytes
    l6->setText(sPriorityLabel);                                             // Priority
    l7->setText((fLowOutput ? (fDust ? tr("DUST") : tr("yes")) : tr("no"))); // Low Output / Dust
    l8->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, nChange));        // Change

    // turn labels "red"
    l5->setStyleSheet((nBytes >= 10000) ? "color:red;" : "");               // Bytes >= 10000
    l6->setStyleSheet((dPriority <= 576000) ? "color:red;" : "");         // Priority < "medium"
    l7->setStyleSheet((fLowOutput) ? "color:red;" : "");                    // Low Output = "yes"
    l8->setStyleSheet((nChange > 0 && nChange < CENT) ? "color:red;" : ""); // Change < 0.01BTC

    // tool tips
    l5->setToolTip(tr("This label turns red, if the transaction size is bigger than 10000 bytes.\n\n This means a fee of at least %1 per kb is required.\n\n Can vary +/- 1 Byte per input.").arg(BitcoinUnits::formatWithUnit(nDisplayUnit, CENT)));
    l6->setToolTip(tr("Transactions with higher priority get more likely into a block.\n\nThis label turns red, if the priority is smaller than \"medium\".\n\n This means a fee of at least %1 per kb is required.").arg(BitcoinUnits::formatWithUnit(nDisplayUnit, CENT)));
    l7->setToolTip(tr("This label turns red, if any recipient receives an amount smaller than %1.\n\n This means a fee of at least %2 is required. \n\n Amounts below 0.546 times the minimum relay fee are shown as DUST.").arg(BitcoinUnits::formatWithUnit(nDisplayUnit, CENT)).arg(BitcoinUnits::formatWithUnit(nDisplayUnit, CENT)));
    l8->setToolTip(tr("This label turns red, if the change is smaller than %1.\n\n This means a fee of at least %2 is required.").arg(BitcoinUnits::formatWithUnit(nDisplayUnit, CENT)).arg(BitcoinUnits::formatWithUnit(nDisplayUnit, CENT)));
    dialog->findChild<QLabel *>("labelCoinControlBytesText")    ->setToolTip(l5->toolTip());
    dialog->findChild<QLabel *>("labelCoinControlPriorityText") ->setToolTip(l6->toolTip());
    dialog->findChild<QLabel *>("labelCoinControlLowOutputText")->setToolTip(l7->toolTip());
    dialog->findChild<QLabel *>("labelCoinControlChangeText")   ->setToolTip(l8->toolTip());

    // Insufficient funds
    QLabel *label = dialog->findChild<QLabel *>("labelCoinControlInsuffFunds");
    if (label)
        label->setVisible(nChange < 0);
}

void CoinControlDialog::updateView()
{
    bool treeMode = ui->radioTreeMode->isChecked();

    ui->treeWidget->clear();
    ui->treeWidget->setEnabled(false); // performance, otherwise updateLabels would be called for every checked checkbox
    QFlags<Qt::ItemFlag> flgCheckbox=Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    QFlags<Qt::ItemFlag> flgTristate=Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsTristate;

    int nDisplayUnit = BitcoinUnits::BTC;
    if (model && model->getOptionsModel())
        nDisplayUnit = model->getOptionsModel()->getDisplayUnit();

    map<QString, vector<COutput> > mapCoins;
    model->listCoins(mapCoins);

    for(const pair<QString, vector<COutput>>& coins : mapCoins)
    {
        QTreeWidgetItem *itemWalletAddress = new QTreeWidgetItem();
        QString sWalletAddress = coins.first;
        QString sWalletLabel = "";
        if (model->getAddressTableModel())
            sWalletLabel = model->getAddressTableModel()->labelForAddress(sWalletAddress);
        if (sWalletLabel.length() == 0)
            sWalletLabel = tr("(no label)");

        if (treeMode)
        {
            // wallet address
            ui->treeWidget->addTopLevelItem(itemWalletAddress);

            itemWalletAddress->setFlags(flgTristate);
            itemWalletAddress->setCheckState(COLUMN_CHECKBOX,Qt::Unchecked);

            for (int i = 0; i < ui->treeWidget->columnCount(); i++)
                itemWalletAddress->setBackground(i, QColor(248, 247, 246));

            // label
            itemWalletAddress->setText(COLUMN_LABEL, sWalletLabel);

            // address
            itemWalletAddress->setText(COLUMN_ADDRESS, sWalletAddress);
        }

        int64_t nSum = 0;
        int64_t nSumReserve = 0;
        int64_t nSumLiquidity = 0;
        CFractions sumFractions(0, CFractions::STD);
        double dPrioritySum = 0;
        int nChildren = 0;
        int nInputSum = 0;
        for(const COutput& out : coins.second)
        {
            int nInputSize = 148; // 180 if uncompressed public key
            nSum += out.tx->vout[out.i].nValue;
            nChildren++;

            QTreeWidgetItem *itemOutput;
            if (treeMode)    itemOutput = new QTreeWidgetItem(itemWalletAddress);
            else             itemOutput = new QTreeWidgetItem(ui->treeWidget);
            itemOutput->setFlags(flgCheckbox);
            itemOutput->setCheckState(COLUMN_CHECKBOX,Qt::Unchecked);

            // address
            CTxDestination outputAddress;
            QString sAddress = "";
            if(ExtractDestination(out.tx->vout[out.i].scriptPubKey, outputAddress))
            {
                sAddress = CBitcoinAddress(outputAddress).ToString().c_str();

                // if listMode or change => show bitcoin address. In tree mode, address is not shown again for direct wallet address outputs
                if (!treeMode || (!(sAddress == sWalletAddress)))
                    itemOutput->setText(COLUMN_ADDRESS, sAddress);

                CPubKey pubkey;
                CKeyID *keyid = boost::get< CKeyID >(&outputAddress);
                if (keyid && model->getPubKey(*keyid, pubkey) && !pubkey.IsCompressed())
                    nInputSize = 180;
            }

            // label
            if (!(sAddress == sWalletAddress)) // change
            {
                // tooltip from where the change comes from
                itemOutput->setToolTip(COLUMN_LABEL, tr("change from %1 (%2)").arg(sWalletLabel).arg(sWalletAddress));
                itemOutput->setText(COLUMN_LABEL, tr("(change)"));
            }
            else if (!treeMode)
            {
                QString sLabel = "";
                if (model->getAddressTableModel())
                    sLabel = model->getAddressTableModel()->labelForAddress(sAddress);
                if (sLabel.length() == 0)
                    sLabel = tr("(no label)");
                itemOutput->setText(COLUMN_LABEL, sLabel);
            }

            // amount
            sumFractions += out.tx->vOutFractions[out.i].Ref();
            int64_t nReserve = out.tx->vOutFractions[out.i].Ref().Low(model->getPegSupplyIndex());
            int64_t nLiquidity = out.tx->vOutFractions[out.i].Ref().High(model->getPegSupplyIndex());
            uint32_t nFlags = out.tx->vOutFractions[out.i].nFlags();
            nSumReserve += nReserve;
            nSumLiquidity += nLiquidity;
            
            QVariant vfractions;
            vfractions.setValue(CFractions(out.tx->vOutFractions[out.i].Ref()));
            itemOutput->setData(COLUMN_FRACTIONS, BlockchainModel::FractionsRole, vfractions);
            itemOutput->setData(COLUMN_FRACTIONS, BlockchainModel::PegSupplyRole, model->getPegSupplyIndex());
            itemOutput->setData(COLUMN_FRACTIONS, BlockchainModel::PegSupplyNRole, model->getPegSupplyNIndex());
            itemOutput->setData(COLUMN_FRACTIONS, BlockchainModel::PegSupplyNNRole, model->getPegSupplyNNIndex());
            itemOutput->setData(COLUMN_AMOUNT, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
            itemOutput->setText(COLUMN_AMOUNT, BitcoinUnits::formatR(nDisplayUnit, out.tx->vout[out.i].nValue));
            itemOutput->setData(COLUMN_RESERVE, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
            itemOutput->setText(COLUMN_RESERVE, BitcoinUnits::formatR(nDisplayUnit, nReserve));
            itemOutput->setData(COLUMN_LIQUIDITY, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
            itemOutput->setText(COLUMN_LIQUIDITY, BitcoinUnits::formatR(nDisplayUnit, nLiquidity));
            itemOutput->setText(COLUMN_AMOUNT_INT64, strPad(QString::number(out.tx->vout[out.i].nValue), 15, " ")); // padding so that sorting works correctly
            itemOutput->setText(COLUMN_RESERVE_INT64, strPad(QString::number(nReserve), 15, " ")); // padding so that sorting works correctly
            itemOutput->setText(COLUMN_LIQUIDITY_INT64, strPad(QString::number(nLiquidity), 15, " ")); // padding so that sorting works correctly
            
            if (txType == PEG_MAKETX_SEND_LIQUIDITY || 
                txType == PEG_MAKETX_FREEZE_LIQUIDITY) {
                itemOutput->setData(COLUMN_RESERVE, Qt::ForegroundRole, QColor(180,180,180));
            }
            else if (txType == PEG_MAKETX_SEND_RESERVE || 
                     txType == PEG_MAKETX_FREEZE_RESERVE) {
                itemOutput->setData(COLUMN_LIQUIDITY, Qt::ForegroundRole, QColor(180,180,180));
            }
            
            if (nFlags & CFractions::NOTARY_F) {
                itemOutput->setData(COLUMN_RESERVE, Qt::DecorationPropertyRole, pmNotaryF);
                itemOutput->setData(COLUMN_LIQUIDITY, Qt::DecorationPropertyRole, pmNotaryF);
            }
            else if (nFlags & CFractions::NOTARY_V) {
                itemOutput->setData(COLUMN_RESERVE, Qt::DecorationPropertyRole, pmNotaryV);
                itemOutput->setData(COLUMN_LIQUIDITY, Qt::DecorationPropertyRole, pmNotaryV);
            }
            
            // date
            itemOutput->setText(COLUMN_DATE, QDateTime::fromTime_t(out.tx->GetTxTime()).toUTC().toString("yy-MM-dd hh:mm"));

            // confirmations
            itemOutput->setText(COLUMN_CONFIRMATIONS, strPad(QString::number(out.nDepth), 8, " "));

            // priority
            double dPriority = ((double)out.tx->vout[out.i].nValue  / (nInputSize + 78)) * (out.nDepth+1); // 78 = 2 * 34 + 10
            itemOutput->setText(COLUMN_PRIORITY, CoinControlDialog::getPriorityLabel(dPriority));
            itemOutput->setText(COLUMN_PRIORITY_INT64, strPad(QString::number((int64_t)dPriority), 20, " "));
            dPrioritySum += (double)out.tx->vout[out.i].nValue  * (out.nDepth+1);
            nInputSum    += nInputSize;

            // transaction hash
            uint256 txhash = out.tx->GetHash();
            itemOutput->setText(COLUMN_TXHASH, txhash.GetHex().c_str());

            // vout index
            itemOutput->setText(COLUMN_VOUT_INDEX, QString::number(out.i));

            // disable locked coins
            /*if (model->isLockedCoin(txhash, out.i))
            {
                COutPoint outpt(txhash, out.i);
                coinControl->UnSelect(outpt); // just to be sure
                itemOutput->setDisabled(true);
                itemOutput->setIcon(COLUMN_CHECKBOX, QIcon(":/icons/lock_closed"));
            }*/

            // set checkbox
            if (coinControl->IsSelected(txhash, out.i))
                itemOutput->setCheckState(COLUMN_CHECKBOX,Qt::Checked);
        }

        // amount
        if (treeMode)
        {
            QVariant vfractions;
            vfractions.setValue(sumFractions);
            itemWalletAddress->setData(COLUMN_FRACTIONS, BlockchainModel::FractionsRole, vfractions);
            itemWalletAddress->setData(COLUMN_FRACTIONS, BlockchainModel::PegSupplyRole, model->getPegSupplyIndex());
            itemWalletAddress->setData(COLUMN_FRACTIONS, BlockchainModel::PegSupplyNRole, model->getPegSupplyNIndex());
            itemWalletAddress->setData(COLUMN_FRACTIONS, BlockchainModel::PegSupplyNNRole, model->getPegSupplyNNIndex());
            dPrioritySum = dPrioritySum / (nInputSum + 78);
            itemWalletAddress->setText(COLUMN_CHECKBOX, "(" + QString::number(nChildren) + ")");
            itemWalletAddress->setData(COLUMN_AMOUNT, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
            itemWalletAddress->setText(COLUMN_AMOUNT, BitcoinUnits::formatR(nDisplayUnit, nSum));
            itemWalletAddress->setText(COLUMN_RESERVE, BitcoinUnits::formatR(nDisplayUnit, nSumReserve));
            itemWalletAddress->setData(COLUMN_RESERVE, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
            itemWalletAddress->setText(COLUMN_LIQUIDITY, BitcoinUnits::formatR(nDisplayUnit, nSumLiquidity));
            itemWalletAddress->setData(COLUMN_LIQUIDITY, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
            itemWalletAddress->setText(COLUMN_PRIORITY, CoinControlDialog::getPriorityLabel(dPrioritySum));
            
            itemWalletAddress->setText(COLUMN_AMOUNT_INT64, strPad(QString::number(nSum), 15, " "));
            itemWalletAddress->setText(COLUMN_RESERVE_INT64, strPad(QString::number(nSumReserve), 15, " "));
            itemWalletAddress->setText(COLUMN_LIQUIDITY_INT64, strPad(QString::number(nSumLiquidity), 15, " "));
            itemWalletAddress->setText(COLUMN_PRIORITY_INT64, strPad(QString::number((int64_t)dPrioritySum), 20, " "));
            
            if (txType == PEG_MAKETX_SEND_LIQUIDITY || 
                txType == PEG_MAKETX_FREEZE_LIQUIDITY) {
                itemWalletAddress->setData(COLUMN_RESERVE, Qt::ForegroundRole, QColor(180,180,180));
            }
            else if (txType == PEG_MAKETX_SEND_RESERVE || 
                     txType == PEG_MAKETX_FREEZE_RESERVE) {
                itemWalletAddress->setData(COLUMN_LIQUIDITY, Qt::ForegroundRole, QColor(180,180,180));
            }
        }
    }

    // expand all partially selected
    if (treeMode)
    {
        for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
            if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) == Qt::PartiallyChecked)
                ui->treeWidget->topLevelItem(i)->setExpanded(true);
    }

    // sort view
    sortView(sortColumn, sortOrder);
    ui->treeWidget->setEnabled(true);
}

static QString displayValue(int64_t nValue) {
    QString sValue = QString::number(nValue);
    if (sValue.length() <8) {
        sValue = sValue.rightJustified(8, QChar(' '));
    }
    sValue.insert(sValue.length()-8, QChar('.'));
    if (sValue.length() > (8+1+3))
        sValue.insert(sValue.length()-8-1-3, QChar(','));
    if (sValue.length() > (8+1+3+1+3))
        sValue.insert(sValue.length()-8-1-3-1-3, QChar(','));
    if (sValue.length() > (8+1+3+1+3+1+3))
        sValue.insert(sValue.length()-8-1-3-1-3-1-3, QChar(','));
    return sValue;
}

void CoinControlDialog::openFractions(QTreeWidgetItem * item, int column)
{
    if (column != COLUMN_FRACTIONS) // only fractions column
        return;

    auto dlg = new QDialog(this);
    Ui::FractionsDialog ui;
    ui.setupUi(dlg);
    QwtPlot * fplot = new QwtPlot;
    QVBoxLayout *fvbox = new QVBoxLayout;
    fvbox->setMargin(0);
    fvbox->addWidget(fplot);
    ui.chart->setLayout(fvbox);

    QFont font = GUIUtil::bitcoinAddressFont();
    qreal pt = font.pointSizeF()*0.8;
    if (pt != .0) {
        font.setPointSizeF(pt);
    } else {
        int px = font.pixelSize()*8/10;
        font.setPixelSize(px);
    }

    QString hstyle = R"(
        QHeaderView::section {
            background-color: rgb(204,203,227);
            color: rgb(64,64,64);
            padding-left: 4px;
            border: 0px solid #6c6c6c;
            border-right: 1px solid #6c6c6c;
            border-bottom: 1px solid #6c6c6c;
            min-height: 16px;
            text-align: left;
        }
    )";
    ui.fractions->setStyleSheet(hstyle);
    ui.fractions->setFont(font);
    ui.fractions->header()->setFont(font);
    ui.fractions->header()->resizeSection(0 /*n*/, 50);
    ui.fractions->header()->resizeSection(1 /*value*/, 160);

    ui.fractions->setContextMenuPolicy(Qt::CustomContextMenu);
    //ui.fractions->installEventFilter(new FractionsDialogEvents(ui.fractions, this));
    connect(ui.fractions, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(openFractionsMenu(const QPoint &)));

    auto txhash = item->data(4, BlockchainModel::HashRole).toString();
    auto supply = item->data(4, BlockchainModel::PegSupplyRole).toInt();
    auto vfractions = item->data(4, BlockchainModel::FractionsRole);
    auto fractions = vfractions.value<CFractions>();
    auto fractions_std = fractions.Std();

    unsigned long len_test = 0;
    CDataStream fout_test(SER_DISK, CLIENT_VERSION);
    fractions.Pack(fout_test, &len_test);
    ui.packedLabel->setText(tr("Packed: %1 bytes").arg(len_test));
    ui.valueLabel->setText(tr("Value: %1").arg(displayValue(fractions.Total())));
    ui.reserveLabel->setText(tr("Reserve: %1").arg(displayValue(fractions.Low(supply))));
    ui.liquidityLabel->setText(tr("Liquidity: %1").arg(displayValue(fractions.High(supply))));

    qreal xs_reserve[PEG_SIZE*2];
    qreal ys_reserve[PEG_SIZE*2];
    qreal xs_liquidity[PEG_SIZE*2];
    qreal ys_liquidity[PEG_SIZE*2];

    for (int i=0; i<PEG_SIZE; i++) {
        QStringList row;
        row << QString::number(i) << displayValue(fractions_std.f[i]); // << QString::number(fdelta[i]) << QString::number(fd.f[i]);
        auto row_item = new QTreeWidgetItem(row);
        row_item->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        row_item->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        row_item->setData(1, BlockchainModel::ValueForCopy, qlonglong(fractions_std.f[i]));
        ui.fractions->addTopLevelItem(row_item);

        xs_reserve[i*2] = i;
        ys_reserve[i*2] = i < supply ? qreal(fractions_std.f[i]) : 0;
        xs_reserve[i*2+1] = i+1;
        ys_reserve[i*2+1] = ys_reserve[i*2];

        xs_liquidity[i*2] = i;
        ys_liquidity[i*2] = i >= supply ? qreal(fractions_std.f[i]) : 0;
        xs_liquidity[i*2+1] = i+1;
        ys_liquidity[i*2+1] = ys_liquidity[i*2];
    }

    QPen nopen(Qt::NoPen);

    auto curve_reserve = new QwtPlotCurve;
    curve_reserve->setPen(nopen);
    curve_reserve->setBrush(QColor("#c06a15"));
    curve_reserve->setSamples(xs_reserve, ys_reserve, supply*2);
    curve_reserve->setRenderHint(QwtPlotItem::RenderAntialiased);
    curve_reserve->attach(fplot);

    auto curve_liquidity = new QwtPlotCurve;
    curve_liquidity->setPen(nopen);
    curve_liquidity->setBrush(QColor("#2da5e0"));
    curve_liquidity->setSamples(xs_liquidity+supply*2,
                                ys_liquidity+supply*2,
                                PEG_SIZE*2-supply*2);
    curve_liquidity->setRenderHint(QwtPlotItem::RenderAntialiased);
    curve_liquidity->attach(fplot);

    fplot->replot();

    dlg->setWindowTitle(txhash+" "+tr("fractions"));
    dlg->show();
}

void CoinControlDialog::openFractionsMenu(const QPoint & pos)
{
    QTreeWidget * table = dynamic_cast<QTreeWidget *>(sender());
    if (!table) return;
    QModelIndex mi = table->indexAt(pos);
    if (!mi.isValid()) return;
    auto model = mi.model();
    if (!model) return;

    QMenu m;

    auto a = m.addAction(tr("Copy Value"));
    connect(a, &QAction::triggered, [&] {
        QString text;
        QModelIndex mi2 = model->index(mi.row(), 1 /*value column*/);
        QVariant v1 = mi2.data(BlockchainModel::ValueForCopy);
        if (v1.isValid())
            text = v1.toString();
        else text = mi2.data(Qt::DisplayRole).toString();
        QApplication::clipboard()->setText(text);
    });
    a = m.addAction(tr("Copy All Rows"));
    connect(a, &QAction::triggered, [&] {
        QString text;
        for(int r=0; r<model->rowCount(); r++) {
            for(int c=0; c<model->columnCount(); c++) {
                if (c>0) text += "\t";
                QModelIndex mi2 = model->index(r, c);
                QVariant v1 = mi2.data(BlockchainModel::ValueForCopy);
                if (v1.isValid())
                    text += v1.toString();
                else text += mi2.data(Qt::DisplayRole).toString();
            }
            text += "\n";
        }
        QApplication::clipboard()->setText(text);
    });
    m.exec(table->viewport()->mapToGlobal(pos));
}
