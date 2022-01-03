#include "sendcoinsdialog.h"
#include "ui_sendcoinsdialog.h"

#include "walletmodel.h"
#include "addresstablemodel.h"
#include "addressbookpage.h"

#include "bitcoinunits.h"
#include "addressbookpage.h"
#include "optionsmodel.h"
#include "sendcoinsentry.h"
#include "guiutil.h"
#include "askpassphrasedialog.h"

#include "base58.h"
#include "coincontrol.h"
#include "coincontroldialog.h"
#include "peg.h"
#include "wallet.h"
#include "txdetailswidget.h"

#include <QMessageBox>
#include <QTextDocument>
#include <QScrollBar>
#include <QClipboard>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QDialogButtonBox>

SendCoinsDialog::SendCoinsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SendCoinsDialog),
    model(0)
{
    ui->setupUi(this);
    GUIUtil::SetBitBayFonts(this);

#ifdef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    ui->addButton->setIcon(QIcon());
    ui->clearButton->setIcon(QIcon());
    ui->sendButton->setIcon(QIcon());
#endif

    addEntry();

    connect(ui->addButton, SIGNAL(clicked()), this, SLOT(addEntry()));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));

    // Coin Control
    connect(ui->pushButtonCoinControl, SIGNAL(clicked()), this, SLOT(coinControlButtonClicked()));
    connect(ui->pushButtonTxPreview, SIGNAL(clicked()), this, SLOT(txPreviewButtonClicked()));

    // Coin Control: clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardPriorityAction = new QAction(tr("Copy priority"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy low output"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);
    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardBytes()));
    connect(clipboardPriorityAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardPriority()));
    connect(clipboardLowOutputAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardLowOutput()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardChange()));
    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);
    ui->labelCoinControlFee->addAction(clipboardFeeAction);
    ui->labelCoinControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelCoinControlBytes->addAction(clipboardBytesAction);
    ui->labelCoinControlPriority->addAction(clipboardPriorityAction);
    ui->labelCoinControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelCoinControlChange->addAction(clipboardChangeAction);

    fNewRecipientAllowed = true;
    
    QString white1 = R"(
        QWidget {
            padding-left: 3px;
            padding-right:3px;
        }
    )";
    QString white2 = R"(
        QWidget {
            color: rgb(102,102,102);
            padding-left: 3px;
            padding-right:3px;
        }
    )";

    ui->labelLiquidityText  ->setStyleSheet(white2);
    ui->labelReservesText   ->setStyleSheet(white2);
    ui->labelLiquidity      ->setStyleSheet(white1);
    ui->labelReserves       ->setStyleSheet(white1);
    
    ui->comboBoxTxType->clear();
    ui->comboBoxTxType->addItem(tr("Transfer spendable funds (liquidity)"), PEG_MAKETX_SEND_LIQUIDITY);
    ui->comboBoxTxType->addItem(tr("Transfer reserve and freeze it for 1 month"), PEG_MAKETX_SEND_RESERVE);
    ui->comboBoxTxType->addItem(tr("Freeze reserve for 1 month (such funds have 20 BAY staking reward)"), PEG_MAKETX_FREEZE_RESERVE);
    ui->comboBoxTxType->addItem(tr("Freeze liquidity for 4 month (such funds have 40 BAY staking reward)"), PEG_MAKETX_FREEZE_LIQUIDITY);
//    ui->comboBoxTxType->insertSeparator(4);
//    ui->comboBoxTxType->addItem(tr("Transfer coins to cold wallet"), PEG_MAKETX_SEND_TOCOLD);
//    ui->comboBoxTxType->addItem(tr("Return coins from cold wallet"), PEG_MAKETX_SEND_FROMCOLD);

    connect(ui->comboBoxTxType, SIGNAL(activated(int)), this, SLOT(clear()));
}

void SendCoinsDialog::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel())
    {
        for(int i = 0; i < ui->entries->count(); ++i)
        {
            SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
            if(entry)
            {
                entry->setModel(model);
            }
        }

        vector<CFrozenCoinInfo> frozenCoins;
        int64_t nFrozen = model->getFrozen(NULL, &frozenCoins);
        setBalance(model->getBalance(), 
                   model->getReserve(), model->getLiquidity(), nFrozen, frozenCoins,
                   model->getStake(), model->getUnconfirmedBalance(), model->getImmatureBalance());
        connect(model, SIGNAL(balanceChanged(qint64, qint64, qint64, qint64, std::vector<CFrozenCoinInfo>, qint64, qint64, qint64)),
                this, SLOT(setBalance(qint64, qint64, qint64, qint64, std::vector<CFrozenCoinInfo>, qint64, qint64, qint64)));
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        // Coin Control
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(coinControlUpdateLabels()));
        connect(model->getOptionsModel(), SIGNAL(coinControlFeaturesChanged(bool)), this, SLOT(coinControlFeatureChanged(bool)));
        connect(model->getOptionsModel(), SIGNAL(transactionFeeChanged(qint64)), this, SLOT(coinControlUpdateLabels()));
        ui->frameCoinControl->setVisible(model->getOptionsModel()->getCoinControlFeatures());
        coinControlUpdateLabels();
    }
}

