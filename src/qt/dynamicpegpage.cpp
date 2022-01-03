// Copyright (c) 2020 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dynamicpegpage.h"
#include "ui_dynamicpegpage.h"

#include "main.h"
#include "init.h"
#include "base58.h"
#include "txdb.h"
#include "peg.h"
#include "guiutil.h"
#include "walletmodel.h"
#include "optionsmodel.h"
#include "bitcoinunits.h"
#include "addressbookpage.h"

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

#include "qwt/qwt_plot.h"
#include "qwt/qwt_plot_item.h"
#include "qwt/qwt_plot_curve.h"
#include "qwt/qwt_plot_barchart.h"
#include "qwt/qwt_date_scale_draw.h"

DynamicPegPage::DynamicPegPage(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DynamicPegPage)
{
    ui->setupUi(this);
    GUIUtil::SetBitBayFonts(this);
    
    setStyleSheet("QRadioButton { background: none; }");

#ifdef Q_OS_MAC
    QFont tfont("Roboto", 15, QFont::Bold);
#else
    QFont tfont("Roboto", 11, QFont::Bold);
#endif
    
//    QString white1 = R"(
//        QWidget {
//            background-color: rgb(255,255,255);
//            padding-left: 10px;
//            padding-right:3px;
//        }
//    )";
//    QString white2 = R"(
//        QWidget {
//            color: rgb(102,102,102);
//            background-color: rgb(255,255,255);
//            padding-left: 10px;
//            padding-right:10px;
//        }
//    )";
    
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
    ui->values->setStyleSheet(hstyle);
    ui->values->setFont(font);
    ui->values->header()->setFont(font);
    ui->values->header()->resizeSection(0, 150);
    
    pollTimer = new QTimer(this);
    pollTimer->setInterval(30*1000);
    pollTimer->start();
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(updateTimer()));
    
    ui->comboVotes->clear();
    ui->comboVotes->addItem(tr("Automatic (recommended)"), PEG_VOTE_AUTO);
    ui->comboVotes->addItem(tr("No change"), PEG_VOTE_NOCHANGE);
    ui->comboVotes->addItem(tr("Deflation"), PEG_VOTE_DEFLATE);
    ui->comboVotes->addItem(tr("Inflation"), PEG_VOTE_INFLATE);
    ui->comboVotes->addItem(tr("Disable"), PEG_VOTE_NONE);
    connect(ui->comboVotes, SIGNAL(currentIndexChanged(int)), this, SLOT(updatePegVoteType()));
    
    setFocusPolicy(Qt::TabFocus);
    
    fplot = new QwtPlot;
    QVBoxLayout *fvbox = new QVBoxLayout;
    fvbox->setMargin(5);
    fvbox->addWidget(fplot);
    ui->chart1->setStyleSheet("QWidget { background: #ffffff; }");
    ui->chart1->setLayout(fvbox);
    
    QPen nopen(Qt::NoPen);
    QPen pegpen;
    pegpen.setStyle(Qt::SolidLine);
    pegpen.setWidth(2);
    pegpen.setColor("#ff0000");
    
    curvePrice = new QwtPlotCurve;
    curvePrice->setPen(QColor("#2da5e0"));
    curvePrice->setRenderHint(QwtPlotItem::RenderAntialiased);
    curvePrice->attach(fplot);

    QPen floorpen;
    floorpen.setStyle(Qt::SolidLine);
    floorpen.setWidth(2.5);
    floorpen.setColor("#6905c6");
    
    curveFloor = new QwtPlotCurve;
    curveFloor->setPen(floorpen);
    curveFloor->setRenderHint(QwtPlotItem::RenderAntialiased);
    curveFloor->attach(fplot);

    QPen rangepen;
    rangepen.setStyle(Qt::DotLine);
    rangepen.setWidth(2.5);
    rangepen.setColor(QColor("#6905c6"));
    
    curveFloorMin = new QwtPlotCurve;
    curveFloorMin->setPen(rangepen);
    curveFloorMin->setRenderHint(QwtPlotItem::RenderAntialiased);
    curveFloorMin->attach(fplot);

    curveFloorMax = new QwtPlotCurve;
    curveFloorMax->setPen(rangepen);
    curveFloorMax->setRenderHint(QwtPlotItem::RenderAntialiased);
    curveFloorMax->attach(fplot);
    
    curvePeg = new QwtPlotCurve;
    curvePeg->setPen(pegpen);
    curvePeg->setRenderHint(QwtPlotItem::RenderAntialiased);
    curvePeg->setYAxis(QwtPlot::yRight);
    curvePeg->attach(fplot);
    
    fplot->enableAxis(QwtPlot::yRight, true);
    fplot->setAxisScale(QwtPlot::yRight, 0, 1200);
    
    QwtDateScaleDraw * scale = new QwtDateScaleDraw;
    fplot->setAxisScaleDraw(QwtPlot::xBottom, scale);
    
    ui->values->addTopLevelItem(new QTreeWidgetItem(QStringList({"Status",""})));
    ui->values->addTopLevelItem(new QTreeWidgetItem(QStringList({"Algorithm",""})));
    ui->values->addTopLevelItem(new QTreeWidgetItem(QStringList({"Algorithm URL",""})));
    ui->values->addTopLevelItem(new QTreeWidgetItem(QStringList({"Algorithm Chart",""})));
    ui->values->addTopLevelItem(new QTreeWidgetItem(QStringList({"Algorithm Vote",""})));
}

