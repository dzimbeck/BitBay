// Copyright (c) 2018-2020 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockchainpage.h"
#include "ui_blockchainpage.h"
#include "ui_fractionsdialog.h"
#include "txdetailswidget.h"

#include "main.h"
#include "base58.h"
#include "txdb.h"
#include "peg.h"
#include "net.h"
#include "guiutil.h"
#include "clientmodel.h"
#include "blockchainmodel.h"
#include "itemdelegates.h"
#include "metatypes.h"
#include "qwt/qwt_plot.h"
#include "qwt/qwt_plot_curve.h"
#include "qwt/qwt_plot_barchart.h"

#include <QMenu>
#include <QTime>
#include <QTimer>
#include <QDebug>
#include <QPainter>
#include <QKeyEvent>
#include <QClipboard>
#include <QApplication>

#include <string>
#include <vector>

#include "json/json_spirit_utils.h"
#include "json/json_spirit_writer_template.h"
extern json_spirit::Object blockToJSON(const CBlock& block,
                                       const CBlockIndex* blockindex,
                                       const MapFractions &, 
                                       bool fPrintTransactionDetail);
extern void TxToJSON(const CTransaction& tx,
                     const uint256 hashBlock, 
                     const MapFractions&,
                     int nSupply,
                     json_spirit::Object& entry);
extern double GetDifficulty(const CBlockIndex* blockindex = NULL);

BlockchainPage::BlockchainPage(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::BlockchainPage)
{
    ui->setupUi(this);
    GUIUtil::SetBitBayFonts(this);

    txDetails = new TxDetailsWidget(this);
    txDetails->layout()->setMargin(0);
    auto txDetailsLayout = new QVBoxLayout;
    txDetailsLayout->setMargin(0);
    txDetailsLayout->addWidget(txDetails);
    ui->txDetails->setLayout(txDetailsLayout);

    model = new BlockchainModel(this);
    ui->blockchainView->setModel(model);

    connect(model, SIGNAL(rowsAboutToBeInserted(const QModelIndex &,int,int)),
            this, SLOT(updateCurrentBlockIndex()));
    connect(model, SIGNAL(rowsInserted(const QModelIndex &,int,int)),
            this, SLOT(scrollToCurrentBlockIndex()));

    connect(ui->buttonChain, SIGNAL(clicked()), this, SLOT(showChainPage()));
    connect(ui->buttonBlock, SIGNAL(clicked()), this, SLOT(showBlockPage()));
    connect(ui->buttonTx, SIGNAL(clicked()), this, SLOT(showTxPage()));
    connect(ui->buttonAddress, SIGNAL(clicked()), this, SLOT(showBalancePage()));
    connect(ui->buttonUnspent, SIGNAL(clicked()), this, SLOT(showUnspentPage()));
    connect(ui->buttonNet, SIGNAL(clicked()), this, SLOT(showNetPage()));
    connect(ui->buttonMempool, SIGNAL(clicked()), this, SLOT(showMempoolPage()));
    
    ui->blockchainView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->blockValues->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->netNodes->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->blockchainView->installEventFilter(new BlockchainPageChainEvents(ui->blockchainView, this));
    ui->blockValues->installEventFilter(new BlockchainPageBlockEvents(ui->blockValues, this));

    connect(ui->blockchainView, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(openChainMenu(const QPoint &)));
    connect(ui->blockValues, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(openBlockMenu(const QPoint &)));
    connect(ui->netNodes, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(openNetMenu(const QPoint &)));

    connect(ui->blockchainView, SIGNAL(activated(const QModelIndex &)),
            this, SLOT(openBlock(const QModelIndex &)));
    connect(ui->blockValues, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),
            this, SLOT(openTx(QTreeWidgetItem*,int)));
    connect(ui->blockValues, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),
            this, SLOT(openBlock(QTreeWidgetItem*,int)));

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
    ui->blockValues->setStyleSheet(hstyle);
    ui->blockchainView->setStyleSheet(hstyle);
    ui->balanceCurrent->setStyleSheet(hstyle);
    ui->balanceValues->setStyleSheet(hstyle);
    ui->utxoValues->setStyleSheet(hstyle);
    ui->netNodes->setStyleSheet(hstyle);
    ui->mempoolView->setStyleSheet(hstyle);

    ui->blockValues->setFont(font);
    ui->blockchainView->setFont(font);
    ui->balanceCurrent->setFont(font);
    ui->balanceValues->setFont(font);
    ui->utxoValues->setFont(font);
    ui->netNodes->setFont(font);
    ui->mempoolView->setFont(font);

    ui->blockValues->header()->setFont(font);
    ui->blockchainView->header()->setFont(font);
    ui->balanceCurrent->header()->setFont(font);
    ui->balanceValues->header()->setFont(font);
    ui->utxoValues->header()->setFont(font);
    ui->netNodes->header()->setFont(font);
    ui->mempoolView->header()->setFont(font);

    connect(ui->lineJumpToBlock, SIGNAL(returnPressed()),
            this, SLOT(jumpToBlock()));
    connect(ui->lineFindBlock, SIGNAL(returnPressed()),
            this, SLOT(openBlockFromInput()));
    connect(ui->lineBalanceAddress, SIGNAL(returnPressed()),
            this, SLOT(openBalanceFromInput()));
    connect(ui->lineUtxoAddress, SIGNAL(returnPressed()),
            this, SLOT(openUnspentFromInput()));
    connect(ui->lineTx, SIGNAL(returnPressed()),
            this, SLOT(openTxFromInput()));
    
    ui->netNodes->header()->resizeSection(0 /*addr*/,       250);
    ui->netNodes->header()->resizeSection(1 /*protocol*/,   100);
    ui->netNodes->header()->resizeSection(2 /*version*/,    250);
    
    ui->balanceValues->header()->resizeSection(0 /*n*/,         70);
    ui->balanceValues->header()->resizeSection(1 /*tx*/,        100);
    ui->balanceValues->header()->resizeSection(2 /*credit*/,    180);
    ui->balanceValues->header()->resizeSection(3 /*debit */,    180);
    ui->balanceValues->header()->resizeSection(4 /*balance*/,   180);
    ui->balanceValues->header()->resizeSection(5 /*frozen*/,    180);

    ui->utxoValues->header()->resizeSection(0 /*n*/,        70);
    ui->utxoValues->header()->resizeSection(1 /*tx*/,       100);
    ui->utxoValues->header()->resizeSection(2 /*liquid*/,   180);
    ui->utxoValues->header()->resizeSection(3 /*reserve*/,  180);
    ui->utxoValues->header()->resizeSection(4 /*amount*/,   180);
    ui->utxoValues->header()->resizeSection(5 /*F*/,        20);
    
    connect(txDetails, SIGNAL(openAddressBalance(QString)),
            this, SLOT(openBalanceFromTx(QString)));
    connect(ui->balanceCurrent, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),
            this, SLOT(openUnspentFromBalance(QTreeWidgetItem*,int)));
    connect(ui->balanceValues, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),
            this, SLOT(openTxFromBalance(QTreeWidgetItem*,int)));
    connect(ui->utxoValues, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),
            this, SLOT(openTxFromUnspent(QTreeWidgetItem*,int)));
    
    ui->balanceCurrent->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->balanceValues->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->utxoValues->setContextMenuPolicy(Qt::CustomContextMenu);
    
    connect(ui->balanceCurrent, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(openBalanceMenu1(const QPoint &)));
    connect(ui->balanceValues, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(openBalanceMenu2(const QPoint &)));
    connect(ui->utxoValues, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(openUnspentMenu(const QPoint &)));
    
    QTimer * t = new QTimer(this);
    t->setInterval(10 * 1000);
    connect(t, SIGNAL(timeout()), this, SLOT(updateMempool()));
    t->start();
}