SendCoinsDialog::~SendCoinsDialog()
{
    delete ui;
}

void SendCoinsDialog::on_sendButton_clicked()
{
    if(!model || !model->getOptionsModel())
        return;

    QList<SendCoinsRecipient> recipients;
    bool valid = true;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            if(entry->validate())
            {
                recipients.append(entry->getValue());
            }
            else
            {
                valid = false;
            }
        }
    }

    if(!valid || recipients.isEmpty())
    {
        return;
    }

    // Format confirmation message
    QStringList formatted;
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        formatted.append(tr("<b>%1</b> to %2 (%3)").arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), rcp.amount), Qt::escape(rcp.label), rcp.address));
    }

    fNewRecipientAllowed = false;

    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm send coins"),
                          tr("Are you sure you want to send %1?").arg(formatted.join(tr(" and "))),
          QMessageBox::Yes|QMessageBox::Cancel,
          QMessageBox::Cancel);

    if(retval != QMessageBox::Yes)
    {
        fNewRecipientAllowed = true;
        return;
    }

    WalletModel::UnlockContext ctx(model->requestUnlock());
    if(!ctx.isValid())
    {
        // Unlock wallet was cancelled
        fNewRecipientAllowed = true;
        return;
    }

    PegTxType nTxType = static_cast<PegTxType>(ui->comboBoxTxType->currentData().toInt());
    WalletModel::SendCoinsReturn sendstatus;
    CCoinControl *coinControl = nullptr;
    string sFailCause;

    if (model->getOptionsModel() && model->getOptionsModel()->getCoinControlFeatures()) {
        coinControl = CoinControlDialog::coinControl;
    }
    
    sendstatus = model->sendCoins(recipients, nTxType, coinControl, sFailCause);

    if (!sFailCause.empty()) {
        sFailCause = "\n"+sFailCause;
    }
    QString errdetails = QString::fromStdString(sFailCause);
    
    switch(sendstatus.status)
    {
    case WalletModel::InvalidAddress:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The recipient address is not valid, please recheck.")+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::InvalidAmount:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The amount to pay must be larger than 0.")+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::InvalidTxType:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The selected transaction type is not supported yet.")+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::AmountExceedsBalance:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The amount exceeds your balance.")+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::AmountWithFeeExceedsBalance:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The total exceeds your balance when the %1 transaction fee is included.").
            arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), sendstatus.fee))+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::DuplicateAddress:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("Duplicate address found, can only send to each address once per send operation.")+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::TransactionCreationFailed:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("Error: Transaction creation failed!")+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::TransactionCommitFailed:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("Error: The transaction was rejected. "
               "This might happen if some of the coins in your "
               "wallet were already spent, such as if you used "
               "a copy of wallet.dat and coins were spent in "
               "the copy but not marked as spent here.")+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::Aborted: // User aborted, nothing to do
        break;
    case WalletModel::OK:
        accept();
        CoinControlDialog::coinControl->UnSelectAll();
        coinControlUpdateLabels();
        break;
    }
    fNewRecipientAllowed = true;
}

