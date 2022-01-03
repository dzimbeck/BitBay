// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef STAKINGPAGE_H
#define STAKINGPAGE_H

#include <QDialog>
#include <QDateTime>

namespace Ui {
    class StakingPage;
}
class WalletModel;

class StakingPage : public QDialog
{
    Q_OBJECT
public:
    explicit StakingPage(QWidget *parent = nullptr);
    ~StakingPage();
    
    void setWalletModel(WalletModel*);
    
public slots:
    void updateTimer();
    
private slots:
    void updateDisplayUnit();
    void updateRewardActions();
    void setAmounts(qint64 reward5, qint64 reward10, qint64 reward20, qint64 reward40, 
                    int count5, int count10, int count20, int count40,
                    int stake5, int stake10, int stake20, int stake40);
    
    void addressBookRewardClicked();
    void addressBookSupportClicked();
    
private:
    Ui::StakingPage *ui;
    QTimer* pollTimer;
    
    WalletModel* walletModel;
    
    qint64 current5Amount   =0;
    qint64 current10Amount  =0;
    qint64 current20Amount  =0;
    qint64 current40Amount  =0;

    quint32 current5Count   =0;
    quint32 current10Count  =0;
    quint32 current20Count  =0;
    quint32 current40Count  =0;

    quint32 current5Stake   =0;
    quint32 current10Stake  =0;
    quint32 current20Stake  =0;
    quint32 current40Stake  =0;
};

#endif // STAKINGPAGE_H
