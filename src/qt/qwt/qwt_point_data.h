/* -*- mode: C++ ; c-file-style: "stroustrup" -*- *****************************
 * Qwt Widget Library
 * Copyright (C) 1997   Josef Wilgen
 * Copyright (C) 2002   Uwe Rathmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the Qwt License, Version 1.0
 *****************************************************************************/

#ifndef QWT_POINT_DATA_H
#define QWT_POINT_DATA_H

#include "qwt_global.h"
#include "qwt_series_data.h"

#include <cstring>

/*!
  \brief Interface for iterating over two QVector<T> objects.
*/
template <typename T>
class QwtPointArrayData: public QwtPointSeriesData
{
public:
    QwtPointArrayData( const QVector<T> &x, const QVector<T> &y );
    QwtPointArrayData( const T *x, const T *y, size_t size );

    virtual size_t size() const QWT_OVERRIDE;
    virtual QPointF sample( size_t index ) const QWT_OVERRIDE;

    const QVector<T> &xData() const;
    const QVector<T> &yData() const;

private:
    QVector<T> d_x;
    QVector<T> d_y;
};

/*!
  \brief Data class containing two pointers to memory blocks of T.
 */
template <typename T>
class QwtCPointerData: public QwtPointSeriesData
{
public:
    QwtCPointerData( const T *x, const T *y, size_t size );

    virtual size_t size() const QWT_OVERRIDE;
    virtual QPointF sample( size_t index ) const QWT_OVERRIDE;

    const T *xData() const;
    const T *yData() const;

private:
    const T *d_x;
    const T *d_y;
    size_t d_size;
};

/*!
  \brief Interface for iterating over a QVector<T>.

  The memory contains the y coordinates, while the index is
  interpreted as x coordinate.
*/
template <typename T>
class QwtValuePointData: public QwtPointSeriesData
{
public:
    QwtValuePointData( const QVector<T> &y );
    QwtValuePointData( const T *y, size_t size );

    virtual size_t size() const QWT_OVERRIDE;
    virtual QPointF sample( size_t index ) const QWT_OVERRIDE;

    const QVector<T> &yData() const;

private:
    QVector<T> d_y;
};

/*!
  \brief Data class containing a pointer to memory of y coordinates

  The memory contains the y coordinates, while the index is
  interpreted as x coordinate.
 */
template <typename T>
class QwtCPointerValueData: public QwtPointSeriesData
{
public:
    QwtCPointerValueData( const T *y, size_t size );

    virtual size_t size() const QWT_OVERRIDE;
    virtual QPointF sample( size_t index ) const QWT_OVERRIDE;

    const T *yData() const;

private:
    const T *d_y;
    size_t d_size;
};

/*!
  \brief Synthetic point data

  QwtSyntheticPointData provides a fixed number of points for an interval.
  The points are calculated in equidistant steps in x-direction.

  If the interval is invalid, the points are calculated for
  the "rectangle of interest", what normally is the displayed area on the
  plot canvas. In this mode you get different levels of detail, when
  zooming in/out.

  \par Example

  The following example shows how to implement a sinus curve.

  \code
#include <cmath>
#include <qwt_series_data.h>
#include <qwt_plot_curve.h>
#include <qwt_plot.h>
#include <qapplication.h>

class SinusData: public QwtSyntheticPointData
{
public:
    SinusData():
        QwtSyntheticPointData( 100 )
    {
    }

    virtual double y( double x ) const
    {
        return qSin( x );
    }
};

int main(int argc, char **argv)
{
    QApplication a( argc, argv );

    QwtPlot plot;
    plot.setAxisScale( QwtPlot::xBottom, 0.0, 10.0 );
    plot.setAxisScale( QwtPlot::yLeft, -1.0, 1.0 );

    QwtPlotCurve *curve = new QwtPlotCurve( "y = sin(x)" );
    curve->setData( new SinusData() );
    curve->attach( &plot );

    plot.show();
    return a.exec();
}
   \endcode
*/
class QWT_EXPORT QwtSyntheticPointData: public QwtPointSeriesData
{
public:
    QwtSyntheticPointData( size_t size,
        const QwtInterval & = QwtInterval() );

    void setSize( size_t size );
    virtual size_t size() const QWT_OVERRIDE;

    void setInterval( const QwtInterval& );
    QwtInterval interval() const;

    virtual QRectF boundingRect() const QWT_OVERRIDE;
    virtual QPointF sample( size_t index ) const QWT_OVERRIDE;

    /*!
       Calculate a y value for a x value

       \param x x value
       \return Corresponding y value
     */
    virtual double y( double x ) const = 0;
    virtual double x( uint index ) const;

    virtual void setRectOfInterest( const QRectF & ) QWT_OVERRIDE;
    QRectF rectOfInterest() const;

private:
    size_t d_size;
    QwtInterval d_interval;
    QRectF d_rectOfInterest;
    QwtInterval d_intervalOfInterest;
};