// Coin Control: preview transaction
void SendCoinsDialog::txPreviewButtonClicked()
{
    if(!model || !model->getOptionsModel())
        return;

    QList<SendCoinsRecipient> recipients;
    bool valid = true;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            if(entry->validate())
            {
                recipients.append(entry->getValue());
            }
            else
            {
                valid = false;
            }
        }
    }

    if(!valid || recipients.isEmpty())
    {
        return;
    }

    PegTxType nTxType = static_cast<PegTxType>(ui->comboBoxTxType->currentData().toInt());
    WalletModel::SendCoinsReturn sendstatus;
    CCoinControl *coinControl = nullptr;
    string sFailCause;
    
    if (model->getOptionsModel() && model->getOptionsModel()->getCoinControlFeatures()) {
        coinControl = CoinControlDialog::coinControl;
    }
    
    CWalletTx wtx;
    sendstatus = model->sendCoinsTest(wtx, recipients, nTxType, coinControl, sFailCause);

    if (!sFailCause.empty()) {
        sFailCause = "\n"+sFailCause;
    }
    QString errdetails = QString::fromStdString(sFailCause);
    
    switch(sendstatus.status)
    {
    case WalletModel::InvalidAddress:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The recipient address is not valid, please recheck.")+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::InvalidAmount:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The amount to pay must be larger than 0.")+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::InvalidTxType:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The selected transaction type is not supported yet.")+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::AmountExceedsBalance:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The amount exceeds your balance.")+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::AmountWithFeeExceedsBalance:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("The total exceeds your balance when the %1 transaction fee is included.").
            arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), sendstatus.fee))+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::DuplicateAddress:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("Duplicate address found, can only send to each address once per send operation.")+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::TransactionCreationFailed:
        QMessageBox::warning(this, tr("Send Coins"),
            tr("Error: Transaction creation failed!")+errdetails,
            QMessageBox::Ok, QMessageBox::Ok);
        break;
    case WalletModel::TransactionCommitFailed:
        break;
    case WalletModel::Aborted:
        break;
    case WalletModel::OK:
        QDialog dlg(this);
        auto vbox = new QVBoxLayout;
        vbox->setMargin(12);
        auto txdetails = new TxDetailsWidget;
        txdetails->layout()->setMargin(0);
        vbox->addWidget(txdetails);
        auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);
        connect(buttonBox, SIGNAL(accepted()), &dlg, SLOT(accept()));
        vbox->addWidget(buttonBox);
        dlg.setLayout(vbox);
        
        int nPegInterval = Params().PegInterval(nBestHeight);
        int nCycle = nBestHeight / nPegInterval;
        
        txdetails->openTx(wtx, nullptr, 0, 
                          nCycle,
                          model->getPegSupplyIndex(), 
                          model->getPegSupplyNIndex(), 
                          model->getPegSupplyNNIndex(), 
                          wtx.nTime);
        dlg.resize(1400,800);
        dlg.exec();
        break;
    }
}

void SendCoinsDialog::clear()
{
    // Remove entries until only one left
    while(ui->entries->count())
    {
        delete ui->entries->takeAt(0)->widget();
    }
    addEntry();

    updateRemoveEnabled();

    CoinControlDialog::coinControl->UnSelectAll();
    coinControlUpdateLabels();
    
    if (sender() != ui->comboBoxTxType)
        ui->comboBoxTxType->setCurrentIndex(0);
    
    ui->sendButton->setDefault(true);
    
    PegTxType nTxType = static_cast<PegTxType>(ui->comboBoxTxType->currentData().toInt());
    ui->addButton->setEnabled(nTxType == PEG_MAKETX_SEND_RESERVE ||
                              nTxType == PEG_MAKETX_SEND_LIQUIDITY);
}

void SendCoinsDialog::reject()
{
    clear();
}

void SendCoinsDialog::accept()
{
    clear();
}

SendCoinsEntry *SendCoinsDialog::addEntry()
{
    SendCoinsEntry *entry = new SendCoinsEntry(this);
    entry->setModel(model);
    PegTxType txType = static_cast<PegTxType>(ui->comboBoxTxType->currentData().toInt());
    entry->setTxType(txType);
    ui->entries->addWidget(entry);
    connect(entry, SIGNAL(removeEntry(SendCoinsEntry*)), this, SLOT(removeEntry(SendCoinsEntry*)));
    connect(entry, SIGNAL(payAmountChanged()), this, SLOT(coinControlUpdateLabels()));

    updateRemoveEnabled();

    // Focus the field, so that entry can start immediately
    entry->clear();
    entry->setFocus();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
    QCoreApplication::instance()->processEvents();
    QScrollBar* bar = ui->scrollArea->verticalScrollBar();
    if(bar)
        bar->setSliderPosition(bar->maximum());
    return entry;
}

void SendCoinsDialog::updateRemoveEnabled()
{
    // Remove buttons are enabled as soon as there is more than one send-entry
    bool enabled = (ui->entries->count() > 1);
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            entry->setRemoveEnabled(enabled);
        }
    }
    setupTabChain(0);
    coinControlUpdateLabels();
}

void SendCoinsDialog::removeEntry(SendCoinsEntry* entry)
{
    delete entry;
    updateRemoveEnabled();
}

QWidget *SendCoinsDialog::setupTabChain(QWidget *prev)
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            prev = entry->setupTabChain(prev);
        }
    }
    QWidget::setTabOrder(prev, ui->addButton);
    QWidget::setTabOrder(ui->addButton, ui->sendButton);
    return ui->sendButton;
}

void SendCoinsDialog::pasteEntry(const SendCoinsRecipient &rv)
{
    if(!fNewRecipientAllowed)
        return;

    SendCoinsEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendCoinsEntry *first = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setValue(rv);
}

bool SendCoinsDialog::handleURI(const QString &uri)
{
    SendCoinsRecipient rv;
    // URI has to be valid
    if (GUIUtil::parseBitcoinURI(uri, &rv))
    {
        CBitcoinAddress address(rv.address.toStdString());
        if (!address.IsValid())
            return false;
        pasteEntry(rv);
        return true;
    }

    return false;
}