DynamicPegPage::~DynamicPegPage()
{
    delete ui;
}

void DynamicPegPage::updateTimer()
{
    if (!pwalletMain)
        return;
    auto voteType = pwalletMain->GetPegVoteType();
    if (voteType != lastPegVoteType) {
        lastPegVoteType = voteType;
        lastPegVoteTypeChanged = QDateTime::currentDateTime();
    }
    
    if (lastPegVoteType != PEG_VOTE_AUTO && 
            lastPegVoteType != PEG_VOTE_NONE &&
            lastPegVoteTypeChanged.secsTo(QDateTime::currentDateTime()) > ((5*24+12)*60*60)) {
        pwalletMain->SetPegVoteType(PEG_VOTE_AUTO);
        int auto_idx = ui->comboVotes->findData(PEG_VOTE_AUTO);
        ui->comboVotes->setCurrentIndex(auto_idx);
    }
}

void DynamicPegPage::updatePegVoteType()
{
    int peg_vote = ui->comboVotes->currentData().toInt();
    pwalletMain->SetPegVoteType(PegVoteType(peg_vote));
}

void DynamicPegPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        connect(model, SIGNAL(rewardsInfoChanged(qint64,qint64,qint64,qint64, int,int,int,int, int,int,int,int)), 
                this, SLOT(setAmounts()));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void DynamicPegPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        setAmounts();
    }
}

void DynamicPegPage::setAmounts()
{
    //int unit = walletModel->getOptionsModel()->getDisplayUnit();
//    double last_peak = pwalletMain->LastPeakPrice();
//    PegVoteType last_vote = pwalletMain->LastAutoVoteType();
//    if (last_vote == PEG_VOTE_DEFLATE)
//        ui->comboVotes->setItemText(0, tr("Automatic (recommended), deflation to %1").arg(last_peak));
//    else if (last_vote == PEG_VOTE_INFLATE)
//        ui->comboVotes->setItemText(0, tr("Automatic (recommended), inflation to %1").arg(last_peak));
//    else if (last_vote == PEG_VOTE_NOCHANGE)
//        ui->comboVotes->setItemText(0, tr("Automatic (recommended), no change to %1").arg(last_peak));
//    else if (last_vote == PEG_VOTE_NONE)
//        ui->comboVotes->setItemText(0, tr("Automatic (recommended), disabled"));
//    else 
//        ui->comboVotes->setItemText(0, tr("Automatic (recommended)"));
}

void DynamicPegPage::setStatusMessage(QString txt)
{
    auto twi = ui->values->topLevelItem(0); // status
    twi->setText(1, txt);
}

void DynamicPegPage::setAlgorithmInfo(QString name, QString url, QString chart)
{
    auto twi1 = ui->values->topLevelItem(1); // name
    twi1->setText(1, name);
    auto twi2 = ui->values->topLevelItem(2); // url
    twi2->setText(1, url);
    auto twi3 = ui->values->topLevelItem(3); // chart
    twi3->setText(1, chart);
}

void DynamicPegPage::setAlgorithmVote(QString vote, double bay_floor_in_usd)
{
    if (vote == "deflate")
        ui->comboVotes->setItemText(0, tr("Automatic (recommended), deflation to %1").arg(bay_floor_in_usd));
    else if (vote == "inflate")
        ui->comboVotes->setItemText(0, tr("Automatic (recommended), inflation to %1").arg(bay_floor_in_usd));
    else if (vote == "nochange")
        ui->comboVotes->setItemText(0, tr("Automatic (recommended), no change to %1").arg(bay_floor_in_usd));
    else if (vote == "disabled")
        ui->comboVotes->setItemText(0, tr("Automatic (recommended), disabled"));
    else 
        ui->comboVotes->setItemText(0, tr("Automatic (recommended)"));
    
    auto twi = ui->values->topLevelItem(4); // vote
    twi->setText(1, vote);
}
