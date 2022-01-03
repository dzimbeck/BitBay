// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCKCHAINMODEL_H
#define BLOCKCHAINMODEL_H

#include <QAbstractItemModel>
#include <QCache>

#include "bignum.h"

class BlockchainModelPriv;

/**
   Qt model of the blockchain.
 */
class BlockchainModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit BlockchainModel(QObject *parent = nullptr);
    ~BlockchainModel();

    enum ColumnIndex {
        Height = 0, /**< Height of the block */
        Hash,        /**< Blok hash */
        Votes,
        Peg,
        Mined,
        Date        /**< Block date */
    };
    enum Roles {
        HashRole = Qt::UserRole+1000,
        HeightRole,
        HashStringRole,
        TxNumRole,
        OutNumRole,
        FractionsRole,
        PegCycleRole,
        PegSupplyRole,
        PegSupplyNRole,
        PegSupplyNNRole,
        ValueForCopy
    };

    /** @name Methods overridden from QAbstractItemModel
        @{*/
    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    bool setData(const QModelIndex &index, const QVariant &value, int role);
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex &parent) const;
    QModelIndex parent(const QModelIndex &index) const;
    Qt::ItemFlags flags(const QModelIndex &index) const;
    /*@}*/

public slots:
    void setNumBlocks(int);

private:
    bool getItem(int) const;

private:
    BlockchainModelPriv *priv;
};

#endif // BLOCKCHAINMODEL_H