BlockchainPage::~BlockchainPage()
{
    delete ui;
}

void BlockchainPage::showEvent(QShowEvent * se)
{
    static bool first_show = true;
    if (first_show) {
        first_show = false;
        {
            CTxDB txdb("r");
            bool fIsReady = false;
            bool fEnabled = false;
            txdb.ReadUtxoDbIsReady(fIsReady);
            txdb.ReadUtxoDbEnabled(fEnabled);
            ui->buttonAddress->setEnabled(/*fIsReady && */fEnabled);
            ui->buttonUnspent->setEnabled(/*fIsReady && */fEnabled);
        }
    }
    QDialog::showEvent(se);
}

BlockchainModel * BlockchainPage::blockchainModel() const
{
    return model;
}

void BlockchainPage::showChainPage()
{
    ui->tabs->setCurrentWidget(ui->pageChain);
}

void BlockchainPage::showBlockPage()
{
    ui->tabs->setCurrentWidget(ui->pageBlock);
}

void BlockchainPage::showTxPage()
{
    ui->tabs->setCurrentWidget(ui->pageTx);
}

void BlockchainPage::showBalancePage()
{
    ui->tabs->setCurrentWidget(ui->pageAddress);
}

void BlockchainPage::showUnspentPage()
{
    ui->tabs->setCurrentWidget(ui->pageUtxo);
}

void BlockchainPage::showNetPage()
{
    ui->tabs->setCurrentWidget(ui->pageNet);
}

void BlockchainPage::showMempoolPage()
{
    ui->tabs->setCurrentWidget(ui->pageMempool);
}

void BlockchainPage::jumpToTop()
{
    auto mi = ui->blockchainView->model()->index(0, 0);
    ui->blockchainView->setCurrentIndex(QModelIndex());
    ui->blockchainView->selectionModel()->clearSelection();
    ui->blockchainView->scrollTo(mi, QAbstractItemView::PositionAtCenter);
    ui->blockchainView->setFocus();
}

void BlockchainPage::jumpToBlock()
{
    bool ok = false;
    int blockNum = ui->lineJumpToBlock->text().toInt(&ok);
    if (!ok) return;

    int n = ui->blockchainView->model()->rowCount();
    int r = n-blockNum;
    if (r<0 || r>=n) return;
    auto mi = ui->blockchainView->model()->index(r, 0);
    ui->blockchainView->setCurrentIndex(mi);
    ui->blockchainView->selectionModel()->select(mi, QItemSelectionModel::Current);
    ui->blockchainView->scrollTo(mi, QAbstractItemView::PositionAtCenter);
    ui->blockchainView->setFocus();
    ui->lineJumpToBlock->clear();
}

void BlockchainPage::openBlockFromInput()
{
    bool ok = false;
    int blockNum = ui->lineFindBlock->text().toInt(&ok);
    if (ok) {
        int n = ui->blockchainView->model()->rowCount();
        int r = n-blockNum;
        if (r<0 || r>=n) return;
        auto mi = ui->blockchainView->model()->index(r, 0);
        openBlock(mi);
        return;
    }
    // consider it as hash
    uint256 hash(ui->lineFindBlock->text().toStdString());
    openBlock(hash);
}

void BlockchainPage::updateCurrentBlockIndex()
{
    currentBlockIndex = ui->blockchainView->currentIndex();
}

