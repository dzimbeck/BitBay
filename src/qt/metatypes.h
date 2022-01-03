
#ifndef METATYPES_H
#define METATYPES_H

#include <QObject>
#include <QPointer>
#include <vector>
#include "bignum.h"
#include "peg.h"
#include "net.h"

Q_DECLARE_METATYPE(uint256);
Q_DECLARE_METATYPE(CFractions);
Q_DECLARE_METATYPE(CNodeShortStat);
Q_DECLARE_METATYPE(CNodeShortStats);

#endif // METATYPES_H
