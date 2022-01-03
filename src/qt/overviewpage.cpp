#include "overviewpage.h"
#include "ui_overviewpage.h"
#include "ui_frozeninfodialog.h"

#include "clientmodel.h"
#include "walletmodel.h"
#include "bitcoinunits.h"
#include "optionsmodel.h"
#include "transactiontablemodel.h"
#include "transactionfilterproxy.h"
#include "guiutil.h"
#include "guiconstants.h"
#include "peg.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QTimer>

void TxViewDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
					  const QModelIndex &index ) const
{
	painter->save();

	QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
	QRect mainRect = option.rect;
//        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
//        int xspace = DECORATION_SIZE + 8;
	int xspace = 0;
	int ypad = 6;
	//int halfheight = (mainRect.height() - 2*ypad)/2;
	int halfheight = (mainRect.height() - 2*ypad);
	QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
	//QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
	//icon.paint(painter, decorationRect);

	QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
	QString address = index.data(Qt::DisplayRole).toString();
	qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
	bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
	QVariant value = index.data(Qt::ForegroundRole);
	QColor foreground = option.palette.color(QPalette::Text);
	if(qVariantCanConvert<QColor>(value))
	{
		foreground = qvariant_cast<QColor>(value);
	}

	painter->setPen(foreground);
	//painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address);

	if(amount < 0)
	{
		foreground = COLOR_NEGATIVE;
	}
	else if(!confirmed)
	{
		foreground = COLOR_UNCONFIRMED;
	}
	else
	{
		foreground = option.palette.color(QPalette::Text);
	}
	painter->setPen(foreground);
	QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true);
	if(!confirmed)
	{
		amountText = QString("[") + amountText + QString("]");
	}
	QFont f = painter->font();
	QFont fb = f;
	fb.setBold(true);
	painter->setFont(fb);
	painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

	painter->setFont(f);
	painter->setPen(QPen("#666666"));
	painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

	painter->restore();
}

OverviewPage::OverviewPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentReserve(-1),
    currentLiquidity(-1),
    currentFrozen(-1),
    currentStake(0),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    txdelegate(new TxViewDelegate()),
    filter(0)
{
    ui->setupUi(this);
    GUIUtil::SetBitBayFonts(this);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight((NUM_ITEMS * (DECORATION_SIZE + 2))/2 +100);
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

#ifdef Q_OS_MAC
    QFont hfont("Roboto Black", 20, QFont::Bold);
#else
    QFont hfont("Roboto Black", 15, QFont::Bold);
#endif

    ui->labelWallet->setFont(hfont);
    ui->labelRecent->setFont(hfont);

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
            padding-right:3px;
        }
    )";

    ui->w_recent->setStyleSheet(white1);

    ui->labelBalanceText        ->setStyleSheet(white2);
    ui->labelReserveText        ->setStyleSheet(white2);
    ui->labelFrozenText         ->setStyleSheet(white2);
    ui->labelStakeText          ->setStyleSheet(white2);
    ui->labelUnconfirmedText    ->setStyleSheet(white2);
    ui->labelImmatureText       ->setStyleSheet(white2);

#ifdef Q_OS_MAC
    QFont tfont("Roboto", 15, QFont::Bold);
#else
    QFont tfont("Roboto", 11, QFont::Bold);
