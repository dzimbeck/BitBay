// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ITEMDELEGATES_H
#define ITEMDELEGATES_H

#include <QItemDelegate>
#include <QStyledItemDelegate>

class QModelIndex;

class LeftSideIconItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit LeftSideIconItemDelegate(QWidget *parent = nullptr);
    ~LeftSideIconItemDelegate() override;

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};

class FractionsItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit FractionsItemDelegate(QWidget *parent = nullptr);
    ~FractionsItemDelegate() override;

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};

#endif // ITEMDELEGATES_H
