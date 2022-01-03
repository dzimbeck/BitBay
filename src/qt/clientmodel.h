#ifndef CLIENTMODEL_H
#define CLIENTMODEL_H

#include <QObject>
#include <boost/tuple/tuple.hpp>

#include "net.h"

class OptionsModel;
class AddressTableModel;
class TransactionTableModel;
class CWallet;

QT_BEGIN_NAMESPACE
class QDateTime;
class QTimer;
QT_END_NAMESPACE

/** Model for Bitcoin network client. */
class ClientModel : public QObject
{
    Q_OBJECT

public:
    explicit ClientModel(OptionsModel *optionsModel, QObject *parent = 0);
    ~ClientModel();

    OptionsModel *getOptionsModel();

    CNodeShortStats getConnections() const;
    int getNumConnections() const;
    int getNumBlocks() const;
    int getNumBlocksAtStartup();
    int getPegSupplyIndex() const;
    int getPegNextSupplyIndex() const;
    int getPegNextNextSupplyIndex() const;
    int getPegStartBlockNum() const;
    boost::tuple<int,int,int> getPegVotes() const;

    quint64 getTotalBytesRecv() const;
    quint64 getTotalBytesSent() const;

    QDateTime getLastBlockDate() const;

    //! Return true if client connected to testnet
    bool isTestNet() const;
    //! Return true if core is doing initial block download
    bool inInitialBlockDownload() const;
    //! Return true if core is importing blocks
    bool isImporting() const;
    //! Return warnings to be displayed in status bar
    QString getStatusBarWarnings() const;

    QString formatFullVersion() const;
    QString formatBuildDate() const;
    bool isReleaseVersion() const;
    QString clientName() const;
    QString formatClientStartupTime() const;

private:
    OptionsModel *optionsModel;

    int cachedNumBlocks;

    int numBlocksAtStartup;

    QTimer *pollTimer;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

signals:
    void connectionsChanged(const CNodeShortStats &);
    void numConnectionsChanged(int count);
    void numBlocksChanged(int count);
    void alertsChanged(const QString &warnings);
    void bytesChanged(quint64 totalBytesIn, quint64 totalBytesOut);

    //! Asynchronous message notification
    void message(const QString &title, const QString &message, bool modal, unsigned int style);

public slots:
    void updateTimer();
    void updateNumConnections(int numConnections);
    void updateConnections(const CNodeShortStats &);
    void updateAlert(const QString &hash, int status);
};

#endif // CLIENTMODEL_H
