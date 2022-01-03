// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "stakingpage.h"
#include "ui_stakingpage.h"

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


StakingPage::StakingPage(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StakingPage)
{
    ui->setupUi(this);
    GUIUtil::SetBitBayFonts(this);
    
    setStyleSheet("QRadioButton { background: none; }");

#ifdef Q_OS_MAC
    QFont tfont("Roboto", 15, QFont::Bold);
#else
    QFont tfont("Roboto", 11, QFont::Bold);
#endif
    ui->labelRewards->setFont(tfont);
    
    QString white1 = R"(
        QWidget {
            background-color: rgb(255,255,255);
            padding-left: 10px;
            padding-right:3px;
        }
    )";
    QString white2 = R"(
        QWidget {
            color: rgb(102,102,102);
            background-color: rgb(255,255,255);
            padding-left: 10px;
            padding-right:10px;
        }
    )";

    ui->labelRewards->setStyleSheet(white1);
    
    ui->labelTotalText->setFont(tfont);
    ui->labelTotalText->setStyleSheet(white1);
    
    
    ui->label5Text      ->setStyleSheet(white2);
    ui->label10Text     ->setStyleSheet(white2);
    ui->label20Text     ->setStyleSheet(white2);
    ui->label40Text     ->setStyleSheet(white2);

    ui->label5Count     ->setStyleSheet(white1);
    ui->label10Count    ->setStyleSheet(white1);
    ui->label20Count    ->setStyleSheet(white1);
    ui->label40Count    ->setStyleSheet(white1);
    ui->labelTotalCount ->setStyleSheet(white1);
    
    ui->label5Amount    ->setStyleSheet(white1);
    ui->label10Amount   ->setStyleSheet(white1);
    ui->label20Amount   ->setStyleSheet(white1);
    ui->label40Amount   ->setStyleSheet(white1);
    ui->labelTotalAmount->setStyleSheet(white1);
    
    pollTimer = new QTimer(this);
    pollTimer->setInterval(30*1000);
    pollTimer->start();
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(updateTimer()));
    
    ui->lineRewardTo->setPlaceholderText(tr("Enter a BitBay address:"));
    ui->lineSupportTo->setPlaceholderText(tr("Enter a BitBay address:"));

    setFocusPolicy(Qt::TabFocus);
    GUIUtil::setupAddressWidget(ui->lineRewardTo, this);
    GUIUtil::setupAddressWidget(ui->lineSupportTo, this);
    
    connect(ui->checkRewardTo, SIGNAL(toggled(bool)), this, SLOT(updateRewardActions()));
    connect(ui->checkSupportFund, SIGNAL(toggled(bool)), this, SLOT(updateRewardActions()));
    connect(ui->checkConsolidateCoins, SIGNAL(toggled(bool)), this, SLOT(updateRewardActions()));
    connect(ui->lineRewardTo, SIGNAL(textChanged(const QString &)), this, SLOT(updateRewardActions()));
    connect(ui->lineSupportTo, SIGNAL(editingFinished()), this, SLOT(updateRewardActions()));
    connect(ui->sliderSupport, SIGNAL(valueChanged(int)), this, SLOT(updateRewardActions()));
    
    connect(ui->rewardAddressBookButton, SIGNAL(clicked(bool)), this, SLOT(addressBookRewardClicked()));
    connect(ui->supportAddressBookButton, SIGNAL(clicked(bool)), this, SLOT(addressBookSupportClicked()));
}

StakingPage::~StakingPage()
{
    delete ui;
}

extern double GetPoSKernelPS();

