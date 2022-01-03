#ifndef RESERVE_METER
#define RESERVE_METER

#include <qstring.h>
#include "qwt_dial.h"

class ReserveMeter: public QwtDial
{
public:
    ReserveMeter( QWidget *parent = NULL );
};

#endif
