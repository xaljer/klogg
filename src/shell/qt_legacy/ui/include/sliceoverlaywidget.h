/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

#ifndef SLICEOVERLAYWIDGET_H
#define SLICEOVERLAYWIDGET_H

#include <QPaintEvent>
#include <QColor>
#include <QMargins>
#include <QWidget>

#include "eventvisualization.h"

class QLabel;
class QScrollArea;
class QVBoxLayout;

class SliceOverlayWidget : public QWidget {
    Q_OBJECT

  public:
    explicit SliceOverlayWidget( QWidget* parent = nullptr );

    void setSlices( const QVector<SliceLineRange>& slices, LineNumber line );
    void showEmptyState( LineNumber line );
    void setMaximumDisplayHeight( int height );
    void setPreferredDisplayWidth( int width );
    void setTextColor( const QColor& color );
    void setBackgroundColor( const QColor& color );
    void setBorderEnabled( bool enabled );

  protected:
    void paintEvent( QPaintEvent* event ) override;

  private:
    QWidget* contentWidget_ = nullptr;
    QVBoxLayout* contentLayout_ = nullptr;
    QLabel* headerLabel_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    int maximumDisplayHeight_ = 280;
    int preferredDisplayWidth_ = 320;
    QColor textColor_ = QColor( QStringLiteral( "#2e5a6f" ) );
    QColor backgroundColor_ = QColor( QStringLiteral( "#d1ffff" ) );
    QMargins panelPadding_ = QMargins( 10, 8, 10, 8 );
    bool borderEnabled_ = true;

    void clearContent();
    void updateMinimumDisplayHeight( int minimumVisibleLines = 2 );
    QColor outlineShadowColor() const;
    QString formatSliceTitle( const SliceLineRange& slice ) const;
    QString formatSliceDuration( const SliceLineRange& slice ) const;
};

#endif