void BlockchainPage::scrollToCurrentBlockIndex()
{
    ui->blockchainView->scrollTo(currentBlockIndex, QAbstractItemView::PositionAtCenter);
}

void BlockchainPage::openChainMenu(const QPoint & pos)
{
    QModelIndex mi = ui->blockchainView->indexAt(pos);
    if (!mi.isValid()) return;

    QMenu m;
    auto a = m.addAction(tr("Open Block"));
    connect(a, &QAction::triggered, [&] { openBlock(mi); });
    m.addSeparator();
    a = m.addAction(tr("Copy Block Hash"));
    connect(a, &QAction::triggered, [&] {
        QApplication::clipboard()->setText(
            mi.data(BlockchainModel::HashStringRole).toString()
        );
    });
    a = m.addAction(tr("Copy Block Height"));
    connect(a, &QAction::triggered, [&] {
        QApplication::clipboard()->setText(
            mi.data(BlockchainModel::HeightRole).toString()
        );
    });
    a = m.addAction(tr("Copy Block Info"));
    connect(a, &QAction::triggered, [&] {
        auto shash = mi.data(BlockchainModel::HashStringRole).toString();
        uint256 hash(shash.toStdString());
        if (!mapBlockIndex.count(hash))
            return;

        CBlock block;
        auto pblockindex = mapBlockIndex.ref(hash);
        block.ReadFromDisk(pblockindex, true);

        // todo (extended)
        MapFractions mapFractions;
        {
            LOCK(cs_main);
            CPegDB pegdb("r");
            for (const CTransaction & tx : block.vtx) {
                for(size_t i=0; i<tx.vout.size(); i++) {
                    auto fkey = uint320(tx.GetHash(), i);
                    CFractions fractions(0, CFractions::VALUE);
                    if (pegdb.ReadFractions(fkey, fractions)) {
                        if (fractions.Total() == tx.vout[i].nValue) {
                            mapFractions[fkey] = fractions;
                        }
                    }
                }
            }
        }
        
        json_spirit::Value result = blockToJSON(block, pblockindex, mapFractions, false);
        string str = json_spirit::write_string(result, true);

        QApplication::clipboard()->setText(
            QString::fromStdString(str)
        );
    });
    m.exec(ui->blockchainView->viewport()->mapToGlobal(pos));
}

bool BlockchainPageChainEvents::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyRelease) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->matches(QKeySequence::Copy)) {
            QModelIndex mi = treeWidget->currentIndex();
            if (!mi.isValid()) return true;
            auto shash = mi.data(BlockchainModel::HashStringRole).toString();
            uint256 hash(shash.toStdString());
            if (!mapBlockIndex.count(hash))
                return true;

            CBlock block;
            auto pblockindex = mapBlockIndex.ref(hash);
            block.ReadFromDisk(pblockindex, true);

            MapFractions mapFractions;
            {
                LOCK(cs_main);
                CPegDB pegdb("r");
                for (const CTransaction & tx : block.vtx) {
                    for(size_t i=0; i<tx.vout.size(); i++) {
                        auto fkey = uint320(tx.GetHash(), i);
                        CFractions fractions(0, CFractions::VALUE);
                        if (pegdb.ReadFractions(fkey, fractions)) {
                            if (fractions.Total() == tx.vout[i].nValue) {
                                mapFractions[fkey] = fractions;
                            }
                        }
                    }
                }
            }
            
            json_spirit::Value result = blockToJSON(block, pblockindex, mapFractions, false);
            string str = json_spirit::write_string(result, true);

            QApplication::clipboard()->setText(
                QString::fromStdString(str)
            );
            return true;
        }
    }
    return QObject::eventFilter(obj, event);
}

void BlockchainPage::openBlock(const QModelIndex & mi)
{
    if (!mi.isValid())
        return;
    openBlock(mi.data(BlockchainModel::HashRole).value<uint256>());
}

void BlockchainPage::openBlock(QTreeWidgetItem* item, int)
{
    if (item->text(0) == "Next" || item->text(0) == "Previous") {
        uint256 bhash(item->text(1).toStdString());
        openBlock(bhash);
    }
}

void BlockchainPage::openBlock(uint256 hash)
{
    currentBlock = hash;
    QString bhash = QString::fromStdString(currentBlock.ToString());

    LOCK(cs_main);
    if (mapBlockIndex.find(currentBlock) == mapBlockIndex.end())
        return;
    CBlockIndex* pblockindex = mapBlockIndex.ref(currentBlock);
    if (!pblockindex)
        return;
    showBlockPage();
    if (ui->lineFindBlock->text() != bhash && ui->lineFindBlock->text().toInt() != pblockindex->nHeight)
        ui->lineFindBlock->clear();
    ui->blockValues->clear();
    auto topItem = new QTreeWidgetItem(QStringList({"Height",QString::number(pblockindex->nHeight)}));
    QVariant vhash;
    vhash.setValue(hash);
    topItem->setData(0, BlockchainModel::HashRole, vhash);
    ui->blockValues->addTopLevelItem(topItem);
    ui->blockValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Datetime",QString::fromStdString(DateTimeStrFormat(pblockindex->GetBlockTime()))})));
    ui->blockValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Hash",bhash})));
    QString pbhash;
    QString nbhash;
    if (pblockindex->pprev) {
        pbhash = QString::fromStdString(pblockindex->pprev->GetBlockHash().ToString());
    }
    if (pblockindex->pnext) {
        nbhash = QString::fromStdString(pblockindex->pnext->GetBlockHash().ToString());
    }
    CBlock block;
    block.ReadFromDisk(pblockindex, true);

    ui->blockValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Next",nbhash})));
    ui->blockValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Previous",pbhash})));
    ui->blockValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Version",QString::number(block.nVersion)})));
    ui->blockValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Block Bits", QString::fromStdString(strprintf("%08x", pblockindex->nBits))})));
    ui->blockValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Difficulty", QString::number(GetDifficulty(pblockindex))})));
    ui->blockValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Block Trust",QString::fromStdString(pblockindex->GetBlockTrust().ToString())})));
    ui->blockValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Chain Trust",QString::fromStdString(pblockindex->nChainTrust.ToString())})));

    int idx = 0;
    for(const CTransaction & tx : block.vtx) {
        QString stx = "tx"+QString::number(idx);
        QString thash = QString::fromStdString(tx.GetHash().ToString());
        ui->blockValues->addTopLevelItem(new QTreeWidgetItem(QStringList({stx,thash})));
        idx++;
    }
}

