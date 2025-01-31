#include "sendcoinsentry.h"
#include "ui_sendcoinsentry.h"

#include "guiutil.h"
#include "bitcoinunits.h"
#include "addressbookpage.h"
#include "walletmodel.h"
#include "optionsmodel.h"
#include "addresstablemodel.h"

#include <QApplication>
#include <QClipboard>

#include <boost/assign/list_of.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

SendCoinsEntry::SendCoinsEntry(QWidget *parent) :
    QFrame(parent),
    ui(new Ui::SendCoinsEntry),
    model(0)
{
    ui->setupUi(this);
    GUIUtil::SetBitBayFonts(this);

#ifdef Q_OS_MAC
    ui->payToLayout->setSpacing(4);
#endif
#if QT_VERSION >= 0x040700
    /* Do not move this to the XML file, Qt before 4.7 will choke on it */
    ui->addAsLabel->setPlaceholderText(tr("Enter a label for this address to add it to your address book"));
    ui->payTo->setPlaceholderText(tr("Enter a BitBay address:"));
#endif
    setFocusPolicy(Qt::TabFocus);
    setFocusProxy(ui->payTo);

    GUIUtil::setupAddressWidget(ui->payTo, this);

    minuteTimer = new QTimer(this);
    minuteTimer->setInterval(60*1000);
    minuteTimer->start();
    connect(minuteTimer, SIGNAL(timeout()), this, SLOT(updateBridges()));
    updateBridges();

    connect(ui->networkCombo, SIGNAL(currentTextChanged(QString)), this, SLOT(onNetworkChange(QString)));
}

SendCoinsEntry::~SendCoinsEntry()
{
    delete ui;
}

void SendCoinsEntry::on_pasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->payTo->setText(QApplication::clipboard()->text());
}

void SendCoinsEntry::on_addressBookButton_clicked()
{
    if(!model)
        return;
    AddressBookPage::Tabs tab = AddressBookPage::SendingTab;
    if (txType == PEG_MAKETX_FREEZE_RESERVE ||
        txType == PEG_MAKETX_FREEZE_LIQUIDITY) {
        tab = AddressBookPage::ReceivingTab;
    }
    AddressBookPage dlg(AddressBookPage::ForSending, tab, this);
    dlg.setModel(model->getAddressTableModel());
    if(dlg.exec())
    {
        ui->payTo->setText(dlg.getReturnValue());
        ui->payAmount->setFocus();
    }
}

void SendCoinsEntry::on_payTo_textChanged(const QString &address)
{
    if(!model)
        return;
    // Fill in label from address book, if address has an associated label
    QString associatedLabel = model->getAddressTableModel()->labelForAddress(address);
    if(!associatedLabel.isEmpty())
        ui->addAsLabel->setText(associatedLabel);
}

void SendCoinsEntry::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel())
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    connect(ui->payAmount, SIGNAL(textChanged()), this, SIGNAL(payAmountChanged()));

    clear();
    updateBridges();
}

void SendCoinsEntry::setTxType(PegTxType t)
{
    txType = t;
}

void SendCoinsEntry::setRemoveEnabled(bool enabled)
{
    ui->deleteButton->setEnabled(enabled);
}

void SendCoinsEntry::clear()
{
    ui->payTo->clear();
    ui->addAsLabel->clear();
    ui->payAmount->clear();
    ui->payTo->setFocus();
    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void SendCoinsEntry::on_deleteButton_clicked()
{
    emit removeEntry(this);
}

bool SendCoinsEntry::validate()
{
    // Check input validity
    bool retval = true;

    if(!ui->payAmount->validate())
    {
        retval = false;
    }
    else
    {
        if(ui->payAmount->value() <= 0)
        {
            // Cannot send 0 coins or less
            ui->payAmount->setValid(false);
            retval = false;
        }
    }

    if(!ui->payTo->hasAcceptableInput())
    {
        ui->payTo->setValid(false);
        retval = false;
    }

    QString bitbay = ui->networkCombo->itemText(0);
    QString network = ui->networkCombo->currentText();
    if (network == bitbay) {
        if(model && !model->validateAddress(ui->payTo->text()))
        {
            ui->payTo->setValid(false);
            retval = false;
        }
    }
    else {
        // validate eth-style address
        string saddr = ui->payTo->text().trimmed().toStdString();

        if (!boost::starts_with(saddr, "0x"))
        {
            ui->payTo->setValid(false);
            retval = false;
        }
        if (!IsHex(saddr.substr(2)) || saddr.size() != 42)
        {
            ui->payTo->setValid(false);
            retval = false;
        }

    }

    return retval;
}

SendCoinsRecipient SendCoinsEntry::getValue()
{
    SendCoinsRecipient rv;

    rv.network = ui->networkCombo->currentData().toString();
    rv.address = ui->payTo->text();
    rv.label = ui->addAsLabel->text();
    rv.amount = ui->payAmount->value();

    return rv;
}

QWidget *SendCoinsEntry::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, ui->payTo);
    QWidget::setTabOrder(ui->payTo, ui->addressBookButton);
    QWidget::setTabOrder(ui->addressBookButton, ui->pasteButton);
    QWidget::setTabOrder(ui->pasteButton, ui->deleteButton);
    QWidget::setTabOrder(ui->deleteButton, ui->addAsLabel);
    return ui->payAmount->setupTabChain(ui->addAsLabel);
}

void SendCoinsEntry::setValue(const SendCoinsRecipient &value)
{
    ui->payTo->setText(value.address);
    ui->addAsLabel->setText(value.label);
    ui->payAmount->setValue(value.amount);
}

bool SendCoinsEntry::isClear()
{
    return ui->payTo->text().isEmpty();
}

void SendCoinsEntry::setFocus()
{
    ui->payTo->setFocus();
}

void SendCoinsEntry::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        // Update payAmount with the current unit
        ui->payAmount->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    }
}

void SendCoinsEntry::updateBridges()
{
    QString cur = ui->networkCombo->currentText();
    ui->networkCombo->clear();
    ui->networkCombo->addItem(tr("BitBay"));
    if (!model) return;
    QMap<QString,QString> bridges = model->getBridges();
    for (const QString & bridge : bridges.keys()) {
        QString text = bridge+" "+bridges[bridge];
        ui->networkCombo->addItem(text, bridge);
        if (text == cur) {
            ui->networkCombo->setCurrentText(cur);
        }
    }
}

void SendCoinsEntry::onNetworkChange(QString text)
{
    QString bitbay = ui->networkCombo->itemText(0);
    if (text == bitbay) {
        ui->addAsLabel->setEnabled(true);
        ui->payTo->setPlaceholderText(tr("Enter a BitBay address:"));
        return;
    }
    QString network = ui->networkCombo->currentData().toString();
    ui->addAsLabel->setEnabled(false);
    ui->payTo->setPlaceholderText("Enter a "+network+" address:");
}