void StakingPage::updateTimer()
{
    uint64_t nWeight = 0;
    {
        if (!pwalletMain)
            return;
    
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain)
            return;
    
        TRY_LOCK(pwalletMain->cs_wallet, lockWallet);
        if (!lockWallet)
            return;
    
        nWeight = pwalletMain->GetStakeWeight();
    }
    
    if (nLastCoinStakeSearchInterval && nWeight)
    {
        uint64_t nNetworkWeight = GetPoSKernelPS();
        unsigned nEstimateTime = GetTargetSpacing(nBestHeight) * nNetworkWeight / nWeight;

        QString text;
        if (nEstimateTime < 60)
        {
            text = tr("%n second(s)", "", nEstimateTime);
        }
        else if (nEstimateTime < 60*60)
        {
            text = tr("%n minute(s)", "", nEstimateTime/60);
        }
        else if (nEstimateTime < 24*60*60)
        {
            text = tr("%n hour(s)", "", nEstimateTime/(60*60));
        }
        else
        {
            text = tr("%n day(s)", "", nEstimateTime/(60*60*24));
        }

        nWeight /= COIN;
        nNetworkWeight /= COIN;

        ui->labelText->setText(tr("Staking.<br>Your weight is %1<br>Network weight is %2<br>Expected time to earn reward is %3").arg(nWeight).arg(nNetworkWeight).arg(text));
    }
    else
    {
        if (pwalletMain && pwalletMain->IsLocked())
            ui->labelText->setText(tr("Not staking because wallet is locked"));
        else if (vNodes.empty())
            ui->labelText->setText(tr("Not staking because wallet is offline"));
        else if (IsInitialBlockDownload())
            ui->labelText->setText(tr("Not staking because wallet is syncing"));
        else if (!nWeight)
            ui->labelText->setText(tr("Not staking because you don't have mature coins"));
        else
            ui->labelText->setText(tr("Not staking"));
    }
}

void StakingPage::updateRewardActions()
{
    QString rewardAddress;
    if (ui->checkRewardTo->isChecked()) {
        rewardAddress = ui->lineRewardTo->text();
    }
    else {
        ui->lineRewardTo->clear();
    }
    
    pwalletMain->SetRewardAddress(rewardAddress.toStdString(), true/*write*/);
    
    QString supportAddress;
    if (ui->checkSupportFund->isChecked()) {
        supportAddress = ui->lineSupportTo->text();
        if (supportAddress.isEmpty()) { // read default
            QString supportAddr = QString::fromStdString(pwalletMain->GetSupportAddress());
            ui->lineSupportTo->setText(supportAddr);
        }
    }
    else {
        ui->lineSupportTo->clear();
    }
    
    pwalletMain->SetSupportEnabled(ui->checkSupportFund->isChecked(), true/*write*/);
    pwalletMain->SetSupportAddress(supportAddress.toStdString(), true/*write*/);
    pwalletMain->SetSupportPart(ui->sliderSupport->value(), true/*write*/);
    pwalletMain->SetConsolidateEnabled(ui->checkConsolidateCoins->isChecked(), true/*write*/);
}

void StakingPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        vector<RewardInfo> vRewardsInfo;
        vRewardsInfo.push_back({PEG_REWARD_5 ,0,0,0});
        vRewardsInfo.push_back({PEG_REWARD_10,0,0,0});
        vRewardsInfo.push_back({PEG_REWARD_20,0,0,0});
        vRewardsInfo.push_back({PEG_REWARD_40,0,0,0});
        model->getRewardInfo(vRewardsInfo);
        
        // Keep up to date with wallet
        setAmounts(vRewardsInfo[PEG_REWARD_5 ].amount,
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
        connect(model, SIGNAL(rewardsInfoChanged(qint64,qint64,qint64,qint64, int,int,int,int, int,int,int,int)), 
                this, SLOT(setAmounts(qint64,qint64,qint64,qint64, int,int,int,int, int,int,int,int)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        
        QString rewardAddr = QString::fromStdString(pwalletMain->GetRewardAddress());
        QString supportAddr = QString::fromStdString(pwalletMain->GetSupportAddress());
        int supportPart = pwalletMain->GetSupportPart();
        bool supportIsOn = pwalletMain->GetSupportEnabled();
        bool consolidateIsOn = pwalletMain->GetConsolidateEnabled();
        
        ui->checkConsolidateCoins->setChecked(consolidateIsOn);
        ui->checkRewardTo->setChecked(!rewardAddr.isEmpty());
        ui->lineRewardTo->setText(rewardAddr);
        ui->checkSupportFund->setChecked(supportIsOn);
        ui->lineSupportTo->setText(supportIsOn ? supportAddr : "");
        ui->sliderSupport->setValue(supportPart);
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void StakingPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        vector<RewardInfo> vRewardsInfo;
        vRewardsInfo.push_back({PEG_REWARD_5 ,0,0,0});
        vRewardsInfo.push_back({PEG_REWARD_10,0,0,0});
        vRewardsInfo.push_back({PEG_REWARD_20,0,0,0});
        vRewardsInfo.push_back({PEG_REWARD_40,0,0,0});
        walletModel->getRewardInfo(vRewardsInfo);
        
        setAmounts(vRewardsInfo[PEG_REWARD_5 ].amount,
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

void StakingPage::setAmounts(qint64 amount5, qint64 amount10, qint64 amount20, qint64 amount40, 
                             int count5, int count10, int count20, int count40,
                             int stake5, int stake10, int stake20, int stake40)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    
    current5Amount  = amount5;
    current10Amount = amount10;
    current20Amount = amount20;
    current40Amount = amount40;

    current5Count  = count5;
    current10Count = count10;
    current20Count = count20;
    current40Count = count40;

    current5Stake  = stake5;
    current10Stake = stake10;
    current20Stake = stake20;
    current40Stake = stake40;
    
    ui->label5Amount->setText(BitcoinUnits::formatWithUnitForLabel(unit, amount5));
    ui->label5Count->setText(tr("(%1)").arg(count5));
    if (stake5)
        ui->label5Count->setText(tr("(%1/%2)").arg(stake5).arg(count5));
    
    ui->label10Amount->setText(BitcoinUnits::formatWithUnitForLabel(unit, amount10));
    ui->label10Count->setText(tr("(%1)").arg(count10));
    if (stake10)
        ui->label10Count->setText(tr("(%1/%2)").arg(stake10).arg(count10));
    
    ui->label20Amount->setText(BitcoinUnits::formatWithUnitForLabel(unit, amount20));
    ui->label20Count->setText(tr("(%1)").arg(count20));
    if (stake20)
        ui->label20Count->setText(tr("(%1/%2)").arg(stake20).arg(count20));
    
    ui->label40Amount->setText(BitcoinUnits::formatWithUnitForLabel(unit, amount40));
    ui->label40Count->setText(tr("(%1)").arg(count40));
    if (stake40)
        ui->label40Count->setText(tr("(%1/%2)").arg(stake40).arg(count40));
    
    int count = count5+count10+count20+count40;
    int stake = stake5+stake10+stake20+stake40;
    qint64 amount = amount5+amount10+amount20+amount40;
    
    ui->labelTotalAmount->setText(BitcoinUnits::formatWithUnitForLabel(unit, amount));
    ui->labelTotalCount->setText(tr("(%1)").arg(count));
    if (stake)
        ui->labelTotalCount->setText(tr("(%1/%2)").arg(stake).arg(count));
}

void StakingPage::addressBookRewardClicked()
{
    if(!walletModel)
        return;
    AddressBookPage::Tabs tab = AddressBookPage::ReceivingTab;
    AddressBookPage dlg(AddressBookPage::ForSending, tab, this);
    dlg.setModel(walletModel->getAddressTableModel());
    if(dlg.exec())
    {
        ui->lineRewardTo->setText(dlg.getReturnValue());
        ui->lineRewardTo->setFocus();
    }
}

void StakingPage::addressBookSupportClicked()
{
    if(!walletModel)
        return;
    AddressBookPage::Tabs tab = AddressBookPage::SendingTab;
    AddressBookPage dlg(AddressBookPage::ForSending, tab, this);
    dlg.setModel(walletModel->getAddressTableModel());
    if(dlg.exec())
    {
        ui->lineSupportTo->setText(dlg.getReturnValue());
        ui->lineSupportTo->setFocus();
    }
}