void BlockchainPage::openBlockMenu(const QPoint & pos)
{
    QModelIndex mi = ui->blockValues->indexAt(pos);
    if (!mi.isValid()) return;
    auto model = mi.model();
    if (!model) return;

    QMenu m;

    auto a = m.addAction(tr("Copy Value"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(mi.row(), 1 /*value column*/);
        QApplication::clipboard()->setText(
            mi2.data(Qt::DisplayRole).toString()
        );
    });
    a = m.addAction(tr("Copy All Rows"));
    connect(a, &QAction::triggered, [&] {
        QString text;
        for(int r=0; r<model->rowCount(); r++) {
            for(int c=0; c<model->columnCount(); c++) {
                if (c>0) text += "\t";
                QModelIndex mi2 = model->index(r, c);
                text += mi2.data(Qt::DisplayRole).toString();
            }
            text += "\n";
        }
        QApplication::clipboard()->setText(text);
    });
    a = m.addAction(tr("Copy Block Info (json)"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(0, 0); // topItem
        auto hash = mi2.data(BlockchainModel::HashRole).value<uint256>();
        if (!mapBlockIndex.count(hash))
            return;

        CBlock block;
        auto pblockindex = mapBlockIndex.ref(hash);
        block.ReadFromDisk(pblockindex, true);

        MapFractions mapFractions;
        {
            LOCK(cs_main);
            CPegDB pegdb("r");
            for (const CTransaction & tx : block.vtx) {
                for(size_t i=0; i<tx.vout.size(); i++) {
                    auto fkey = uint320(tx.GetHash(), i);
                    CFractions fractions(0, CFractions::VALUE);
                    if (pegdb.ReadFractions(fkey, fractions)) {
                        if (fractions.Total() == tx.vout[i].nValue) {
                            mapFractions[fkey] = fractions;
                        }
                    }
                }
            }
        }
        
        json_spirit::Value result = blockToJSON(block, pblockindex, mapFractions, false);
        string str = json_spirit::write_string(result, true);

        QApplication::clipboard()->setText(
            QString::fromStdString(str)
        );
    });
    m.exec(ui->blockValues->viewport()->mapToGlobal(pos));
}

void BlockchainPage::openNetMenu(const QPoint & pos)
{
    QModelIndex mi = ui->netNodes->indexAt(pos);
    if (!mi.isValid()) return;
    auto model = mi.model();
    if (!model) return;

    QMenu m;

    auto a = m.addAction(tr("Copy Value"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(mi.row(), mi.column());
        QApplication::clipboard()->setText(
            mi2.data(Qt::DisplayRole).toString()
        );
    });
    a = m.addAction(tr("Copy All Rows"));
    connect(a, &QAction::triggered, [&] {
        QString text;
        for(int r=0; r<model->rowCount(); r++) {
            for(int c=0; c<model->columnCount(); c++) {
                if (c>0) text += "\t";
                QModelIndex mi2 = model->index(r, c);
                text += mi2.data(Qt::DisplayRole).toString();
            }
            text += "\n";
        }
        QApplication::clipboard()->setText(text);
    });
    m.exec(ui->netNodes->viewport()->mapToGlobal(pos));
}

void BlockchainPage::openBalanceMenu1(const QPoint & pos)
{
    QModelIndex mi = ui->balanceCurrent->indexAt(pos);
    if (!mi.isValid()) return;
    auto model = mi.model();
    if (!model) return;

    QMenu m;

    auto a = m.addAction(tr("Copy Value"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(mi.row(), mi.column());
        QApplication::clipboard()->setText(
            mi2.data(Qt::DisplayRole).toString()
        );
    });
    a = m.addAction(tr("Copy All Rows"));
    connect(a, &QAction::triggered, [&] {
        QString text;
        for(int r=0; r<model->rowCount(); r++) {
            for(int c=0; c<model->columnCount(); c++) {
                if (c>0) text += "\t";
                QModelIndex mi2 = model->index(r, c);
                text += mi2.data(Qt::DisplayRole).toString();
            }
            text += "\n";
        }
        QApplication::clipboard()->setText(text);
    });
    m.exec(ui->balanceCurrent->viewport()->mapToGlobal(pos));
}