#endif
    ui->labelTotalText->setFont(tfont);
    ui->labelTotalText          ->setStyleSheet(white1);

    ui->labelBalance            ->setStyleSheet(white1);
    ui->labelReserve            ->setStyleSheet(white1);
    ui->labelFrozen             ->setStyleSheet(white1);
    ui->labelStake              ->setStyleSheet(white1);
    ui->labelUnconfirmed        ->setStyleSheet(white1);
    ui->labelImmature           ->setStyleSheet(white1);
    ui->labelTotal              ->setStyleSheet(white1);

    connect(ui->labelFrozenText, SIGNAL(linkActivated(QString)), this, SLOT(openFrozenCoinsInfo()));
    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // init "out of sync" warning labels
    ui->labelWalletStatus->setText("(" + tr("out of sync") + ")");
    ui->labelTransactionsStatus->setText("(" + tr("out of sync") + ")");

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);

}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        emit transactionClicked(filter->mapToSource(index));
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(qint64 balance, 
                              qint64 reserve, qint64 liquidity, qint64 frozen,
                              vector<CFrozenCoinInfo> frozenCoins,
                              qint64 stake, qint64 unconfirmedBalance, qint64 immatureBalance)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentReserve = reserve;
    currentLiquidity = liquidity;
    currentFrozen = frozen;
    currentFrozenCoins = frozenCoins;
    currentStake = stake;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    ui->labelReserve->setText(BitcoinUnits::formatWithUnitForLabel(unit, reserve));
    ui->labelBalance->setText(BitcoinUnits::formatWithUnitForLabel(unit, liquidity));
    ui->labelFrozen->setText(BitcoinUnits::formatWithUnitForLabel(unit, frozen));
    ui->labelStake->setText(BitcoinUnits::formatWithUnitForLabel(unit, stake));
    ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnitForLabel(unit, unconfirmedBalance));
    ui->labelImmature->setText(BitcoinUnits::formatWithUnitForLabel(unit, immatureBalance));
    ui->labelTotal->setText(BitcoinUnits::formatWithUnitForLabel(unit, balance + stake + unconfirmedBalance + immatureBalance));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    ui->labelImmature->setVisible(showImmature);
    ui->labelImmatureText->setVisible(showImmature);
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
        QTimer* alertsTimer = new QTimer(this);
        alertsTimer->setInterval(1000*60*5); // 5 min
        alertsTimer->start();
        connect(alertsTimer, SIGNAL(timeout()), this, SLOT(updateAlerts()));
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter = new TransactionFilterProxy();
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setFilterRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Status, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter);
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        vector<CFrozenCoinInfo> frozenCoins;
        int64_t nFrozen = model->getFrozen(NULL, &frozenCoins);
        setBalance(model->getBalance(), 
                   model->getReserve(), model->getLiquidity(), nFrozen, frozenCoins,
                   model->getStake(), model->getUnconfirmedBalance(), model->getImmatureBalance());
        connect(model, SIGNAL(balanceChanged(qint64, qint64, qint64, qint64, std::vector<CFrozenCoinInfo>, qint64, qint64, qint64)), 
                this, SLOT(setBalance(qint64, qint64, qint64, qint64, std::vector<CFrozenCoinInfo>, qint64, qint64, qint64)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if(currentBalance != -1)
            setBalance(currentBalance, currentReserve, currentLiquidity, currentFrozen, currentFrozenCoins,
                       walletModel->getStake(), currentUnconfirmedBalance, currentImmatureBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts()
{
    if (clientModel)
        updateAlerts(clientModel->getStatusBarWarnings());
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

static QString timeBehindText(int secs) 
{
    QString time_behind_text;
    const int MINUTE_IN_SECONDS = 60;
    const int HOUR_IN_SECONDS = 60*60;
    const int DAY_IN_SECONDS = 24*60*60;
    const int WEEK_IN_SECONDS = 7*24*60*60;
    const int YEAR_IN_SECONDS = 31556952; // Average length of year in Gregorian calendar
   
    if(secs < 2*MINUTE_IN_SECONDS) 
    {
        time_behind_text = QObject::tr("%n second(s)","",secs);
    }
    else if(secs < 2*HOUR_IN_SECONDS) 
    {
        time_behind_text = QObject::tr("%n minute(s)","",secs/MINUTE_IN_SECONDS);
    }
    else if(secs < 2*DAY_IN_SECONDS)
    {
        time_behind_text = QObject::tr("%n hour(s)","",secs/HOUR_IN_SECONDS);
    }
    else if(secs < 2*WEEK_IN_SECONDS)
    {
        time_behind_text = QObject::tr("%n day(s)","",secs/DAY_IN_SECONDS);
    }
    else if(secs < YEAR_IN_SECONDS)
    {
        time_behind_text = QObject::tr("%n week(s)","",secs/WEEK_IN_SECONDS);
    }
    else
    {
        int years = secs / YEAR_IN_SECONDS;
        int remainder = secs % YEAR_IN_SECONDS;
        time_behind_text = QObject::tr("%1 and %2")
                .arg(QObject::tr("%n year(s)", "", years))
                .arg(QObject::tr("%n week(s)","", remainder/WEEK_IN_SECONDS));
    }
    return time_behind_text;
}

void OverviewPage::openFrozenCoinsInfo()
{
    QDialog dlg(this);
    Ui::FrozenInfoDialog ui;
    ui.setupUi(&dlg);
    ui.frozen->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui.frozen->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui.frozen->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    vector<CFrozenCoinInfo> frozenCoins;
    walletModel->getFrozen(NULL, &frozenCoins);
    for (const auto & coin : frozenCoins) {
        QStringList cells;
        int secs = coin.nLockTime - QDateTime::currentDateTimeUtc().toTime_t();
        cells << BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), coin.nValue);
        cells << QString::fromStdString(DateTimeStrFormat(coin.nLockTime));
        cells << timeBehindText(secs);
        QTreeWidgetItem * twi = new QTreeWidgetItem(cells);
        auto f = twi->font(0);
        f.setBold(true);
        twi->setFont(0, f);
        ui.frozen->addTopLevelItem(twi);
    }
    dlg.exec();
}
