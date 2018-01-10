#ifndef VERIFYMESSAGEPAGE_H
#define VERIFYMESSAGEPAGE_H

#include <QDialog>

namespace Ui {
    class VerifyMessagePage;
}
class WalletModel;

class VerifyMessagePage : public QDialog
{
    Q_OBJECT

public:
    explicit VerifyMessagePage(QWidget *parent = 0);
    ~VerifyMessagePage();

    void setModel(WalletModel *model);
    void setAddress_VM(QString address);

protected:
    bool eventFilter(QObject *object, QEvent *event);

private:
    Ui::VerifyMessagePage *ui;
    WalletModel *model;

private slots:
    /* verify message */
    void on_addressBookButton_VM_clicked();
    void on_verifyMessageButton_VM_clicked();
    void on_clearButton_VM_clicked();
};

#endif // VERIFYMESSAGEPAGE_H
