#include "reserve_meter.h"

#include "qwt_dial_needle.h"
#include "qwt_round_scale_draw.h"

#include <qpainter.h>

ReserveMeter::ReserveMeter( QWidget *parent ):
    QwtDial( parent )
{
    QwtRoundScaleDraw *scaleDraw = new QwtRoundScaleDraw();
    scaleDraw->setSpacing( 8 );
    scaleDraw->enableComponent( QwtAbstractScaleDraw::Backbone, false );
    scaleDraw->setTickLength( QwtScaleDiv::MinorTick, 0 );
    scaleDraw->setTickLength( QwtScaleDiv::MediumTick, 4 );
    scaleDraw->setTickLength( QwtScaleDiv::MajorTick, 8 );
    setScaleDraw( scaleDraw );

    setWrapping( false );
    setReadOnly( true );

    setOrigin( 220.0 );
    setScaleArc( 0.0, 100.0 );

    QwtDialSimpleNeedle *needle = new QwtDialSimpleNeedle(
        QwtDialSimpleNeedle::Arrow, true, Qt::red,
        QColor( Qt::gray ).light( 130 ) );
    setNeedle( needle );
    setValue(0);
    setScaleStepSize(20);
}