void BlockchainPage::openBalanceMenu2(const QPoint & pos)
{
    QModelIndex mi = ui->balanceValues->indexAt(pos);
    if (!mi.isValid()) return;
    auto model = mi.model();
    if (!model) return;

    QMenu m;

    auto a = m.addAction(tr("Copy Value"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(mi.row(), mi.column());
        QApplication::clipboard()->setText(
            mi2.data(Qt::DisplayRole).toString()
        );
    });
    a = m.addAction(tr("Copy All Rows"));
    connect(a, &QAction::triggered, [&] {
        QString text;
        for(int r=0; r<model->rowCount(); r++) {
            for(int c=0; c<model->columnCount(); c++) {
                if (c>0) text += "\t";
                QModelIndex mi2 = model->index(r, c);
                text += mi2.data(Qt::DisplayRole).toString();
            }
            text += "\n";
        }
        QApplication::clipboard()->setText(text);
    });
    m.exec(ui->balanceValues->viewport()->mapToGlobal(pos));
}

void BlockchainPage::openUnspentMenu(const QPoint & pos)
{
    QModelIndex mi = ui->utxoValues->indexAt(pos);
    if (!mi.isValid()) return;
    auto model = mi.model();
    if (!model) return;

    QMenu m;

    auto a = m.addAction(tr("Copy Value"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(mi.row(), mi.column());
        QApplication::clipboard()->setText(
            mi2.data(Qt::DisplayRole).toString()
        );
    });
    a = m.addAction(tr("Copy All Rows"));
    connect(a, &QAction::triggered, [&] {
        QString text;
        for(int r=0; r<model->rowCount(); r++) {
            for(int c=0; c<model->columnCount(); c++) {
                if (c>0) text += "\t";
                QModelIndex mi2 = model->index(r, c);
                text += mi2.data(Qt::DisplayRole).toString();
            }
            text += "\n";
        }
        QApplication::clipboard()->setText(text);
    });
    m.exec(ui->utxoValues->viewport()->mapToGlobal(pos));
}

bool BlockchainPageBlockEvents::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyRelease) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->matches(QKeySequence::Copy)) {
            QModelIndex mi = treeWidget->currentIndex();
            if (!mi.isValid()) return true;
            auto model = mi.model();
            if (!model) return true;
            QModelIndex mi2 = model->index(mi.row(), 1 /*value column*/);
            QApplication::clipboard()->setText(
                mi2.data(Qt::DisplayRole).toString()
            );
            return true;
        }
    }
    return QObject::eventFilter(obj, event);
}

void BlockchainPage::openTxFromBalance(QTreeWidgetItem * item,int)
{
    uint256 txhash = item->data(0, BlockchainModel::HashRole).value<uint256>();
    CTxDB txdb("r");
    CTxIndex txindex;
    txdb.ReadTxIndex(txhash, txindex);
    uint nTxNum = 0;
    uint256 blockhash;
    txindex.GetHeightInMainChain(&nTxNum, txhash, &blockhash);
    showTxPage();
    txDetails->openTx(blockhash, nTxNum);
}

void BlockchainPage::openTxFromUnspent(QTreeWidgetItem * item,int)
{
    uint256 txhash = item->data(0, BlockchainModel::HashRole).value<uint256>();
    CTxDB txdb("r");
    CTxIndex txindex;
    txdb.ReadTxIndex(txhash, txindex);
    uint nTxNum = 0;
    uint256 blockhash;
    txindex.GetHeightInMainChain(&nTxNum, txhash, &blockhash);
    showTxPage();
    txDetails->openTx(blockhash, nTxNum);
}

void BlockchainPage::openTxFromInput()
{
    // as height-index
    if (ui->lineTx->text().contains("-")) {
        auto args = ui->lineTx->text().split("-");
        bool ok = false;
        int blockNum = args.front().toInt(&ok);
        if (ok) {
            uint txidx = args.back().toUInt();
            int n = ui->blockchainView->model()->rowCount();
            int r = n-blockNum;
            if (r<0 || r>=n) return;
            auto mi = ui->blockchainView->model()->index(r, 0);
            auto bhash = mi.data(BlockchainModel::HashRole).value<uint256>();
            showTxPage();
            txDetails->openTx(bhash, txidx);
        }
        return;
    }
    // consider it as hash
    uint nTxNum = 0;
    uint256 blockhash;
    uint256 txhash(ui->lineTx->text().toStdString());
    {
        LOCK(cs_main);
        CTxDB txdb("r");
        CTxIndex txindex;
        if (!txdb.ReadTxIndex(txhash, txindex)) {
            showTxPage();
            txDetails->showNotFound();
            return;
        }
        txindex.GetHeightInMainChain(&nTxNum, txhash, &blockhash);
    }
    showTxPage();
    txDetails->openTx(blockhash, nTxNum);
}

void BlockchainPage::openTx(QTreeWidgetItem * item, int column)
{
    Q_UNUSED(column);
    if (item->text(0).startsWith("tx")) { // open from block page
        bool tx_idx_ok = false;
        uint tx_idx = item->text(0).mid(2).toUInt(&tx_idx_ok);
        if (!tx_idx_ok)
            return;

        showTxPage();
        txDetails->openTx(currentBlock, tx_idx);
    }
}

bool BlockchainPageTxEvents::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyRelease) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->matches(QKeySequence::Copy)) {
            QModelIndex mi = treeWidget->currentIndex();
            if (!mi.isValid()) return true;
            auto model = mi.model();
            if (!model) return true;
            QModelIndex mi2 = model->index(mi.row(), 1 /*value column*/);
            QApplication::clipboard()->setText(
                mi2.data(Qt::DisplayRole).toString()
            );
            return true;
        }
    }
    return QObject::eventFilter(obj, event);
}

void BlockchainPage::setClientModel(ClientModel *model)
{
    if(model) {
        connect(model, SIGNAL(connectionsChanged(const CNodeShortStats &)), 
                this, SLOT(updateConnections(const CNodeShortStats &)));
        updateConnections(model->getConnections());
    }
}

