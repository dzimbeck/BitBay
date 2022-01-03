// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdetailswidget.h"
#include "ui_txdetails.h"
#include "ui_fractionsdialog.h"
#include "ui_pegvotesdialog.h"

#include "main.h"
#include "base58.h"
#include "txdb.h"
#include "peg.h"
#include "guiutil.h"
#include "blockchainmodel.h"
#include "itemdelegates.h"
#include "metatypes.h"
#include "qwt/qwt_plot.h"
#include "qwt/qwt_plot_item.h"
#include "qwt/qwt_plot_curve.h"
#include "qwt/qwt_plot_barchart.h"

#include <QTime>
#include <QMenu>
#include <QDebug>
#include <QPainter>
#include <QKeyEvent>
#include <QClipboard>
#include <QApplication>

#include <string>
#include <vector>

#include <boost/multiprecision/cpp_int.hpp>
using namespace boost;

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

TxDetailsWidget::TxDetailsWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TxDetails)
{
    ui->setupUi(this);
    GUIUtil::SetBitBayFonts(this);

    ui->txValues->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->txInputs->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->txOutputs->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->txValues->installEventFilter(new TxDetailsWidgetTxEvents(ui->txValues, this));

    connect(ui->txValues, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(openTxMenu(const QPoint &)));
    connect(ui->txInputs, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(openInpMenu(const QPoint &)));
    connect(ui->txOutputs, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(openOutMenu(const QPoint &)));

    connect(ui->txInputs, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),
            this, SLOT(openFractions(QTreeWidgetItem*,int)));
    connect(ui->txOutputs, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),
            this, SLOT(openFractions(QTreeWidgetItem*,int)));
    connect(ui->txOutputs, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),
            this, SLOT(openPegVotes(QTreeWidgetItem*,int)));
    connect(ui->txInputs, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),
            this, SLOT(openTx(QTreeWidgetItem*,int)));
    connect(ui->txOutputs, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),
            this, SLOT(openTx(QTreeWidgetItem*,int)));

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
    ui->txValues->setStyleSheet(hstyle);
    ui->txInputs->setStyleSheet(hstyle);
    ui->txOutputs->setStyleSheet(hstyle);

    ui->txValues->setFont(font);
    ui->txInputs->setFont(font);
    ui->txOutputs->setFont(font);

    ui->txValues->header()->setFont(font);
    ui->txInputs->header()->setFont(font);
    ui->txOutputs->header()->setFont(font);

    ui->txValues->header()->resizeSection(0 /*property*/, 250);
    ui->txInputs->header()->resizeSection(0 /*n*/, 50);
    ui->txOutputs->header()->resizeSection(0 /*n*/, 50);
    ui->txInputs->header()->resizeSection(1 /*tx*/, 140);
    ui->txOutputs->header()->resizeSection(1 /*tx*/, 140);
    ui->txInputs->header()->resizeSection(2 /*addr*/, 280);
    ui->txOutputs->header()->resizeSection(2 /*addr*/, 280);
    ui->txInputs->header()->resizeSection(3 /*value*/, 180);
    ui->txOutputs->header()->resizeSection(3 /*value*/, 180);

    auto txInpValueDelegate = new LeftSideIconItemDelegate(ui->txInputs);
    auto txOutValueDelegate = new LeftSideIconItemDelegate(ui->txOutputs);
    ui->txInputs->setItemDelegateForColumn(3 /*value*/, txInpValueDelegate);
    ui->txOutputs->setItemDelegateForColumn(3 /*value*/, txOutValueDelegate);

    auto txInpFractionsDelegate = new FractionsItemDelegate(ui->txInputs);
    auto txOutFractionsDelegate = new FractionsItemDelegate(ui->txOutputs);
    ui->txInputs->setItemDelegateForColumn(4 /*frac*/, txInpFractionsDelegate);
    ui->txOutputs->setItemDelegateForColumn(4 /*frac*/, txOutFractionsDelegate);

    pmChange = QPixmap(":/icons/change");
    pmChange = pmChange.scaled(32,32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    pmNotaryF = QPixmap(":/icons/frostr");
    pmNotaryF = pmNotaryF.scaled(32,32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    pmNotaryV = QPixmap(":/icons/frostl");
    pmNotaryV = pmNotaryV.scaled(32,32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

TxDetailsWidget::~TxDetailsWidget()
{
    delete ui;
}

void TxDetailsWidget::openTx(QTreeWidgetItem * item, int column)
{
    if ((sender() == ui->txInputs && column == COL_INP_TX) || 
        (sender() == ui->txOutputs && column == COL_OUT_TX)) {
        uint256 txhash = item->data(1, BlockchainModel::HashRole).value<uint256>();
        CTxDB txdb("r");
        CTxIndex txindex;
        txdb.ReadTxIndex(txhash, txindex);
        uint nTxNum = 0;
        uint256 blockhash;
        txindex.GetHeightInMainChain(&nTxNum, txhash, &blockhash);
        openTx(blockhash, nTxNum);
    }
    if ((sender() == ui->txInputs && column == COL_INP_ADDR) || 
        (sender() == ui->txOutputs && column == COL_OUT_ADDR)) {
        QString address = item->text(COL_INP_ADDR);
        if (address.length() != 34) 
            return;
        
        CTxDB txdb("r");
        bool fIsReady = false;
        bool fEnabled = false;
        txdb.ReadUtxoDbIsReady(fIsReady);
        txdb.ReadUtxoDbEnabled(fEnabled);
        if (!fIsReady || !fEnabled)
            return;
        
        emit openAddressBalance(address);
    }
}

static QString scriptToAddress(const CScript& scriptPubKey,
                               bool& is_notary,
                               bool& is_voting,
                               bool show_alias =true) {
    is_notary = false;
    is_voting = false;
    int nRequired;
    txnouttype type;
    vector<CTxDestination> addresses;
    if (ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
        std::string str_addr_all;
        bool none = true;
        for(const CTxDestination& addr : addresses) {
            std::string str_addr = CBitcoinAddress(addr).ToString();
            if (show_alias) {
                if (str_addr == Params().PegInflateAddr()) {
                    str_addr = "peginflate";
                    is_voting = true;
                }
                else if (str_addr == Params().PegDeflateAddr()) {
                    str_addr = "pegdeflate";
                    is_voting = true;
                }
                else if (str_addr == Params().PegNochangeAddr()) {
                    str_addr = "pegnochange";
                    is_voting = true;
                }
            }
            if (!str_addr_all.empty())
                str_addr_all += "\n";
            str_addr_all += str_addr;
            none = false;
        }
        if (!none)
            return QString::fromStdString(str_addr_all);
    }
    const CScript& script1 = scriptPubKey;

    opcodetype opcode1;
    vector<unsigned char> vch1;
    CScript::const_iterator pc1 = script1.begin();
    if (!script1.GetOp(pc1, opcode1, vch1))
        return QString();

    if (opcode1 == OP_RETURN && script1.size()>1) {
        is_notary = true;
        QString left_bytes;
        unsigned long len_bytes = script1[1];
        if (len_bytes > script1.size()-2)
            len_bytes = script1.size()-2;
        for(unsigned int i=0; i< len_bytes; i++) {
            left_bytes += char(script1[i+2]);
        }
        return left_bytes;
    }

    return QString();
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

static QString txId(CTxDB& txdb, uint256 txhash) {
    QString txid;
    CTxIndex txindex;
    txdb.ReadTxIndex(txhash, txindex);
    uint nTxNum = 0;
    int nHeight = txindex.GetHeightInMainChain(&nTxNum, txhash);
    if (nHeight >0) {
        txid = QString("%1-%2").arg(nHeight).arg(nTxNum);
    }
    return txid;
}

static bool calculateFeesFractions(CBlockIndex* pblockindex,
                                   CFractions& feesFractions,
                                   int64_t& nFeesValue)
{
    CBlock block;
    block.ReadFromDisk(pblockindex, true);
    
    MapPrevTx mapInputs;
    MapFractions mapInputsFractions;
    map<uint256, CTxIndex> mapUnused;
    MapFractions mapFractionsUnused;
    string sPegFailCause;

    bool ok = true;
    for (CTransaction & tx : block.vtx) {

        if (tx.IsCoinBase()) continue;
        if (tx.IsCoinStake()) continue;

        uint256 hash = tx.GetHash();

        CTxDB txdb("r");
        CPegDB pegdb("r");
        if (!txdb.ContainsTx(hash))
            return false;

        bool fInvalid = false;
        tx.FetchInputs(txdb, pegdb, 
                       mapUnused, mapFractionsUnused, 
                       false, false, 
                       mapInputs, mapInputsFractions, 
                       fInvalid,
                       true /*skip pruned*/);

        int64_t nTxValueIn = tx.GetValueIn(mapInputs);
        int64_t nTxValueOut = tx.GetValueOut();
        nFeesValue += nTxValueIn - nTxValueOut;

        bool peg_ok = CalculateStandardFractions(tx,
                                                 pblockindex->nPegSupplyIndex,
                                                 pblockindex->nTime,
                                                 mapInputs, mapInputsFractions,
                                                 mapFractionsUnused,
                                                 feesFractions,
                                                 sPegFailCause);

        if (!peg_ok) {
            ok = false;
        }
    }

    return ok;
}

void TxDetailsWidget::showNotFound()
{
    ui->txValues->clear();
    ui->txInputs->clear();
    ui->txOutputs->clear();
    ui->txValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Error","Transaction not found"})));
}

void TxDetailsWidget::openTx(uint256 blockhash, uint txidx)
{
    LOCK(cs_main);
    if (mapBlockIndex.find(blockhash) == mapBlockIndex.end())
        return;
    CBlockIndex* pblockindex = mapBlockIndex.ref(blockhash);
    if (!pblockindex)
        return;

    CBlock block;
    block.ReadFromDisk(pblockindex, true);
    if (txidx >= block.vtx.size())
        return;

    int nPegInterval = Params().PegInterval(pblockindex->nHeight);
    int nCycle = pblockindex->nHeight / nPegInterval;
    
    CTransaction & tx = block.vtx[txidx];
    openTx(tx, pblockindex, txidx, 
           nCycle,
           pblockindex->nPegSupplyIndex, 
           pblockindex->GetNextIntervalPegSupplyIndex(),
           pblockindex->GetNextNextIntervalPegSupplyIndex(),
           pblockindex->nTime);
}

void TxDetailsWidget::openTx(CTransaction & tx, 
                             CBlockIndex* pblockindex, 
                             uint txidx, 
                             int nCycle, 
                             int nSupply, 
                             int nSupplyN, 
                             int nSupplyNN, 
                             unsigned int nTime)
{
    ui->txValues->clear();
    ui->txInputs->clear();
    ui->txOutputs->clear();
    
    LOCK(cs_main);

    uint256 hash = tx.GetHash();

    CTxDB txdb("r");
    CPegDB pegdb("r");

    bool pruned = false;
    if (pblockindex) {
        QString thash = QString::fromStdString(hash.ToString());
        QString sheight = QString("%1-%2").arg(pblockindex->nHeight).arg(txidx);
        auto topItem = new QTreeWidgetItem(QStringList({"Height",sheight}));
        QVariant vhash;
        vhash.setValue(hash);
        topItem->setData(0, BlockchainModel::HashRole, vhash);
        ui->txValues->addTopLevelItem(topItem);
        ui->txValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Datetime",QString::fromStdString(DateTimeStrFormat(pblockindex->GetBlockTime()))})));
        int nConfirmations = 0;
        {
            LOCK(cs_main);
            nConfirmations = pindexBest->nHeight - pblockindex->nHeight + 1;
        }
        ui->txValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Confirmations",QString::number(nConfirmations)})));
        ui->txValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Hash",thash})));
        
        bool fPegPruneEnabled = true;
        if (!pegdb.ReadPegPruneEnabled(fPegPruneEnabled)) {
            fPegPruneEnabled = true;
        }
        if (fPegPruneEnabled) {
            if (pblockindex->nHeight <= nBestHeight-PEG_PRUNE_INTERVAL) {
                pruned = true;
            }
        }
    }

    QString txtype = tr("Transaction");
    if (tx.IsCoinBase()) txtype = tr("CoinBase");
    if (tx.IsCoinStake()) txtype = tr("CoinStake");
    ui->txValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Type",txtype})));
    ui->txValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Peg Supply Index",QString::number(nSupply)})));

    if (tx.IsCoinStake() && !pblockindex) {
        ui->txValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Error","No blockindex provided"})));
        return;
    }
    
    // logic

    QTime timeFetchInputs = QTime::currentTime();
    MapPrevTx mapInputs;
    MapFractions mapInputsFractions;
    map<uint256, CTxIndex> mapUnused;
    MapFractions mapFractionsUnused;
    CFractions feesFractions(0, CFractions::STD);
    int64_t nFeesValue = 0;
    string sPegFailCause;
    bool fInvalid = false;
    tx.FetchInputs(txdb, pegdb, 
                   mapUnused, mapFractionsUnused, 
                   false, false, 
                   mapInputs, mapInputsFractions, 
                   fInvalid,
                   true /*skip pruned*/);
    int msecsFetchInputs = timeFetchInputs.msecsTo(QTime::currentTime());

    int64_t nReserveIn = 0;
    int64_t nLiquidityIn = 0;
    for(auto const & inputFractionItem : mapInputsFractions) {
        inputFractionItem.second.LowPart(nSupply, &nReserveIn);
        inputFractionItem.second.HighPart(nSupply, &nLiquidityIn);
    }

    bool peg_ok = false;

    // need to collect all fees fractions from all tx in the block
    if (tx.IsCoinStake() && !calculateFeesFractions(pblockindex, feesFractions, nFeesValue)) {
        if (!pruned) {
            ui->txValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Error","Failed to calculate fees fractions"})));
            return;
        }
    }
    
    QTime timePegChecks = QTime::currentTime();
    if (tx.IsCoinStake()) {
        uint64_t nCoinAge = 0;
        if (!tx.GetCoinAge(txdb, pblockindex->pprev, nCoinAge)) {
            ui->txValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Error","Cannot get coin age"})));
            return;
        }
        CFractions inpStake(0, CFractions::STD);
        if (tx.vin.size() > 0) {
            const COutPoint & prevout = tx.vin.front().prevout;
            auto fkey = uint320(prevout.hash, prevout.n);
            if (mapInputsFractions.find(fkey) != mapInputsFractions.end()) {
                inpStake = mapInputsFractions[fkey];
            }
        }
        int64_t nStakeRewardWithoutFees = GetProofOfStakeReward(
                    pblockindex->pprev, nCoinAge, 0 /*fees*/, inpStake);
        
        peg_ok = CalculateStakingFractions(tx, pblockindex,
                                           mapInputs, mapInputsFractions,
                                           mapUnused, mapFractionsUnused,
                                           feesFractions,
                                           nStakeRewardWithoutFees,
                                           sPegFailCause);
    }
    else {
        peg_ok = CalculateStandardFractions(tx,
                                            nSupply,
                                            nTime,
                                            mapInputs, mapInputsFractions,
                                            mapFractionsUnused,
                                            feesFractions,
                                            sPegFailCause);
    }
    int msecsPegChecks = timePegChecks.msecsTo(QTime::currentTime());

    size_t n_vin = tx.vin.size();
    size_t n_vout = tx.vout.size();
    if (tx.IsCoinBase()) n_vin = 0;
    if (tx.IsCoinBase()) n_vout = 0;
    
    int64_t nValueIn = 0;
    for (unsigned int i = 0; i < n_vin; i++) {
        COutPoint prevout = tx.vin[i].prevout;
        if (mapInputs.find(prevout.hash) != mapInputs.end()) {
            CTransaction& txPrev = mapInputs[prevout.hash].second;
            if (prevout.n < txPrev.vout.size()) {
                int64_t nValue = txPrev.vout[prevout.n].nValue;
                nValueIn += nValue;
            }
        }
    }
    int64_t nValueOut = 0;
    for (unsigned int i = 0; i < n_vout; i++) {
        int64_t nValue = tx.vout[i].nValue;
        nValueOut += nValue;
    }
    
    // gui

    QSet<QString> sAddresses;
    for (unsigned int i = 0; i < n_vin; i++)
    {
        COutPoint prevout = tx.vin[i].prevout;
        QStringList row;
        row << QString::number(i); // idx, 0

        QString prev_thash = QString::fromStdString(prevout.hash.ToString());
        QString prev_txid_hash = prev_thash.left(4)+"..."+prev_thash.right(4);
        QString prev_txid_height = txId(txdb, prevout.hash);
        QString prev_txid = prev_txid_height.isEmpty() ? prev_txid_hash : prev_txid_height;

        row << QString("%1:%2").arg(prev_txid).arg(prevout.n); // tx, 1
        auto prev_input = QString("%1:%2").arg(prev_thash).arg(prevout.n); // tx, 1
        int64_t nValue = 0;

        if (mapInputs.find(prevout.hash) != mapInputs.end()) {
            CTransaction& txPrev = mapInputs[prevout.hash].second;
            if (prevout.n < txPrev.vout.size()) {
                bool is_notary = false;
                bool is_voting = false;
                auto addr = scriptToAddress(txPrev.vout[prevout.n].scriptPubKey, is_notary, is_voting);
                if (addr.isEmpty())
                    row << "N/A"; // address, 2
                else {
                    row << addr;
                    sAddresses.insert(addr);
                }

                nValue = txPrev.vout[prevout.n].nValue;
                row << displayValue(nValue);
            }
            else {
                row << "N/A"; // address, 2
                row << "none"; // value, 3
            }
        }
        else {
            row << "N/A"; // address
            row << "none"; // value
        }

        auto input = new QTreeWidgetItem(row);
        {
            QVariant vhash;
            vhash.setValue(prevout.hash);
            input->setData(COL_INP_TX, BlockchainModel::HashRole, vhash);
            input->setData(COL_INP_TX, BlockchainModel::OutNumRole, prevout.n);
        }
        auto fkey = uint320(prevout.hash, prevout.n);
        if (mapInputsFractions.find(fkey) != mapInputsFractions.end()) {
            QVariant vfractions;
            vfractions.setValue(mapInputsFractions[fkey]);
            input->setData(COL_INP_FRACTIONS, BlockchainModel::HashRole, prev_input);
            input->setData(COL_INP_FRACTIONS, BlockchainModel::FractionsRole, vfractions);
            input->setData(COL_INP_FRACTIONS, BlockchainModel::PegCycleRole, nCycle);
            input->setData(COL_INP_FRACTIONS, BlockchainModel::PegSupplyRole, nSupply);
            input->setData(COL_INP_FRACTIONS, BlockchainModel::PegSupplyNRole, nSupplyN);
            input->setData(COL_INP_FRACTIONS, BlockchainModel::PegSupplyNNRole, nSupplyNN);
            if (mapInputsFractions[fkey].nFlags & CFractions::NOTARY_F) {
                input->setData(COL_INP_VALUE, Qt::DecorationPropertyRole, pmNotaryF);
            }
            else if (mapInputsFractions[fkey].nFlags & CFractions::NOTARY_V) {
                input->setData(COL_INP_VALUE, Qt::DecorationPropertyRole, pmNotaryV);
            }
        }
        input->setData(COL_INP_VALUE, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        input->setData(COL_INP_VALUE, BlockchainModel::ValueForCopy, qlonglong(nValue));
        ui->txInputs->addTopLevelItem(input);
    }

    if (tx.IsCoinStake()) {
        uint64_t nCoinAge = 0;
        if (!tx.GetCoinAge(txdb, pblockindex->pprev, nCoinAge)) {
            ui->txValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Error","Cannot get coin age"})));
            return;
        }
        CFractions inpStake(0, CFractions::STD);
        if (tx.vin.size() > 0) {
            const COutPoint & prevout = tx.vin.front().prevout;
            auto fkey = uint320(prevout.hash, prevout.n);
            if (mapInputsFractions.find(fkey) != mapInputsFractions.end()) {
                inpStake = mapInputsFractions[fkey];
            }
        }
        
        int64_t nMined = nValueOut - nValueIn - nFeesValue;

        QStringList rowMined;
        rowMined << "Mined"; // idx, 0
        rowMined << "";      // tx, 1
        rowMined << "N/A";   // address, 2
        rowMined << displayValue(nMined);

        auto inputMined = new QTreeWidgetItem(rowMined);
        QVariant vfractions;
        vfractions.setValue(CFractions(nMined, CFractions::STD));
        inputMined->setData(COL_INP_FRACTIONS, BlockchainModel::FractionsRole, vfractions);
        inputMined->setData(COL_INP_FRACTIONS, BlockchainModel::PegCycleRole, nCycle);
        inputMined->setData(COL_INP_FRACTIONS, BlockchainModel::PegSupplyRole, nSupply);
        inputMined->setData(COL_INP_FRACTIONS, BlockchainModel::PegSupplyNRole, nSupplyN);
        inputMined->setData(COL_INP_FRACTIONS, BlockchainModel::PegSupplyNNRole, nSupplyNN);
        inputMined->setData(COL_INP_VALUE, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        inputMined->setData(COL_INP_VALUE, BlockchainModel::ValueForCopy, qlonglong(nMined));
        ui->txInputs->addTopLevelItem(inputMined);

        QStringList rowFees;
        rowFees << "Fees";  // idx, 0
        rowFees << "";      // tx, 1
        rowFees << "N/A";   // address, 2
        rowFees << displayValue(nFeesValue);

        auto inputFees = new QTreeWidgetItem(rowFees);
        vfractions.setValue(feesFractions);
        inputFees->setData(COL_INP_FRACTIONS, BlockchainModel::FractionsRole, vfractions);
        inputFees->setData(COL_INP_FRACTIONS, BlockchainModel::PegCycleRole, nCycle);
        inputFees->setData(COL_INP_FRACTIONS, BlockchainModel::PegSupplyRole, nSupply);
        inputFees->setData(COL_INP_FRACTIONS, BlockchainModel::PegSupplyNRole, nSupplyN);
        inputFees->setData(COL_INP_FRACTIONS, BlockchainModel::PegSupplyNNRole, nSupplyNN);
        inputFees->setData(COL_INP_VALUE, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        inputFees->setData(COL_INP_VALUE, BlockchainModel::ValueForCopy, qlonglong(nFeesValue));
        ui->txInputs->addTopLevelItem(inputFees);
    }

    int nAlignInpHigh = 0;
    for(int i=0; i< ui->txInputs->topLevelItemCount(); i++) {
        auto input = ui->txInputs->topLevelItem(i);
        auto nSupply = input->data(COL_INP_FRACTIONS, BlockchainModel::PegSupplyRole).toInt();
        QVariant vfractions = input->data(COL_INP_FRACTIONS, BlockchainModel::FractionsRole);
        if (!vfractions.isValid()) continue;
        CFractions fractions = vfractions.value<CFractions>();
        int64_t liquidity = fractions.High(nSupply);
        QString text = displayValue(liquidity);
        if (text.length() > nAlignInpHigh)
            nAlignInpHigh = text.length();
    }
    for(int i=0; i< ui->txInputs->topLevelItemCount(); i++) {
        auto input = ui->txInputs->topLevelItem(i);
        int64_t nValue = input->data(COL_INP_VALUE, BlockchainModel::ValueForCopy).toLongLong();
        auto nSupply = input->data(COL_INP_FRACTIONS, BlockchainModel::PegSupplyRole).toInt();
        QVariant vfractions = input->data(COL_INP_FRACTIONS, BlockchainModel::FractionsRole);
        if (!vfractions.isValid()) continue;
        CFractions fractions = vfractions.value<CFractions>();
        if (fractions.Total() != nValue) continue;
        int64_t reserve = fractions.Low(nSupply);
        int64_t liquidity = fractions.High(nSupply);
        input->setData(COL_INP_FRACTIONS, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        input->setText(COL_INP_FRACTIONS, displayValue(reserve)+" / "+displayValueR(liquidity, nAlignInpHigh));
    }

    CTxIndex txindex;
    txdb.ReadTxIndex(hash, txindex);

    if (pruned) {
        for (unsigned int i = 0; i < n_vout; i++)
        {
            auto fkey = uint320(hash, i);
            CFractions fractions(tx.vout[i].nValue, CFractions::STD);
            if (pegdb.ReadFractions(fkey, fractions, true /*must_have*/)) {
                mapFractionsUnused[fkey] = fractions;
            }
        }
    }
    
    for (unsigned int i = 0; i < n_vout; i++)
    {
        QStringList row;
        row << QString::number(i); // 0, idx

        bool hasSpend = false;
        QString titleSpend;
        uint256 next_hash;
        uint next_outnum = 0;

        if (i < txindex.vSpent.size()) {
            CDiskTxPos & txpos = txindex.vSpent[i];
            CTransaction txSpend;
            if (txSpend.ReadFromDisk(txpos)) {
                int vin_idx =0;
                for(const CTxIn &txin : txSpend.vin) {
                    if (txin.prevout.hash == hash && txin.prevout.n == i) {
                        next_hash = txSpend.GetHash();
                        next_outnum = i;

                        QString next_thash = QString::fromStdString(next_hash.ToString());
                        QString next_txid_hash = next_thash.left(4)+"..."+next_thash.right(4);
                        QString next_txid_height = txId(txdb, next_hash);
                        QString next_txid = next_txid_height.isEmpty() ? next_txid_hash : next_txid_height;

                        row << QString("%1:%2").arg(next_txid).arg(vin_idx); // 1, spend
                        titleSpend = QString("%1:%2").arg(next_thash).arg(vin_idx);
                        hasSpend = true;
                    }
                    vin_idx++;
                }
            }
        }

        bool is_notary = false;
        bool is_voting = false;
        auto addr = scriptToAddress(tx.vout[i].scriptPubKey, is_notary, is_voting);

        if (!hasSpend) {
            if (is_notary) {
                row << "Notary/Burn"; // 1, spend
            }
            else if (is_voting && !pruned) {
                CFractions inpStake(0, CFractions::STD);
                if (tx.vin.size() > 0) {
                    const COutPoint & prevout = tx.vin.front().prevout;
                    auto fkey = uint320(prevout.hash, prevout.n);
                    if (mapInputsFractions.find(fkey) != mapInputsFractions.end()) {
                        inpStake = mapInputsFractions[fkey];
                    }
                }
                int nVotes = CalculatePegVotes(inpStake, nSupply);
                row << QString("+%1 Votes").arg(nVotes);
            }
            else
                row << ""; // 1, spend
        }

        if (addr.isEmpty())
            row << "N/A"; // 2, address
        else row << addr;

        int64_t nValue = tx.vout[i].nValue;
        row << displayValue(nValue); // 3, value

        bool fIndicateFrozen = false;
        auto output = new QTreeWidgetItem(row);
        if (hasSpend) {
            QVariant vhash;
            vhash.setValue(next_hash);
            output->setData(COL_OUT_TX, BlockchainModel::HashRole, vhash);
            output->setData(COL_OUT_TX, BlockchainModel::OutNumRole, next_outnum);
        }
        if (is_voting && !pruned) {
            QVariant vFractions;
            CFractions inpStake(0, CFractions::STD);
            if (tx.vin.size() > 0) {
                const COutPoint & prevout = tx.vin.front().prevout;
                auto fkey = uint320(prevout.hash, prevout.n);
                if (mapInputsFractions.find(fkey) != mapInputsFractions.end()) {
                    inpStake = mapInputsFractions[fkey];
                }
            }
            vFractions.setValue(inpStake);
            output->setData(COL_OUT_TX, BlockchainModel::FractionsRole, vFractions);
            output->setData(COL_OUT_TX, BlockchainModel::PegCycleRole, nCycle);
            output->setData(COL_OUT_TX, BlockchainModel::PegSupplyRole, nSupply);
            output->setData(COL_OUT_TX, BlockchainModel::PegSupplyNRole, nSupplyN);
            output->setData(COL_OUT_TX, BlockchainModel::PegSupplyNNRole, nSupplyNN);
        }
        auto fkey = uint320(hash, i);
        if (mapFractionsUnused.find(fkey) != mapFractionsUnused.end()) {
            QVariant vFractions;
            vFractions.setValue(mapFractionsUnused[fkey]);
            output->setData(COL_OUT_FRACTIONS, BlockchainModel::HashRole, titleSpend);
            output->setData(COL_OUT_FRACTIONS, BlockchainModel::FractionsRole, vFractions);
            output->setData(COL_OUT_FRACTIONS, BlockchainModel::PegCycleRole, nCycle);
            output->setData(COL_OUT_FRACTIONS, BlockchainModel::PegSupplyRole, nSupply);
            output->setData(COL_OUT_FRACTIONS, BlockchainModel::PegSupplyNRole, nSupplyN);
            output->setData(COL_OUT_FRACTIONS, BlockchainModel::PegSupplyNNRole, nSupplyNN);
            if (mapFractionsUnused[fkey].nFlags & CFractions::NOTARY_F) {
                output->setData(COL_OUT_VALUE, Qt::DecorationPropertyRole, pmNotaryF);
                fIndicateFrozen = true;
            }
            else if (mapFractionsUnused[fkey].nFlags & CFractions::NOTARY_V) {
                output->setData(COL_OUT_VALUE, Qt::DecorationPropertyRole, pmNotaryV);
                fIndicateFrozen = true;
            }
        }
        output->setData(COL_OUT_VALUE, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        output->setData(COL_OUT_VALUE, BlockchainModel::ValueForCopy, qlonglong(nValue));
        if (!fIndicateFrozen && sAddresses.contains(addr)) {
            output->setData(COL_OUT_VALUE, Qt::DecorationPropertyRole, pmChange);
        }
        ui->txOutputs->addTopLevelItem(output);
    }

    int nValueMaxLen = qMax(displayValue(nValueIn).length(),
                            qMax(displayValue(nValueOut).length(),
                                 qMax(displayValue(nReserveIn).length(),
                                      displayValue(nLiquidityIn).length())));
    auto sValueIn = displayValueR(nValueIn, nValueMaxLen);
    auto sValueOut = displayValueR(nValueOut, nValueMaxLen);

    auto twiValueIn = new QTreeWidgetItem(QStringList({"Value In",sValueIn}));
    auto twiValueOut = new QTreeWidgetItem(QStringList({"Value Out",sValueOut}));

    twiValueIn->setData(1, BlockchainModel::ValueForCopy, qlonglong(nValueIn));
    twiValueOut->setData(1, BlockchainModel::ValueForCopy, qlonglong(nValueOut));

    ui->txValues->addTopLevelItem(twiValueIn);
    ui->txValues->addTopLevelItem(twiValueOut);
    if (!pruned) {
        auto sReserveIn = displayValueR(nReserveIn, nValueMaxLen);
        auto sLiquidityIn = displayValueR(nLiquidityIn, nValueMaxLen);
        
        auto twiReserves = new QTreeWidgetItem(QStringList({"Reserve",sReserveIn}));
        auto twiLiquidity = new QTreeWidgetItem(QStringList({"Liquidity",sLiquidityIn}));
        
        twiReserves->setData(1, BlockchainModel::ValueForCopy, qlonglong(nReserveIn));
        twiLiquidity->setData(1, BlockchainModel::ValueForCopy, qlonglong(nLiquidityIn));
        
        ui->txValues->addTopLevelItem(twiReserves);
        ui->txValues->addTopLevelItem(twiLiquidity);
    }

    auto twiPegChecks = new QTreeWidgetItem(
                QStringList({
                    "Peg Checks",
                    peg_ok ? "OK" 
                            : pruned ? "SKIP (pruned)" 
                            : "FAIL ("+QString::fromStdString(sPegFailCause)+")"
                })
    );
    ui->txValues->addTopLevelItem(twiPegChecks);
    if (!pruned) {
        ui->txValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Peg Checks Time",QString::number(msecsPegChecks)+" msecs"})));
    }
    ui->txValues->addTopLevelItem(new QTreeWidgetItem(QStringList({"Fetch Inputs Time",QString::number(msecsFetchInputs)+" msecs (can be cached)"})));

    if (!tx.IsCoinBase() && !tx.IsCoinStake() /*&& nValueOut < nValueIn*/) {
        QStringList row;
        row << "Fee";
        row << ""; // spent
        row << ""; // address (todo)
        row << displayValue(nValueIn - nValueOut);
        auto outputFees = new QTreeWidgetItem(row);
        QVariant vfractions;
        vfractions.setValue(feesFractions);
        outputFees->setData(COL_OUT_FRACTIONS, BlockchainModel::FractionsRole, vfractions);
        outputFees->setData(COL_OUT_FRACTIONS, BlockchainModel::PegCycleRole, nCycle);
        outputFees->setData(COL_OUT_FRACTIONS, BlockchainModel::PegSupplyRole, nSupply);
        outputFees->setData(COL_OUT_FRACTIONS, BlockchainModel::PegSupplyNRole, nSupplyN);
        outputFees->setData(COL_OUT_FRACTIONS, BlockchainModel::PegSupplyNNRole, nSupplyNN);
        outputFees->setData(COL_OUT_VALUE, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        outputFees->setData(COL_OUT_VALUE, BlockchainModel::ValueForCopy, qlonglong(nValueIn - nValueOut));
        ui->txOutputs->addTopLevelItem(outputFees);
    }

    int nAlignOutHigh = 0;
    for(int i=0; i< ui->txOutputs->topLevelItemCount(); i++) {
        auto output = ui->txOutputs->topLevelItem(i);
        auto nSupply = output->data(COL_OUT_FRACTIONS, BlockchainModel::PegSupplyRole).toInt();
        QVariant vfractions = output->data(COL_OUT_FRACTIONS, BlockchainModel::FractionsRole);
        if (!vfractions.isValid()) continue;
        CFractions fractions = vfractions.value<CFractions>();
        int64_t liquidity = fractions.High(nSupply);
        QString text = displayValue(liquidity);
        if (text.length() > nAlignOutHigh)
            nAlignOutHigh = text.length();
    }
    for(int i=0; i< ui->txOutputs->topLevelItemCount(); i++) {
        auto output = ui->txOutputs->topLevelItem(i);
        int64_t nValue = output->data(COL_OUT_VALUE, BlockchainModel::ValueForCopy).toLongLong();
        auto nSupply = output->data(COL_OUT_FRACTIONS, BlockchainModel::PegSupplyRole).toInt();
        QVariant vfractions = output->data(COL_OUT_FRACTIONS, BlockchainModel::FractionsRole);
        if (!vfractions.isValid()) continue;
        CFractions fractions = vfractions.value<CFractions>();
        if (fractions.Total() != nValue) continue;
        int64_t reserve = fractions.Low(nSupply);
        int64_t liquidity = fractions.High(nSupply);
        output->setData(COL_OUT_FRACTIONS, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        output->setText(COL_OUT_FRACTIONS, displayValue(reserve)+" / "+displayValueR(liquidity, nAlignOutHigh));
    }

}

void TxDetailsWidget::openTxMenu(const QPoint & pos)
{
    QModelIndex mi = ui->txValues->indexAt(pos);
    if (!mi.isValid()) return;
    auto model = mi.model();
    if (!model) return;

    QMenu m;

    auto a = m.addAction(tr("Copy Value"));
    connect(a, &QAction::triggered, [&] {
        QString text;
        QModelIndex mi2 = model->index(mi.row(), 1 /*value column*/);
        QVariant v2 = mi2.data(BlockchainModel::ValueForCopy);
        if (v2.isValid())
            text = mi2.data(BlockchainModel::ValueForCopy).toString();
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
                text += mi2.data(Qt::DisplayRole).toString();
            }
            text += "\n";
        }
        QApplication::clipboard()->setText(text);
    });
    a = m.addAction(tr("Copy Transaction Info (json)"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(0, 0); // topItem
        auto hash = mi2.data(BlockchainModel::HashRole).value<uint256>();

        CTransaction tx;
        uint256 hashBlock = 0;
        if (!GetTransaction(hash, tx, hashBlock))
            return;

        int nSupply = -1;
        MapFractions mapFractions;
        {
            LOCK(cs_main);
            CPegDB pegdb("r");
            for(size_t i=0; i<tx.vout.size(); i++) {
                auto fkey = uint320(tx.GetHash(), i);
                CFractions fractions(0, CFractions::VALUE);
                if (pegdb.ReadFractions(fkey, fractions)) {
                    if (fractions.Total() == tx.vout[i].nValue) {
                        mapFractions[fkey] = fractions;
                    }
                }
            }

            map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                CBlockIndex* pindex = (*mi).second;
                nSupply = pindex->nPegSupplyIndex;
            }
        }

        json_spirit::Object result;
        TxToJSON(tx, hashBlock, mapFractions, nSupply, result);
        json_spirit::Value vresult = result;
        string str = json_spirit::write_string(vresult, true);

        QApplication::clipboard()->setText(
            QString::fromStdString(str)
        );
    });
    m.exec(ui->txValues->viewport()->mapToGlobal(pos));
}

void TxDetailsWidget::openInpMenu(const QPoint & pos)
{
    QModelIndex mi = ui->txInputs->indexAt(pos);
    if (!mi.isValid()) return;
    auto model = mi.model();
    if (!model) return;

    QMenu m;

    auto a = m.addAction(tr("Copy Value"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(mi.row(), COL_INP_VALUE);
        QApplication::clipboard()->setText(
            mi2.data(BlockchainModel::ValueForCopy).toString()
        );
    });
    a = m.addAction(tr("Copy Reserve"));
    connect(a, &QAction::triggered, [&] {
        QString text = "None";
        QModelIndex mi2 = model->index(mi.row(), COL_INP_VALUE);
        int64_t nValue = mi2.data(BlockchainModel::ValueForCopy).toLongLong();
        QModelIndex mi3 = model->index(mi.row(), COL_INP_FRACTIONS);
        int nSupply = mi3.data(BlockchainModel::PegSupplyRole).toInt();
        QVariant vfractions = mi3.data(BlockchainModel::FractionsRole);
        if (vfractions.isValid()) {
            CFractions fractions = vfractions.value<CFractions>();
            if (fractions.Total() == nValue) {
                int64_t nReserve = fractions.Low(nSupply);
                text = QString::number(nReserve);
            }
        }
        QApplication::clipboard()->setText(text);
    });
    a = m.addAction(tr("Copy Liquidity"));
    connect(a, &QAction::triggered, [&] {
        QString text = "None";
        QModelIndex mi2 = model->index(mi.row(), COL_INP_VALUE);
        int64_t nValue = mi2.data(BlockchainModel::ValueForCopy).toLongLong();
        QModelIndex mi3 = model->index(mi.row(), COL_INP_FRACTIONS);
        int nSupply = mi3.data(BlockchainModel::PegSupplyRole).toInt();
        QVariant vfractions = mi3.data(BlockchainModel::FractionsRole);
        if (vfractions.isValid()) {
            CFractions fractions = vfractions.value<CFractions>();
            if (fractions.Total() == nValue) {
                int64_t nLiquidity = fractions.High(nSupply);
                text = QString::number(nLiquidity);
            }
        }
        QApplication::clipboard()->setText(text);
    });
    m.addSeparator();
    a = m.addAction(tr("Copy Address"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(mi.row(), COL_INP_ADDR);
        QApplication::clipboard()->setText(
            mi2.data(Qt::DisplayRole).toString()
        );
    });
    a = m.addAction(tr("Copy Input Hash"));
    connect(a, &QAction::triggered, [&] {
        QString text = "None";
        QModelIndex mi2 = model->index(mi.row(), COL_INP_TX);
        QVariant vhash = mi2.data(BlockchainModel::HashRole);
        if (vhash.isValid()) {
            uint256 hash = vhash.value<uint256>();
            int n = mi2.data(BlockchainModel::OutNumRole).toInt();
            text = QString::fromStdString(hash.ToString());
            text += ":"+QString::number(n);
        }
        QApplication::clipboard()->setText(text);
    });
    a = m.addAction(tr("Copy Input Height"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(mi.row(), COL_INP_TX);
        QApplication::clipboard()->setText(
            mi2.data(Qt::DisplayRole).toString()
        );
    });
    m.addSeparator();
    a = m.addAction(tr("Copy Input Fractions"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(mi.row(), COL_INP_FRACTIONS);
        QVariant vfractions = mi2.data(BlockchainModel::FractionsRole);
        if (!vfractions.isValid()) return;
        CFractions fractions = vfractions.value<CFractions>().Std();
        QString text;
        for (int i=0; i<PEG_SIZE; i++) {
            if (i!=0) text += "\n";
            text += QString::number(i);
            text += "\t";
            text += QString::number(qlonglong(fractions.f[i]));
        }
        QApplication::clipboard()->setText(text);
    });

    m.exec(ui->txInputs->viewport()->mapToGlobal(pos));
}

void TxDetailsWidget::openOutMenu(const QPoint & pos)
{
    QModelIndex mi = ui->txOutputs->indexAt(pos);
    if (!mi.isValid()) return;
    auto model = mi.model();
    if (!model) return;

    QMenu m;

    auto a = m.addAction(tr("Copy Value"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(mi.row(), COL_OUT_VALUE);
        QApplication::clipboard()->setText(
            mi2.data(BlockchainModel::ValueForCopy).toString()
        );
    });
    a = m.addAction(tr("Copy Reserve"));
    connect(a, &QAction::triggered, [&] {
        QString text = "None";
        QModelIndex mi2 = model->index(mi.row(), COL_OUT_VALUE);
        int64_t nValue = mi2.data(BlockchainModel::ValueForCopy).toLongLong();
        QModelIndex mi3 = model->index(mi.row(), COL_OUT_FRACTIONS);
        int nSupply = mi3.data(BlockchainModel::PegSupplyRole).toInt();
        QVariant vfractions = mi3.data(BlockchainModel::FractionsRole);
        if (vfractions.isValid()) {
            CFractions fractions = vfractions.value<CFractions>();
            if (fractions.Total() == nValue) {
                int64_t nReserve = fractions.Low(nSupply);
                text = QString::number(nReserve);
            }
        }
        QApplication::clipboard()->setText(text);
    });
    a = m.addAction(tr("Copy Liquidity"));
    connect(a, &QAction::triggered, [&] {
        QString text = "None";
        QModelIndex mi2 = model->index(mi.row(), COL_OUT_VALUE);
        int64_t nValue = mi2.data(BlockchainModel::ValueForCopy).toLongLong();
        QModelIndex mi3 = model->index(mi.row(), COL_OUT_FRACTIONS);
        int nSupply = mi3.data(BlockchainModel::PegSupplyRole).toInt();
        QVariant vfractions = mi3.data(BlockchainModel::FractionsRole);
        if (vfractions.isValid()) {
            CFractions fractions = vfractions.value<CFractions>();
            if (fractions.Total() == nValue) {
                int64_t nLiquidity = fractions.High(nSupply);
                text = QString::number(nLiquidity);
            }
        }
        QApplication::clipboard()->setText(text);
    });
    m.addSeparator();
    a = m.addAction(tr("Copy Address"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(mi.row(), COL_OUT_ADDR);
        QApplication::clipboard()->setText(
            mi2.data(Qt::DisplayRole).toString()
        );
    });
    a = m.addAction(tr("Copy Spent Hash"));
    connect(a, &QAction::triggered, [&] {
        QString text = "None";
        QModelIndex mi2 = model->index(mi.row(), COL_OUT_TX);
        QVariant vhash = mi2.data(BlockchainModel::HashRole);
        if (vhash.isValid()) {
            uint256 hash = vhash.value<uint256>();
            int n = mi2.data(BlockchainModel::OutNumRole).toInt();
            text = QString::fromStdString(hash.ToString());
            text += ":"+QString::number(n);
        }
        QApplication::clipboard()->setText(text);
    });
    a = m.addAction(tr("Copy Spent Height"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(mi.row(), COL_OUT_TX);
        QApplication::clipboard()->setText(
            mi2.data(Qt::DisplayRole).toString()
        );
    });
    m.addSeparator();
    a = m.addAction(tr("Copy Spent Fractions"));
    connect(a, &QAction::triggered, [&] {
        QModelIndex mi2 = model->index(mi.row(), COL_OUT_FRACTIONS);
        QVariant vfractions = mi2.data(BlockchainModel::FractionsRole);
        if (!vfractions.isValid()) return;
        CFractions fractions = vfractions.value<CFractions>().Std();
        QString text;
        for (int i=0; i<PEG_SIZE; i++) {
            if (i!=0) text += "\n";
            text += QString::number(i);
            text += "\t";
            text += QString::number(qlonglong(fractions.f[i]));
        }
        QApplication::clipboard()->setText(text);
    });

    m.exec(ui->txOutputs->viewport()->mapToGlobal(pos));
}

bool TxDetailsWidgetTxEvents::eventFilter(QObject *obj, QEvent *event)
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

void TxDetailsWidget::openPegVotes(QTreeWidgetItem * item, int column)
{
    if (column != COL_OUT_TX) // only "spent" column
        return;
    
    QString dest = item->text(COL_OUT_ADDR);
    if (dest != "peginflate" &&
        dest != "pegdeflate" &&
        dest != "pegnochange")
        return;

    int nPegSupplyIndex = item->data(COL_OUT_TX, BlockchainModel::PegSupplyRole).toInt();
    QVariant vfractions = item->data(COL_OUT_TX, BlockchainModel::FractionsRole);
    if (!vfractions.isValid()) 
        return;
    
    CFractions fractions = vfractions.value<CFractions>();
    if (fractions.Total() ==0) 
        return;
    
    auto dlg = new QDialog(this);
    Ui::PegVotesDialog ui;
    ui.setupUi(dlg);

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
    ui.votesinfo->setStyleSheet(hstyle);
    ui.votesinfo->setFont(font);
    ui.votesinfo->header()->setFont(font);
    
    ui.votesinfo->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui.votesinfo->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui.votesinfo->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    
    {
        QStringList cells;
        cells << "Peg Index";
        cells << "Peg";
        cells << QString::number(nPegSupplyIndex);
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
    }
    
    {
        QStringList cells;
        cells << "Peg Maximum";
        cells << "PegMax";
        cells << QString::number(nPegMaxSupplyIndex);
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
    }
    
    int64_t nLiquid = fractions.High(nPegSupplyIndex);
    int64_t nReserve = fractions.Low(nPegSupplyIndex);
    
    {
        QStringList cells;
        cells << "";
        cells << "";
        cells << "";
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
    }
    
    int64_t nLiquidWeight = nLiquid;
    if (nLiquidWeight > INT_LEAST64_MAX/(nPegSupplyIndex+2)) {
        // check for rare extreme case when user stake more than about 100M coins
        // in this case multiplication is very close int64_t overflow (int64 max is ~92 GCoins)
        multiprecision::uint128_t nLiquidWeight128(nLiquidWeight);
        multiprecision::uint128_t nPegSupplyIndex128(nPegSupplyIndex);
        multiprecision::uint128_t nPegMaxSupplyIndex128(nPegMaxSupplyIndex);
        multiprecision::uint128_t f128 = (nLiquidWeight128*nPegSupplyIndex128)/nPegMaxSupplyIndex128;
        nLiquidWeight -= f128.convert_to<int64_t>();
    }
    else // usual case, fast calculations
        nLiquidWeight -= nLiquidWeight * nPegSupplyIndex / nPegMaxSupplyIndex;
    
    int64_t nReserveWeight = nReserve;
        
    int nAlignOutHigh = 0;
    {
        QString text1 = displayValue(nLiquid);
        QString text2 = displayValue(nReserve);
        QString text3 = displayValue(nLiquidWeight);
        QString text4 = displayValue(nReserveWeight);
        if (text1.length() > nAlignOutHigh) nAlignOutHigh = text1.length();
        if (text2.length() > nAlignOutHigh) nAlignOutHigh = text2.length();
        if (text3.length() > nAlignOutHigh) nAlignOutHigh = text3.length();
        if (text4.length() > nAlignOutHigh) nAlignOutHigh = text4.length();
    }
    
    {
        QStringList cells;
        cells << "Liquid";
        cells << "Liquid";
        cells << displayValueR(nLiquid, nAlignOutHigh);
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
    }

    {
        QStringList cells;
        cells << "Reserve";
        cells << "Reserve";
        cells << displayValueR(nReserve, nAlignOutHigh);
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
    }
    
    {
        QStringList cells;
        cells << "";
        cells << "";
        cells << "";
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
    }
    
    {
        QStringList cells;
        cells << "Liquid Weight";
        cells << "Liquid X (1 - Peg / PegMax)";
        cells << displayValueR(nLiquidWeight, nAlignOutHigh);
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
    }

    {
        QStringList cells;
        cells << "Reserve Weight";
        cells << "Reserve";
        cells << displayValueR(nReserveWeight, nAlignOutHigh);
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
    }
    
    {
        QStringList cells;
        cells << "";
        cells << "";
        cells << "";
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
    }
    
    int nVotes = 1;
    int nVotesTotal = 1;
    int nWeightMultiplier = nPegSupplyIndex/120+1;
    
    bool has_x2 = false;
    bool has_x3 = false;
    bool has_x4 = false;
    
    if (nLiquidWeight > (nReserveWeight*4)) {
        nVotesTotal = 4*nWeightMultiplier;
        nVotes = nWeightMultiplier;
        has_x4 = true;
    }
    else if (nLiquidWeight > (nReserveWeight*3)) {
        nVotesTotal = 3*nWeightMultiplier;
        nVotes = nWeightMultiplier;
        has_x3 = true;
    }
    else if (nLiquidWeight > (nReserveWeight*2)) {
        nVotesTotal = 2*nWeightMultiplier;
        nVotes = nWeightMultiplier;
        has_x2 = true;
    }
    
    {
        QStringList cells;
        cells << "Votes Multiplier";
        cells << "1 + Peg/120";
        if (has_x2 || has_x3 || has_x4) {
            cells << QString::number(nWeightMultiplier);
        } else {
            cells << "off";
        }
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
        
        if (!has_x2 && !has_x3 && !has_x4) {
            auto fs = twi->flags();
            fs = fs & ~Qt::ItemIsEnabled;
            twi->setFlags(fs);
        }
    }
    
    {
        QStringList cells;
        cells << "Votes 1x";
        cells << "";
        cells << "+"+QString::number(nVotes)+" Votes";
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
    }
    
    {
        QStringList cells;
        cells << "Votes 2x";
        cells << "Liquid Weight > 2 X Reserve Weight";
        if (has_x2 || has_x3 || has_x4) {
            cells << "+"+QString::number(nVotes)+" Votes";
        }
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
    }

    {
        QStringList cells;
        cells << "Votes 3x";
        cells << "Liquid Weight > 3 X Reserve Weight";
        if (has_x3 || has_x4) {
            cells << "+"+QString::number(nVotes)+" Votes";
        }
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
    }

    {
        QStringList cells;
        cells << "Votes 4x";
        cells << "Liquid Weight > 4 X Reserve Weight";
        if (has_x4) {
            cells << "+"+QString::number(nVotes)+" Votes";
        }
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
    }
    
    {
        QStringList cells;
        cells << "";
        cells << "";
        cells << "";
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
    }
    
    {
        QStringList cells;
        cells << "Votes Total";
        cells << "";
        cells << "+"+QString::number(nVotesTotal)+" Votes";
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        ui.votesinfo->addTopLevelItem(twi);
        
        auto f = twi->font(0);
        f.setBold(true);
        twi->setFont(0, f);
        twi->setFont(1, f);
        twi->setFont(2, f);
    }
    
    dlg->show();
}

void TxDetailsWidget::openFractions(QTreeWidgetItem * item, int column)
{
    if (column != COL_OUT_FRACTIONS) // only fractions column
        return;

    auto dlg = new QDialog(this);
    Ui::FractionsDialog ui;
    ui.setupUi(dlg);
    fplot = new QwtPlot;
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
    auto cycle = item->data(4, BlockchainModel::PegCycleRole).toInt();
    auto supply = item->data(4, BlockchainModel::PegSupplyRole).toInt();
    auto supplyn = item->data(4, BlockchainModel::PegSupplyNRole).toInt();
    auto supplynn = item->data(4, BlockchainModel::PegSupplyNNRole).toInt();
    auto vfractions = item->data(4, BlockchainModel::FractionsRole);
    auto fractions = vfractions.value<CFractions>();

    QPen nopen(Qt::NoPen);
    QPen pegpen;
    pegpen.setStyle(Qt::DotLine);
    pegpen.setWidth(1);
    pegpen.setColor(QColor(128,0,0));
    
    curveReserve = new QwtPlotCurve;
    curveReserve->setPen(nopen);
    curveReserve->setBrush(QColor("#c06a15"));
    curveReserve->setRenderHint(QwtPlotItem::RenderAntialiased);
    curveReserve->attach(fplot);

    curveLiquid = new QwtPlotCurve;
    curveLiquid->setPen(nopen);
    curveLiquid->setBrush(QColor("#2da5e0"));
    curveLiquid->setRenderHint(QwtPlotItem::RenderAntialiased);
    curveLiquid->attach(fplot);

    curvePeg = new QwtPlotCurve;
    curvePeg->setPen(pegpen);
    curvePeg->setRenderHint(QwtPlotItem::RenderAntialiased);
    curvePeg->attach(fplot);
    
    CPegLevel level("");
    level.nCycle = cycle;
    level.nCyclePrev = cycle-1;
    level.nSupply = supply;
    level.nSupplyNext = supplyn;
    level.nSupplyNextNext = supplynn;
    plotFractions(ui.fractions, fractions, level);
    
    dlg->setWindowTitle(txhash+" "+tr("fractions"));
    dlg->show();
}

void TxDetailsWidget::plotFractions(QTreeWidget * table, 
                                    const CFractions & fractions,
                                    const CPegLevel & level,
                                    int64_t nLiquidSave,
                                    int64_t nReserveSave,
                                    int64_t nID)
{
    if (!table) return;
    QWidget * top = table->topLevelWidget();
    if (!top) return;
    QLabel * packedLabel = top->findChild<QLabel*>("packedLabel");
    QLabel * valueLabel = top->findChild<QLabel*>("valueLabel");
    QLabel * reserveLabel = top->findChild<QLabel*>("reserveLabel");
    QLabel * liquidityLabel = top->findChild<QLabel*>("liquidityLabel");

    table->clear();
    
    unsigned long len_test = 0;
    CDataStream fout_test(SER_DISK, CLIENT_VERSION);
    fractions.Pack(fout_test, &len_test);
    if (packedLabel) packedLabel->setText(tr("Packed: %1 bytes").arg(len_test));
    if (valueLabel) valueLabel->setText(tr("Value: %1").arg(displayValue(fractions.Total())));
    if (reserveLabel) reserveLabel->setText(tr("Reserve: %1").arg(displayValue(fractions.Low(level))));
    if (liquidityLabel) liquidityLabel->setText(tr("Liquidity: %1").arg(displayValue(fractions.High(level))));
    
    auto fractions_std = fractions.Std();

    qreal y_min = 0;
    qreal y_max = 0;
    qreal xs_reserve[PEG_SIZE*2];
    qreal ys_reserve[PEG_SIZE*2];
    qreal xs_liquidity[PEG_SIZE*2];
    qreal ys_liquidity[PEG_SIZE*2];

    int supply = 0;
    if (!level.IsValid()) {
        supply = level.nSupply;
    } else {
        supply = level.nSupply + level.nShift;
    }
    
    bool simple = nLiquidSave < 0;

    QStringList row_cycle;
    row_cycle << tr("Cycle") << QString::number(level.nCycle);
    auto row_item_cycle = new QTreeWidgetItem(row_cycle);
    row_item_cycle->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
    row_item_cycle->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
    row_item_cycle->setFlags(row_item_cycle->flags() | Qt::ItemIsEditable );
    table->addTopLevelItem(row_item_cycle);

    if (!simple) {
        QStringList row_cyclep;
        row_cyclep << tr("Prev") << QString::number(level.nCyclePrev);
        auto row_item_cyclep = new QTreeWidgetItem(row_cyclep);
        row_item_cyclep->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
        row_item_cyclep->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        row_item_cyclep->setFlags(row_item_cycle->flags() | Qt::ItemIsEditable );
        table->addTopLevelItem(row_item_cyclep);
    }
    
    QStringList row_peg;
    row_peg << tr("Peg") << QString::number(level.nSupply);
    auto row_item_peg = new QTreeWidgetItem(row_peg);
    row_item_peg->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
    row_item_peg->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
    table->addTopLevelItem(row_item_peg);
    
    if (!simple) {
        QStringList row_pegn;
        row_pegn << tr("PegN") << QString::number(level.nSupplyNext);
        auto row_item_pegn = new QTreeWidgetItem(row_pegn);
        row_item_pegn->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
        row_item_pegn->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        table->addTopLevelItem(row_item_pegn);
    
        QStringList row_pegnn;
        row_pegnn << tr("PegNN") << QString::number(level.nSupplyNextNext);
        auto row_item_pegnn = new QTreeWidgetItem(row_pegnn);
        row_item_pegnn->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
        row_item_pegnn->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        table->addTopLevelItem(row_item_pegnn);

        QStringList row_buf;
        row_buf << tr("Buffer") << QString::number(level.nBuffer);
        auto row_item_buf = new QTreeWidgetItem(row_buf);
        row_item_buf->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
        row_item_buf->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        table->addTopLevelItem(row_item_buf);
    
        QStringList row_shift;
        row_shift << tr("Shift") << QString::number(level.nShift);
        auto row_item_shift = new QTreeWidgetItem(row_shift);
        row_item_shift->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
        row_item_shift->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        table->addTopLevelItem(row_item_shift);
    
        QStringList row_part;
        row_part << tr("Part") << displayValue(level.nShiftLastPart);
        auto row_item_part = new QTreeWidgetItem(row_part);
        row_item_part->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
        row_item_part->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        row_item_part->setData(1, BlockchainModel::ValueForCopy, qlonglong(level.nShiftLastPart));
        table->addTopLevelItem(row_item_part);
    
        QStringList row_ptot;
        row_ptot << tr("PTot") << displayValue(level.nShiftLastTotal);
        auto row_item_ptot = new QTreeWidgetItem(row_ptot);
        row_item_ptot->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
        row_item_ptot->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        row_item_ptot->setData(1, BlockchainModel::ValueForCopy, qlonglong(level.nShiftLastTotal));
        table->addTopLevelItem(row_item_ptot);
    }
    
    QStringList row_value;
    row_value << tr("V") << displayValue(fractions.Total());
    auto row_item_value = new QTreeWidgetItem(row_value);
    row_item_value->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
    row_item_value->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
    row_item_value->setData(1, BlockchainModel::ValueForCopy, qlonglong(fractions.Total()));
    table->addTopLevelItem(row_item_value);

    if (!simple) {
        QStringList row_liquid_calc;
        row_liquid_calc << tr("Lcalc") << displayValue(fractions.High(level));
        auto row_item_liquid_calc = new QTreeWidgetItem(row_liquid_calc);
        row_item_liquid_calc->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
        row_item_liquid_calc->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        row_item_liquid_calc->setData(1, BlockchainModel::ValueForCopy, qlonglong(fractions.High(level)));
        table->addTopLevelItem(row_item_liquid_calc);
    
        QStringList row_reserve_calc;
        row_reserve_calc << tr("Rcalc") << displayValue(fractions.Low(level));
        auto row_item_reserve_calc = new QTreeWidgetItem(row_reserve_calc);
        row_item_reserve_calc->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
        row_item_reserve_calc->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        row_item_reserve_calc->setData(1, BlockchainModel::ValueForCopy, qlonglong(fractions.Low(level)));
        table->addTopLevelItem(row_item_reserve_calc);
    
        QStringList row_liquid_save;
        row_liquid_save << tr("Lsave") << (nLiquidSave <0 ? "" : displayValue(nLiquidSave));
        auto row_item_liquid_save = new QTreeWidgetItem(row_liquid_save);
        row_item_liquid_save->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
        row_item_liquid_save->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        row_item_liquid_save->setData(1, BlockchainModel::ValueForCopy, qlonglong(nLiquidSave));
        table->addTopLevelItem(row_item_liquid_save);
    
        QStringList row_reserve_save;
        row_reserve_save << tr("Rsave") << (nReserveSave <0 ? "" : displayValue(nReserveSave));
        auto row_item_reserve_save = new QTreeWidgetItem(row_reserve_save);
        row_item_reserve_save->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
        row_item_reserve_save->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        row_item_reserve_save->setData(1, BlockchainModel::ValueForCopy, qlonglong(nReserveSave));
        table->addTopLevelItem(row_item_reserve_save);
        
        QStringList row_id;
        row_id << tr("ID") << QString::number(nID);
        auto row_item_id = new QTreeWidgetItem(row_id);
        row_item_id->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
        row_item_id->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        row_item_id->setData(1, BlockchainModel::ValueForCopy, qlonglong(nID));
        table->addTopLevelItem(row_item_id);
    } else {
        QStringList row_liquid_calc;
        row_liquid_calc << tr("L") << displayValue(fractions.High(level));
        auto row_item_liquid_calc = new QTreeWidgetItem(row_liquid_calc);
        row_item_liquid_calc->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
        row_item_liquid_calc->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        row_item_liquid_calc->setData(1, BlockchainModel::ValueForCopy, qlonglong(fractions.High(level)));
        table->addTopLevelItem(row_item_liquid_calc);
    
        QStringList row_reserve_calc;
        row_reserve_calc << tr("R") << displayValue(fractions.Low(level));
        auto row_item_reserve_calc = new QTreeWidgetItem(row_reserve_calc);
        row_item_reserve_calc->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
        row_item_reserve_calc->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        row_item_reserve_calc->setData(1, BlockchainModel::ValueForCopy, qlonglong(fractions.Low(level)));
        table->addTopLevelItem(row_item_reserve_calc);
    }
    
    QStringList row_vhli;
    row_vhli << tr("HLI") << QString::number(fractions.HLI());
    auto row_item_vhli = new QTreeWidgetItem(row_vhli);
    row_item_vhli->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
    row_item_vhli->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
    row_item_vhli->setData(1, BlockchainModel::ValueForCopy, qlonglong(fractions.HLI()));
    table->addTopLevelItem(row_item_vhli);
    
    QStringList row_lhli;
    row_lhli << tr("LHLI") << QString::number(fractions.HighPart(level, nullptr).HLI());
    auto row_item_lhli = new QTreeWidgetItem(row_lhli);
    row_item_lhli->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
    row_item_lhli->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
    row_item_lhli->setData(1, BlockchainModel::ValueForCopy, qlonglong(fractions.HighPart(level, nullptr).HLI()));
    table->addTopLevelItem(row_item_lhli);
    
    QStringList row_rhli;
    row_rhli << tr("RHLI") << QString::number(fractions.LowPart(level, nullptr).HLI());
    auto row_item_rhli = new QTreeWidgetItem(row_rhli);
    row_item_rhli->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignLeft));
    row_item_rhli->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
    row_item_rhli->setData(1, BlockchainModel::ValueForCopy, qlonglong(fractions.LowPart(level, nullptr).HLI()));
    table->addTopLevelItem(row_item_rhli);
    
    QStringList row_space;
    auto row_item_space = new QTreeWidgetItem(row_space);
    table->addTopLevelItem(row_item_space);
    
    if (fractions.nFlags & CFractions::NOTARY_F ||
        fractions.nFlags & CFractions::NOTARY_V ||
        fractions.nFlags & CFractions::NOTARY_C) {
        
        if (fractions.nFlags & CFractions::NOTARY_F) {
            QStringList row_cols_f;
            row_cols_f << tr("Mark") << tr("Frozen");
            auto row_f = new QTreeWidgetItem(row_cols_f);
            table->addTopLevelItem(row_f);
            
            QStringList row_cols_unlock;
            row_cols_unlock << tr("Lock") << QString::number(fractions.nLockTime);
            auto row_unlock = new QTreeWidgetItem(row_cols_unlock);
            table->addTopLevelItem(row_unlock);
        }
        
        if (fractions.nFlags & CFractions::NOTARY_V) {
            QStringList row_cols_v;
            row_cols_v << tr("Mark") << tr("VFrozen");
            auto row_v = new QTreeWidgetItem(row_cols_v);
            table->addTopLevelItem(row_v);
            
            QStringList row_cols_unlock;
            row_cols_unlock << tr("Lock") << QString::number(fractions.nLockTime);
            auto row_unlock = new QTreeWidgetItem(row_cols_unlock);
            table->addTopLevelItem(row_unlock);
        }
        
        if (fractions.nFlags & CFractions::NOTARY_C) {
            QStringList row_cols_c;
            row_cols_c << tr("Mark") << tr("Cold");
            auto row_c = new QTreeWidgetItem(row_cols_c);
            table->addTopLevelItem(row_c);
            
            QStringList row_cols_return;
            row_cols_return << tr("Back") << QString::fromStdString(fractions.sReturnAddr);
            auto row_return = new QTreeWidgetItem(row_cols_return);
            table->addTopLevelItem(row_return);
        } else if (!fractions.sReturnAddr.empty()) {
            QStringList row_cols_note;
            row_cols_note << tr("Extra") << QString::fromStdString(fractions.sReturnAddr);
            auto row_return = new QTreeWidgetItem(row_cols_note);
            table->addTopLevelItem(row_return);
        }
        
        auto row_item_space = new QTreeWidgetItem(row_space);
        table->addTopLevelItem(row_item_space);
    } 
    else if (!fractions.sReturnAddr.empty()) {
        QStringList row_cols_note;
        row_cols_note << tr("Extra") << QString::fromStdString(fractions.sReturnAddr);
        auto row_return = new QTreeWidgetItem(row_cols_note);
        table->addTopLevelItem(row_return);
        
        auto row_item_space = new QTreeWidgetItem(row_space);
        table->addTopLevelItem(row_item_space);
    }
    
    if (supply <0) {
        supply = 0;
    }
    if (supply > nPegMaxSupplyIndex) {
        supply = nPegMaxSupplyIndex;
    }
    
    
    for (int i=0; i<PEG_SIZE; i++) {
        QStringList row;
        row << QString::number(i) << displayValue(fractions_std.f[i]);
        auto row_item = new QTreeWidgetItem(row);
        row_item->setData(0, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        row_item->setData(1, Qt::TextAlignmentRole, int(Qt::AlignVCenter | Qt::AlignRight));
        row_item->setData(1, BlockchainModel::ValueForCopy, qlonglong(fractions_std.f[i]));
        table->addTopLevelItem(row_item);

        xs_reserve[i*2] = i;
        ys_reserve[i*2] = i < supply ? qreal(fractions_std.f[i]) : 0;
        xs_reserve[i*2+1] = i+1;
        ys_reserve[i*2+1] = ys_reserve[i*2];

        xs_liquidity[i*2] = i;
        ys_liquidity[i*2] = i >= supply ? qreal(fractions_std.f[i]) : 0;
        xs_liquidity[i*2+1] = i+1;
        ys_liquidity[i*2+1] = ys_liquidity[i*2];
        
        y_min = qMin(y_min, qreal(fractions_std.f[i]));
        y_max = qMax(y_max, qreal(fractions_std.f[i]));
    }
    
    qreal xs_peg[2];
    qreal ys_peg[2];
    xs_peg[0] = supply;
    xs_peg[1] = supply;
    ys_peg[0] = y_min;
    ys_peg[1] = y_max;

    curvePeg->setSamples(xs_peg, 
                         ys_peg, 
                         2);
    curveReserve->setSamples(xs_reserve, 
                             ys_reserve, 
                             supply*2);
    curveLiquid->setSamples(xs_liquidity+supply*2,
                            ys_liquidity+supply*2,
                            PEG_SIZE*2-supply*2);
    fplot->replot();
}

void TxDetailsWidget::copyPegData(QTreeWidget * table)
{
    if (!table) return;
    int n = table->topLevelItemCount();
    
    CPegData pegdata;
    pegdata.fractions = pegdata.fractions.Std();
    
    for(int i=0; i<n; i++) {
        QString name = table->topLevelItem(i)->text(0);
        QString value = table->topLevelItem(i)->text(1);
        value = value.replace(".", "");
        value = value.replace(",", "");
        
        if (name == tr("Cycle")) pegdata.peglevel.nCycle = value.toInt();
        if (name == tr("Prev")) pegdata.peglevel.nCyclePrev = value.toInt();
        if (name == tr("Peg")) pegdata.peglevel.nSupply = value.toInt();
        if (name == tr("PegN")) pegdata.peglevel.nSupplyNext = value.toInt();
        if (name == tr("PegNN")) pegdata.peglevel.nSupplyNextNext = value.toInt();
        if (name == tr("Buffer")) pegdata.peglevel.nBuffer = value.toInt();
        if (name == tr("Shift")) pegdata.peglevel.nShift = value.toInt();
        if (name == tr("Part")) pegdata.peglevel.nShiftLastPart = value.toInt();
        if (name == tr("PTot")) pegdata.peglevel.nShiftLastTotal = value.toInt();
        if (name == tr("Lsave")) pegdata.nLiquid = value.toLongLong();
        if (name == tr("Rsave")) pegdata.nReserve = value.toLongLong();
        
        bool isIndex = false;
        int index = name.toInt(&isIndex);
        if (isIndex && index >=0 && index < PEG_SIZE) {
            pegdata.fractions.f[index] = value.toLongLong();
        }
        qDebug() << pegdata.fractions.Total();
    }
    
    QString b64 = QString::fromStdString(pegdata.ToString());
    QApplication::clipboard()->setText(b64);
}

void TxDetailsWidget::openFractionsMenu(const QPoint & pos)
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
    {
        QString text = QApplication::clipboard()->text();
        
        try {
            CPegData pegdata(text.toStdString());
            if (pegdata.IsValid()) {
                a = m.addAction(tr("Paste pegdata"));
                connect(a, &QAction::triggered, [&] {
                    QString text = QApplication::clipboard()->text();
                    CPegData pegdata(text.toStdString());
                    
                    plotFractions(table, 
                                  pegdata.fractions, 
                                  pegdata.peglevel, 
                                  pegdata.nLiquid, 
                                  pegdata.nReserve,
                                  pegdata.nId);
                });
                a = m.addAction(tr("Copy pegdata"));
                connect(a, &QAction::triggered, [&] {
                    copyPegData(table);
                });
            }
        }catch (std::exception &) { ; }
    }
    m.exec(table->viewport()->mapToGlobal(pos));
}

bool FractionsDialogEvents::eventFilter(QObject *obj, QEvent *event)
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
