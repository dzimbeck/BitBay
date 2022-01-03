// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "itemdelegates.h"

//#include "main.h"
//#include "base58.h"
//#include "txdb.h"
#include "peg.h"
//#include "guiutil.h"
#include "blockchainmodel.h"
#include "metatypes.h"
//#include "qwt/qwt_plot.h"
//#include "qwt/qwt_plot_curve.h"
//#include "qwt/qwt_plot_barchart.h"

//#include <QTime>
//#include <QMenu>
//#include <QDebug>
#include <QPainter>
#include <QPainterPath>
//#include <QKeyEvent>
//#include <QClipboard>
//#include <QApplication>

//#include <string>
//#include <vector>

// delegate to draw icon on left side

LeftSideIconItemDelegate::LeftSideIconItemDelegate(QWidget *parent) :
    QStyledItemDelegate(parent) {}
LeftSideIconItemDelegate::~LeftSideIconItemDelegate() {}

void LeftSideIconItemDelegate::paint(QPainter* p,
                                  const QStyleOptionViewItem& o,
                                  const QModelIndex& index) const
{
    QStyledItemDelegate::paint(p, o, index);
    auto icondata = index.data(Qt::DecorationPropertyRole);
    if (!icondata.isValid()) return;
    QPixmap pm = icondata.value<QPixmap>();
    if (pm.isNull()) return;
    QRect r = o.rect;
    //int rside = r.height()-2;
    QRect pmr = QRect(0,0, 16,16);
    pmr.moveCenter(QPoint(r.left()+pmr.width()/2, r.center().y()));
    //p->setOpacity(0.5);
    p->drawPixmap(pmr, pm);
    //p->setOpacity(1);
}

// delegate to draw fractions

FractionsItemDelegate::FractionsItemDelegate(QWidget *parent) :
    QStyledItemDelegate(parent)
{
}

FractionsItemDelegate::~FractionsItemDelegate()
{
}

void FractionsItemDelegate::paint(QPainter* p,
                                  const QStyleOptionViewItem& o,
                                  const QModelIndex& index) const
{
    QStyledItemDelegate::paint(p, o, index);

    auto vfractions = index.data(BlockchainModel::FractionsRole);
    auto fractions = vfractions.value<CFractions>();
    auto fractions_std = fractions.Std();

    int64_t f_max = 0;
    for (int i=0; i<PEG_SIZE; i++) {
        auto f = fractions_std.f[i];
        if (f > f_max) f_max = f;
    }
    if (f_max == 0)
        return; // zero-value fractions

    auto supply = index.data(BlockchainModel::PegSupplyRole).toInt();

    QPainterPath path_reserve;
    QPainterPath path_liquidity;
    QVector<QPointF> points_reserve;
    QVector<QPointF> points_liquidity;

    QRect r = o.rect;
    qreal rx = r.x();
    qreal ry = r.y();
    qreal rw = r.width();
    qreal rh = r.height();
    qreal w = PEG_SIZE;
    qreal h = f_max;
    qreal pegx = rx + supply*rw/w;

    points_reserve.push_back(QPointF(rx,r.bottom()));
    for (int i=0; i<supply; i++) {
        int64_t f = fractions_std.f[i];
        qreal x = rx + qreal(i)*rw/w;
        qreal y = ry + rh - qreal(f)*rh/h;
        points_reserve.push_back(QPointF(x,y));
    }
    points_reserve.push_back(QPointF(pegx,r.bottom()));

    points_liquidity.push_back(QPointF(pegx,r.bottom()));
    for (int i=supply; i<PEG_SIZE; i++) {
        int64_t f = fractions_std.f[i];
        qreal x = rx + qreal(i)*rw/w;
        qreal y = ry + rh - qreal(f)*rh/h;
        points_liquidity.push_back(QPointF(x,y));
    }
    points_liquidity.push_back(QPointF(rx+rw,r.bottom()));

    QPolygonF poly_reserve(points_reserve);
    path_reserve.addPolygon(poly_reserve);

    QPolygonF poly_liquidity(points_liquidity);
    path_liquidity.addPolygon(poly_liquidity);

    p->setRenderHint( QPainter::Antialiasing );

    p->setBrush( QColor("#c06a15") );
    p->setPen( QColor("#c06a15") );
    p->drawPath( path_reserve );

    p->setBrush( QColor("#2da5e0") );
    p->setPen( QColor("#2da5e0") );
    p->drawPath( path_liquidity );

    p->setPen( Qt::red );
    p->drawLine(QPointF(pegx, ry), QPointF(pegx, ry+rh));
}