void BlockchainPage::updateConnections(const CNodeShortStats & stats)
{
    ui->netNodes->clear();
    for(const CNodeShortStat & node : stats) {
        auto twi = new QTreeWidgetItem;
        twi->setText(0, QString::fromStdString(node.addrName));
        twi->setText(1, QString::number(node.nVersion));
        twi->setText(2, QString::fromStdString(node.strSubVer));
        twi->setText(3, QString::number(node.nStartingHeight));
        ui->netNodes->addTopLevelItem(twi);
    }
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

static QString displayValueR(int64_t nValue, int len=0) {
    if (len==0) {
        len = 8+1+3+1+3+1+3+5;
    }
    QString sValue = displayValue(nValue);
    sValue = sValue.rightJustified(len, QChar(' '));
    return sValue;
}

static QString extractAddress(const CTxOut & txout) {
    QString fromAddr;
    int nRequired;
    txnouttype type;
    vector<CTxDestination> addresses;
    if (ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired)) {
        if (addresses.size()==1) {
            fromAddr = QString::fromStdString(CBitcoinAddress(addresses.front()).ToString());
        }
        else if (addresses.size()>1) {
            fromAddr = QString("multisig").rightJustified(34);
        }
    }
    return fromAddr;
}

void BlockchainPage::updateMempool()
{
    set<int> removeIndexes;
    set<uint256> oldTxhashes;
    set<uint256> newTxhashes;
    set<uint256> insertTxhashes;
    {
        LOCK(mempool.cs);
        for (map<uint256, CTransaction>::iterator mi = mempool.mapTx.begin(); mi != mempool.mapTx.end(); ++mi)
        {
            newTxhashes.insert((*mi).first);
        }
    }
    for(int i =0; i< ui->mempoolView->topLevelItemCount(); i++) {
        auto item = ui->mempoolView->topLevelItem(i);
        auto oldTxhash = item->data(0, Qt::UserRole).value<uint256>();
        if (!newTxhashes.count(oldTxhash)) {
            removeIndexes.insert(i);
        } else {
            oldTxhashes.insert(oldTxhash);
        }
    }
    for (set<int>::reverse_iterator rit = removeIndexes.rbegin(); rit != removeIndexes.rend(); rit++) {
        auto item = ui->mempoolView->takeTopLevelItem(*rit);
        if (item) { delete item; }
    }
    for (uint256 newTxhash : newTxhashes) {
        if (oldTxhashes.count(newTxhash)) { continue; }
        insertTxhashes.insert(newTxhash);
    }
    
    {
        LOCK(mempool.cs);
        for (uint256 newTxhash : insertTxhashes) {
            if (!mempool.mapTx.count(newTxhash)) continue;
            const auto & tx = mempool.mapTx.at(newTxhash);
            QStringList txcols;
            txcols << QString::fromStdString(newTxhash.ToString());
            auto txitem = new QTreeWidgetItem(ui->mempoolView, txcols);
            QFont f = txitem->font(0);
            f.setBold(true);
            txitem->setFont(0, f);
            ui->mempoolView->expandItem(txitem);
            
            size_t nVin = tx.vin.size();
            size_t nVout = tx.vout.size();
            
            int nAlignInpHigh = 0;
            for(size_t i=0; i< nVin; i++)
            {
                const COutPoint & prevout = tx.vin[i].prevout;
                auto fkey = uint320(prevout.hash, prevout.n);
                
                if (!mempool.mapPrevOuts.count(newTxhash)) continue;
                if (!mempool.mapPrevOuts.at(newTxhash).count(fkey)) continue;

                const CTxOut & txout = mempool.mapPrevOuts.at(newTxhash).at(fkey);
                
                QString text = displayValue(txout.nValue);
                if (text.length() > nAlignInpHigh)
                    nAlignInpHigh = text.length();
            }
            
            for(size_t i=0; i< nVin; i++)
            {
                QStringList inpcols;
                inpcols << QString::fromStdString(tx.vin[i].prevout.ToString());

                const COutPoint & prevout = tx.vin[i].prevout;
                auto fkey = uint320(prevout.hash, prevout.n);
                
                if (!mempool.mapPrevOuts.count(newTxhash)) continue;
                if (!mempool.mapPrevOuts.at(newTxhash).count(fkey)) continue;
                
                const CTxOut & txout = mempool.mapPrevOuts.at(newTxhash).at(fkey);
                QString fromAddr = extractAddress(txout);
                
                auto inpitem = new QTreeWidgetItem(txitem, inpcols);
                inpitem->setText(0, fromAddr + " " + displayValueR(txout.nValue, nAlignInpHigh) + " --> ");
            }
            
            size_t nOuts = 0;
            nAlignInpHigh = 0;
            for(size_t i=0; i< nVout; i++)
            {
                const CTxOut & txout = tx.vout[i];
                QString toAddr = extractAddress(txout);
                if (toAddr.isEmpty()) continue;
                QString text = displayValue(txout.nValue);
                if (text.length() > nAlignInpHigh)
                    nAlignInpHigh = text.length();
                nOuts++;
            }
            
            if (nOuts > nVin) {
                for(size_t i=nVin; i< nOuts; i++)
                {
                    auto inpitem = new QTreeWidgetItem(txitem);
                    inpitem->setText(0, QString(" ").repeated(34+1+nAlignInpHigh)+" --> ");
                }
            }
            
            size_t nOut = 0;
            for(size_t i=0; i< nVout; i++)
            {
                const CTxOut & txout = tx.vout[i];
                QString toAddr = extractAddress(txout);
                if (toAddr.isEmpty()) continue;

                auto outitem = txitem->child(nOut);
                QString text = outitem->text(0);
                
                text += toAddr + " " + displayValueR(txout.nValue, nAlignInpHigh);
                outitem->setText(0, text);
                nOut++;
            }
        }
    }
}