/*!
  Constructor

  \param x Array of x values
  \param y Array of y values

  \sa QwtPlotCurve::setData(), QwtPlotCurve::setSamples()
*/
template <typename T>
QwtPointArrayData<T>::QwtPointArrayData(
        const QVector<T> &x, const QVector<T> &y ):
    d_x( x ),
    d_y( y )
{
}

/*!
  Constructor

  \param x Array of x values
  \param y Array of y values
  \param size Size of the x and y arrays
  \sa QwtPlotCurve::setData(), QwtPlotCurve::setSamples()
*/
template <typename T>
QwtPointArrayData<T>::QwtPointArrayData(
    const T *x, const T *y, size_t size )
{
    d_x.resize( size );
    std::memcpy( d_x.data(), x, size * sizeof( T ) );

    d_y.resize( size );
    std::memcpy( d_y.data(), y, size * sizeof( T ) );
}

//! \return Size of the data set
template <typename T>
size_t QwtPointArrayData<T>::size() const
{
    return qMin( d_x.size(), d_y.size() );
}

/*!
  Return the sample at position i

  \param index Index
  \return Sample at position i
*/
template <typename T>
QPointF QwtPointArrayData<T>::sample( size_t index ) const
{
    return QPointF( d_x[int( index )], d_y[int( index )] );
}

//! \return Array of the x-values
template <typename T>
const QVector<T> &QwtPointArrayData<T>::xData() const
{
    return d_x;
}

//! \return Array of the y-values
template <typename T>
const QVector<T> &QwtPointArrayData<T>::yData() const
{
    return d_y;
}

/*!
  Constructor

  \param y Array of y values

  \sa QwtPlotCurve::setData(), QwtPlotCurve::setSamples()
*/
template <typename T>
QwtValuePointData<T>::QwtValuePointData( const QVector<T> &y ):
    d_y( y )
{
}

/*!
  Constructor

  \param x Array of x values
  \param y Array of y values
  \param size Size of the x and y arrays
  \sa QwtPlotCurve::setData(), QwtPlotCurve::setSamples()
*/
template <typename T>
QwtValuePointData<T>::QwtValuePointData( const T *y, size_t size )
{
    d_y.resize( size );
    std::memcpy( d_y.data(), y, size * sizeof( T ) );
}

//! \return Size of the data set
template <typename T>
size_t QwtValuePointData<T>::size() const
{
    return d_y.size();
}

/*!
  Return the sample at position i

  \param index Index
  \return Sample at position i
*/
template <typename T>
QPointF QwtValuePointData<T>::sample( size_t index ) const
{
    return QPointF( index, d_y[int( index )] );
}

//! \return Array of the y-values
template <typename T>
const QVector<T> &QwtValuePointData<T>::yData() const
{
    return d_y;
}

/*!
  Constructor

  \param x Array of x values
  \param y Array of y values
  \param size Size of the x and y arrays

  \warning The programmer must assure that the memory blocks referenced
           by the pointers remain valid during the lifetime of the
           QwtPlotCPointer object.

  \sa QwtPlotCurve::setData(), QwtPlotCurve::setRawSamples()
*/

template <typename T>
QwtCPointerData<T>::QwtCPointerData( const T *x, const T *y, size_t size ):
    d_x( x ),
    d_y( y ),
    d_size( size )
{
}

//! \return Size of the data set
template <typename T>
size_t QwtCPointerData<T>::size() const
{
    return d_size;
}

/*!
  Return the sample at position i

  \param index Index
  \return Sample at position i
*/
template <typename T>
QPointF QwtCPointerData<T>::sample( size_t index ) const
{
    return QPointF( d_x[int( index )], d_y[int( index )] );
}

//! \return Array of the x-values
template <typename T>
const T *QwtCPointerData<T>::xData() const
{
    return d_x;
}

//! \return Array of the y-values
template <typename T>
const T *QwtCPointerData<T>::yData() const
{
    return d_y;
}

/*!
  Constructor

  \param y Array of y values
  \param size Size of the x and y arrays

  \warning The programmer must assure that the memory blocks referenced
           by the pointers remain valid during the lifetime of the
           QwtCPointerValueData object.

  \sa QwtPlotCurve::setData(), QwtPlotCurve::setRawSamples()
*/

template <typename T>
QwtCPointerValueData<T>::QwtCPointerValueData( const T *y, size_t size ):
    d_y( y ),
    d_size( size )
{
}

//! \return Size of the data set
template <typename T>
size_t QwtCPointerValueData<T>::size() const
{
    return d_size;
}

/*!
  Return the sample at position i

  \param index Index
  \return Sample at position i
*/
template <typename T>
QPointF QwtCPointerValueData<T>::sample( size_t index ) const
{
    return QPointF( index, d_y[ int( index ) ] );
}

//! \return Array of the y-values
template <typename T>
const T *QwtCPointerValueData<T>::yData() const
{
    return d_y;
}

#endif