void SendCoinsDialog::setBalance(qint64 balance, 
                                 qint64 reserves, qint64 liquidity, qint64 frozen,
                                 vector<CFrozenCoinInfo> frozenCoins,
                                 qint64 stake, qint64 unconfirmedBalance, qint64 immatureBalance)
{
    Q_UNUSED(stake);
    Q_UNUSED(frozen);
    Q_UNUSED(balance);
    Q_UNUSED(frozenCoins);
    Q_UNUSED(unconfirmedBalance);
    Q_UNUSED(immatureBalance);

    if(model && model->getOptionsModel())
    {
        //ui->labelBalance->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), balance));
        ui->labelReserves->setText(BitcoinUnits::formatWithUnitForLabel(model->getOptionsModel()->getDisplayUnit(), reserves));
        ui->labelLiquidity->setText(BitcoinUnits::formatWithUnitForLabel(model->getOptionsModel()->getDisplayUnit(), liquidity));
    }
}

void SendCoinsDialog::updateDisplayUnit()
{
    vector<CFrozenCoinInfo> frozenCoins;
    int64_t nFrozen = model->getFrozen(NULL, &frozenCoins);
    setBalance(model->getBalance(), model->getReserve(), model->getLiquidity(), nFrozen, frozenCoins, 0, 0, 0);
}

// Coin Control: copy label "Quantity" to clipboard
void SendCoinsDialog::coinControlClipboardQuantity()
{
    QApplication::clipboard()->setText(ui->labelCoinControlQuantity->text());
}

// Coin Control: copy label "Amount" to clipboard
void SendCoinsDialog::coinControlClipboardAmount()
{
    QApplication::clipboard()->setText(ui->labelCoinControlAmount->text().left(ui->labelCoinControlAmount->text().indexOf(" ")));
}

// Coin Control: copy label "Fee" to clipboard
void SendCoinsDialog::coinControlClipboardFee()
{
    QApplication::clipboard()->setText(ui->labelCoinControlFee->text().left(ui->labelCoinControlFee->text().indexOf(" ")));
}

// Coin Control: copy label "After fee" to clipboard
void SendCoinsDialog::coinControlClipboardAfterFee()
{
    QApplication::clipboard()->setText(ui->labelCoinControlAfterFee->text().left(ui->labelCoinControlAfterFee->text().indexOf(" ")));
}

// Coin Control: copy label "Bytes" to clipboard
void SendCoinsDialog::coinControlClipboardBytes()
{
    QApplication::clipboard()->setText(ui->labelCoinControlBytes->text());
}

// Coin Control: copy label "Priority" to clipboard
void SendCoinsDialog::coinControlClipboardPriority()
{
    QApplication::clipboard()->setText(ui->labelCoinControlPriority->text());
}

// Coin Control: copy label "Low output" to clipboard
void SendCoinsDialog::coinControlClipboardLowOutput()
{
    QApplication::clipboard()->setText(ui->labelCoinControlLowOutput->text());
}

// Coin Control: copy label "Change" to clipboard
void SendCoinsDialog::coinControlClipboardChange()
{
    QApplication::clipboard()->setText(ui->labelCoinControlChange->text().left(ui->labelCoinControlChange->text().indexOf(" ")));
}

// Coin Control: settings menu - coin control enabled/disabled by user
void SendCoinsDialog::coinControlFeatureChanged(bool checked)
{
    ui->frameCoinControl->setVisible(checked);

    if (!checked && model) // coin control features disabled
        CoinControlDialog::coinControl->SetNull();
}

// Coin Control: button inputs -> show actual coin control dialog
void SendCoinsDialog::coinControlButtonClicked()
{
    PegTxType txType = static_cast<PegTxType>(ui->comboBoxTxType->currentData().toInt());
    
    CoinControlDialog dlg;
    dlg.setTxType(txType);
    dlg.setModel(model);
    dlg.exec();
    coinControlUpdateLabels();
}

// Coin Control: update labels
void SendCoinsDialog::coinControlUpdateLabels()
{
    if (!model || !model->getOptionsModel() || !model->getOptionsModel()->getCoinControlFeatures())
        return;

    // set pay amounts
    CoinControlDialog::payAmounts.clear();
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
            CoinControlDialog::payAmounts.append(entry->getValue().amount);
    }

    if (CoinControlDialog::coinControl->HasSelected())
    {
        PegTxType txType = static_cast<PegTxType>(ui->comboBoxTxType->currentData().toInt());
        
        // actual coin control calculation
        CoinControlDialog::updateLabels(model, this, txType);

        // show coin control stats
        ui->labelCoinControlAutomaticallySelected->hide();
        ui->widgetCoinControl->show();
    }
    else
    {
        // hide coin control stats
        ui->labelCoinControlAutomaticallySelected->show();
        ui->widgetCoinControl->hide();
        ui->labelCoinControlInsuffFunds->hide();
    }
}