void BlockchainPage::openBalanceFromInput()
{
    QString addr = ui->lineBalanceAddress->text();
    ui->balanceCurrent->clear();
    ui->balanceValues->clear();
    if (addr.length() != 34) return;
    openBalanceFromTx(addr);
}

void BlockchainPage::openBalanceFromTx(QString addr)
{
    showBalancePage();
    if (addr != ui->lineBalanceAddress->text())
        ui->lineBalanceAddress->setText(addr);
    
    bool fPegPruneEnabled = true;
    {
        CPegDB pegdb("r");
        if (!pegdb.ReadPegPruneEnabled(fPegPruneEnabled)) {
            fPegPruneEnabled = true;
        }
    }
    
    ui->balanceCurrent->clear();
    ui->balanceValues->clear();
    
    LOCK(cs_main);
    CTxDB txdb("r");
    vector<CAddressBalance> records;
    bool ok = txdb.ReadAddressBalanceRecords(addr.toStdString(), records);
    if (!ok) return;
    int nIdx = records.size();
    bool isLatestRecord = true;
    for (const auto & record : records) {
        if (isLatestRecord) {
            CFractions pegbalance;
            txdb.ReadPegBalance(addr.toStdString(), pegbalance);
            int64_t nLiquid = pegbalance.High(pindexBest->nPegSupplyIndex);
            int64_t nReserve = pegbalance.Low(pindexBest->nPegSupplyIndex);
            int nValueMaxLen = qMax(displayValue(record.nBalance).length(),
                                    qMax(displayValue(nLiquid).length(),
                                         qMax(displayValue(nReserve).length(),
                                              displayValue(record.nFrozen).length())));
            
            ui->balanceCurrent->addTopLevelItem(new QTreeWidgetItem(QStringList({"Total",displayValueR(record.nBalance, nValueMaxLen)})));
            ui->balanceCurrent->addTopLevelItem(new QTreeWidgetItem(QStringList({"Liquid",displayValueR(nLiquid, nValueMaxLen)})));
            ui->balanceCurrent->addTopLevelItem(new QTreeWidgetItem(QStringList({"Reserve",displayValueR(nReserve, nValueMaxLen)})));
            ui->balanceCurrent->addTopLevelItem(new QTreeWidgetItem(QStringList({"Frozen",displayValueR(record.nFrozen, nValueMaxLen)})));
            isLatestRecord = false;
        }
        auto item = new QTreeWidgetItem;
        QVariant vhash;
        vhash.setValue(record.txhash);
        item->setData(0, BlockchainModel::HashRole, vhash);
        item->setText(0, QString::number(nIdx));
        item->setData(2, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        item->setData(3, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        item->setData(4, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        item->setData(5, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        if (record.nIndex == 1) { // stake
            item->setText(1, QString("%1-%2").arg(record.nHeight).arg(record.nIndex));
            if (record.nCredit == 0) {
                item->setData(2, Qt::TextColorRole, QColor("#808080"));
                item->setText(2, "(stake)");
            } else {
                item->setText(2, "-"+displayValue(record.nCredit));
            }
            if (record.nDebit == 0) {
                item->setData(3, Qt::TextColorRole, QColor("#808080"));
                item->setText(3, "(reward moved)");
            } else {
                item->setText(3, "+"+displayValue(record.nDebit));
            }
        }
        else if (record.nIndex >=0) {
            item->setText(1, QString("%1-%2").arg(record.nHeight).arg(record.nIndex));
            item->setText(2, record.nCredit>0 ? "-"+displayValue(record.nCredit) : QString(""));
            item->setText(3, record.nDebit >0 ? "+"+displayValue(record.nDebit ) : QString(""));
        } else {
            item->setText(1, QString("%1-U").arg(record.nHeight));
            item->setData(2, Qt::TextColorRole, QColor("#808080"));
            item->setText(2, QString("(unfreeze)"));
            item->setData(3, Qt::TextColorRole, QColor("#808080"));
            item->setText(3, displayValue(record.nDebit));
        }
        item->setText(4, displayValue(record.nBalance));
        item->setData(5, Qt::TextColorRole, QColor("#808080"));
        if (fPegPruneEnabled && record.nHeight >= uint64_t(nPegStartHeight) && 
            (uint64_t(nBestHeight)-record.nHeight) > PEG_PRUNE_INTERVAL) {
            item->setText(5, QString("(pruned)"));
        } else {
            item->setText(5, record.nFrozen >0 ? displayValue(record.nFrozen) : QString(""));
        }
        item->setText(6, QString::fromStdString(DateTimeStrFormat(record.nTime)));
        ui->balanceValues->addTopLevelItem(item);
        nIdx--;
    }
    
    if (isLatestRecord) {
        int nValueMaxLen = qMax(displayValue(0).length(),
                                qMax(displayValue(0).length(),
                                     qMax(displayValue(0).length(),
                                          displayValue(0).length())));
        
        ui->balanceCurrent->addTopLevelItem(new QTreeWidgetItem(QStringList({"Total",displayValueR(0, nValueMaxLen)})));
        ui->balanceCurrent->addTopLevelItem(new QTreeWidgetItem(QStringList({"Liquid",displayValueR(0, nValueMaxLen)})));
        ui->balanceCurrent->addTopLevelItem(new QTreeWidgetItem(QStringList({"Reserve",displayValueR(0, nValueMaxLen)})));
        ui->balanceCurrent->addTopLevelItem(new QTreeWidgetItem(QStringList({"Frozen",displayValueR(0, nValueMaxLen)})));
        isLatestRecord = false;
    }
}

void BlockchainPage::openUnspentFromBalance(QTreeWidgetItem*,int)
{
    QString addr = ui->lineBalanceAddress->text();
    openUnspentFromAddress(addr);
}

void BlockchainPage::openUnspentFromInput()
{
    QString addr = ui->lineUtxoAddress->text();
    openUnspentFromAddress(addr);
}

void BlockchainPage::openUnspentFromAddress(QString addr)
{
    showUnspentPage();
    if (addr != ui->lineUtxoAddress->text())
        ui->lineUtxoAddress->setText(addr);
    
    ui->utxoValues->clear();
    
    LOCK(cs_main);
    CTxDB txdb("r");
    CPegDB pegdb("r");

    auto totalitem = new QTreeWidgetItem;
    {
        totalitem->setText(0, QString("Balance"));
        QFont f = totalitem->data(0, Qt::FontRole).value<QFont>();
        f.setBold(true);
        QVariant vf;
        vf.setValue(f);
        totalitem->setData(0, Qt::FontRole, vf);
        totalitem->setData(2, Qt::FontRole, vf);
        totalitem->setData(3, Qt::FontRole, vf);
        totalitem->setData(4, Qt::FontRole, vf);
        totalitem->setData(2, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        totalitem->setData(3, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        totalitem->setData(4, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        ui->utxoValues->addTopLevelItem(totalitem);
    }
        
    {
        
        int64_t nLiquid = 0;
        int64_t nReserve = 0;
        int64_t nBalance = 0;
        vector<CAddressUnspent> records;
        bool ok = txdb.ReadAddressUnspent(addr.toStdString(), records);
        if (ok) {
            int nIdx = records.size();
            for (const auto & record : records) {
                uint320 txoutid(record.txoutid);
                auto item = new QTreeWidgetItem;
                QVariant vhash;
                vhash.setValue(txoutid.b1());
                item->setData(0, BlockchainModel::HashRole, vhash);
                item->setText(0, QString::number(nIdx));
                item->setData(2, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
                item->setData(3, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
                item->setData(4, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
                item->setText(1, QString("%1-%2:%3").arg(record.nHeight).arg(record.nIndex).arg(txoutid.b2()));
                CFractions fractions(record.nAmount, CFractions::STD);
                if (record.nHeight > nPegStartHeight) {
                    if (pegdb.ReadFractions(txoutid, fractions, true /*must_have*/)) {
                        int64_t nUnspentLiquid = fractions.High(pindexBest->nPegSupplyIndex);
                        int64_t nUnspentReserve = fractions.Low(pindexBest->nPegSupplyIndex);
                        item->setText(2, displayValue(nUnspentLiquid));
                        item->setText(3, displayValue(nUnspentReserve));
                        nLiquid += nUnspentLiquid;
                        nReserve += nUnspentReserve;
                    }
                } else {
                    int64_t nUnspentLiquid = fractions.High(pindexBest->nPegSupplyIndex);
                    int64_t nUnspentReserve = fractions.Low(pindexBest->nPegSupplyIndex);
                    item->setText(2, displayValue(nUnspentLiquid));
                    item->setText(3, displayValue(nUnspentReserve));
                    nLiquid += nUnspentLiquid;
                    nReserve += nUnspentReserve;
                }
                nBalance += record.nAmount;
                item->setText(4, displayValue(record.nAmount));
                ui->utxoValues->addTopLevelItem(item);
                nIdx--;
            }
        }
        
        totalitem->setText(2, displayValue(nLiquid));
        totalitem->setText(3, displayValue(nReserve));
        totalitem->setText(4, displayValue(nBalance));
    }
    
    auto frozenitem = new QTreeWidgetItem;
    {
        frozenitem->setText(0, QString("Frozen"));
        QFont f = frozenitem->data(0, Qt::FontRole).value<QFont>();
        f.setBold(true);
        QVariant vf;
        vf.setValue(f);
        frozenitem->setData(0, Qt::FontRole, vf);
        frozenitem->setData(4, Qt::FontRole, vf);
        frozenitem->setData(2, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        frozenitem->setData(3, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        frozenitem->setData(4, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        ui->utxoValues->addTopLevelItem(frozenitem);
    }
    
    {
        int64_t nFrozen = 0;
        
        vector<CAddressUnspent> records;
        bool ok = txdb.ReadAddressFrozen(addr.toStdString(), records);
        if (ok) {
            int nIdx = records.size();
            for (const auto & record : records) {
                uint320 txoutid(record.txoutid);
                auto item = new QTreeWidgetItem;
                QVariant vhash;
                vhash.setValue(txoutid.b1());
                item->setData(0, BlockchainModel::HashRole, vhash);
                item->setText(0, "F-"+QString::number(nIdx));
                item->setData(2, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
                item->setData(3, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
                item->setData(4, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
                item->setText(1, QString("%1-%2:%3").arg(record.nHeight).arg(record.nIndex).arg(txoutid.b2()));
                item->setText(4, displayValue(record.nAmount));
                nFrozen += record.nAmount;
                ui->utxoValues->addTopLevelItem(item);
                nIdx--;
            }
        }
        frozenitem->setText(4, displayValue(nFrozen));
    }
}

