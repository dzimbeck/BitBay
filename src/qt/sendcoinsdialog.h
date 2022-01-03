#ifndef SENDCOINSDIALOG_H
#define SENDCOINSDIALOG_H

#include <QDialog>
#include <QString>
#include "walletmodel.h"

namespace Ui {
    class SendCoinsDialog;
}
class WalletModel;
class SendCoinsEntry;
class SendCoinsRecipient;

QT_BEGIN_NAMESPACE
class QUrl;
QT_END_NAMESPACE

/** Dialog for sending bitcoins */
class SendCoinsDialog : public QDialog
{
    Q_OBJECT

    enum {
        SEND_STD = 0,
        SEND_RESERVE,
        FREEZE_RESERVE,
        FREEZE_LIQUIDITY,
    };
    
public:
    explicit SendCoinsDialog(QWidget *parent = 0);
    ~SendCoinsDialog();

    void setModel(WalletModel *model);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    void pasteEntry(const SendCoinsRecipient &rv);
    bool handleURI(const QString &uri);

public slots:
    void clear();
    void reject();
    void accept();
    SendCoinsEntry *addEntry();
    void updateRemoveEnabled();
    void setBalance(qint64 balance, 
                    qint64 reserves, qint64 liquidity, qint64 frozen,
                    std::vector<CFrozenCoinInfo> frozenCoins,
                    qint64 stake, qint64 unconfirmedBalance, qint64 immatureBalance);

private:
    Ui::SendCoinsDialog *ui;
    WalletModel *model;
    bool fNewRecipientAllowed;

private slots:
    void on_sendButton_clicked();
    void removeEntry(SendCoinsEntry* entry);
    void updateDisplayUnit();
    void txPreviewButtonClicked();
    void coinControlFeatureChanged(bool);
    void coinControlButtonClicked();
    void coinControlUpdateLabels();
    void coinControlClipboardQuantity();
    void coinControlClipboardAmount();
    void coinControlClipboardFee();
    void coinControlClipboardAfterFee();
    void coinControlClipboardBytes();
    void coinControlClipboardPriority();
    void coinControlClipboardLowOutput();
    void coinControlClipboardChange();
};

#endif // SENDCOINSDIALOG_H
