/* -*- mode: C++ ; c-file-style: "stroustrup" -*- *****************************
 * Qwt Widget Library
 * Copyright (C) 1997   Josef Wilgen
 * Copyright (C) 2002   Uwe Rathmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the Qwt License, Version 1.0
 *****************************************************************************/

#ifndef QWT_PLOT_CANVAS_H
#define QWT_PLOT_CANVAS_H

#include "qwt_global.h"
#include "qwt_plot_abstract_canvas.h"

#include <qframe.h>
#include <qpainterpath.h>

class QwtPlot;
class QPixmap;
class QPainterPath;

/*!
  \brief Canvas of a QwtPlot.

   Canvas is the widget where all plot items are displayed

  \sa QwtPlot::setCanvas(), QwtPlotGLCanvas
*/
class QWT_EXPORT QwtPlotCanvas : public QFrame, public QwtPlotAbstractCanvas
{
    Q_OBJECT

    Q_PROPERTY( double borderRadius READ borderRadius WRITE setBorderRadius )

public:

    /*!
      \brief Paint attributes

      The default setting enables BackingStore and Opaque.

      \sa setPaintAttribute(), testPaintAttribute()
     */
    enum PaintAttribute
    {
        /*!
          \brief Paint double buffered reusing the content
                 of the pixmap buffer when possible.

          Using a backing store might improve the performance
          significantly, when working with widget overlays ( like rubber bands ).
          Disabling the cache might improve the performance for
          incremental paints (using QwtPlotDirectPainter ).

          \sa backingStore(), invalidateBackingStore()
         */
        BackingStore = 1,

        /*!
          \brief Try to fill the complete contents rectangle
                 of the plot canvas

          When using styled backgrounds Qt assumes, that the
          canvas doesn't fill its area completely
          ( f.e because of rounded borders ) and fills the area
          below the canvas. When this is done with gradients it might
          result in a serious performance bottleneck - depending on the size.

          When the Opaque attribute is enabled the canvas tries to
          identify the gaps with some heuristics and to fill those only.

          \warning Will not work for semitransparent backgrounds
         */
        Opaque       = 2,

        /*!
          \brief Try to improve painting of styled backgrounds

          QwtPlotCanvas supports the box model attributes for
          customizing the layout with style sheets. Unfortunately
          the design of Qt style sheets has no concept how to
          handle backgrounds with rounded corners - beside of padding.

          When HackStyledBackground is enabled the plot canvas tries
          to separate the background from the background border
          by reverse engineering to paint the background before and
          the border after the plot items. In this order the border
          gets perfectly antialiased and you can avoid some pixel
          artifacts in the corners.
         */
        HackStyledBackground = 4,

        /*!
          When ImmediatePaint is set replot() calls repaint()
          instead of update().

          \sa replot(), QWidget::repaint(), QWidget::update()
         */
        ImmediatePaint = 8,

        /*!
          \brief Render the canvas via an OpenGL buffer

          In OpenGLBuffer mode the plot scene will be rendered to a temporary
          OpenGL buffer, that will be translated to a QImage afterwards.
          Then this image will be painted to the canvas.

          This mode might be useful for "heavy" plots to achieve
          hardware acceleration on platforms, where the raster paint engine
          ( = software renderer ) would be used otherwise.
          But the penalty for copying out the buffer to the image makes this mode
          less optimal when looking for high refresh rates of a "lightweight" plot.

          On platforms, that already run on a hardware accelerated graphics pipeline   
          this mode does not make much sense. Those platforms might be: 

              - Qt4: QApplication::setGraphicsSystem( "opengl" ) )
              - Qt4/X11: ( QApplication::setGraphicsSystem( "native" )
              - Qt5/X11 ( >= Qt 5.10 ): export QT_XCB_NATIVE_PAINTING=1

          \note The OpenGLBuffer mode has no effect, when "QwtOpenGL" has been disabled in
                qwtconfig.pri.

          \sa QwtPlotOpenGLCanvas, QwtPlotGLCanvas
         */
        OpenGLBuffer = 16
    };

    //! Paint attributes
    typedef QFlags<PaintAttribute> PaintAttributes;

    explicit QwtPlotCanvas( QwtPlot * = NULL );
    virtual ~QwtPlotCanvas();

    void setPaintAttribute( PaintAttribute, bool on = true );
    bool testPaintAttribute( PaintAttribute ) const;

    const QPixmap *backingStore() const;
    Q_INVOKABLE void invalidateBackingStore();

    virtual bool event( QEvent * ) QWT_OVERRIDE;

    Q_INVOKABLE QPainterPath borderPath( const QRect & ) const;

public Q_SLOTS:
    void replot();

protected:
    virtual void paintEvent( QPaintEvent * ) QWT_OVERRIDE;
    virtual void resizeEvent( QResizeEvent * ) QWT_OVERRIDE;

    virtual void drawBorder( QPainter * ) QWT_OVERRIDE;

private:
    QImage toImageFBO( const QSize &size );

    class PrivateData;
    PrivateData *d_data;
};

Q_DECLARE_OPERATORS_FOR_FLAGS( QwtPlotCanvas::PaintAttributes )

#endif
