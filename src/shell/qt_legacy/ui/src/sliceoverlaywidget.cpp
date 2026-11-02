#include "sliceoverlaywidget.h"

#include <QFrame>
#include <QLabel>
#include <QPalette>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QtGlobal>

namespace {
constexpr int MinimumOverlayWidth = 260;
constexpr int MinimumOverlayContentWidth = 220;
constexpr int ShadowInset = 2;
constexpr qreal OverlayCornerRadius = 6.0;
QColor TransparentColor( 0, 0, 0, 0 );
}

namespace {
int panelShadowInset( bool borderEnabled )
{
    return borderEnabled ? ShadowInset : 0;
}
}

SliceOverlayWidget::SliceOverlayWidget( QWidget* parent )
    : QWidget( parent )
{
    setAttribute( Qt::WA_TransparentForMouseEvents, true );
    setAttribute( Qt::WA_StyledBackground, false );
    setAutoFillBackground( false );
    setObjectName( QStringLiteral( "sliceOverlayWidget" ) );
    setMinimumWidth( MinimumOverlayWidth );
    setStyleSheet( QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }" ) );

    auto* layout = new QVBoxLayout( this );
    layout->setContentsMargins( panelPadding_ );
    layout->setSpacing( 4 );

    headerLabel_ = new QLabel( this );
    headerLabel_->hide();

    scrollArea_ = new QScrollArea( this );
    scrollArea_->setFrameShape( QFrame::NoFrame );
    scrollArea_->setWidgetResizable( true );
    scrollArea_->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    scrollArea_->setMaximumHeight( maximumDisplayHeight_ );
    scrollArea_->setAttribute( Qt::WA_StyledBackground, false );
    scrollArea_->setAutoFillBackground( false );

    if ( auto* viewport = scrollArea_->viewport(); viewport != nullptr ) {
        viewport->setAttribute( Qt::WA_StyledBackground, false );
        viewport->setAutoFillBackground( false );
        viewport->setPalette( QPalette( TransparentColor ) );
        viewport->setStyleSheet( QStringLiteral( "background: transparent;" ) );
    }

    contentWidget_ = new QWidget( scrollArea_ );
    contentWidget_->setAttribute( Qt::WA_StyledBackground, false );
    contentWidget_->setAutoFillBackground( false );
    contentWidget_->setPalette( QPalette( TransparentColor ) );
    contentWidget_->setStyleSheet( QStringLiteral( "background: transparent;" ) );
    contentLayout_ = new QVBoxLayout( contentWidget_ );
    contentLayout_->setContentsMargins( 0, 0, 0, 0 );
    contentLayout_->setSpacing( 4 );
    scrollArea_->setWidget( contentWidget_ );
    layout->addWidget( scrollArea_ );

    updateMinimumDisplayHeight( 2 );
    resize( preferredDisplayWidth_, minimumHeight() );
}

void SliceOverlayWidget::setTextColor( const QColor& color )
{
    textColor_ = color;
    update();
}

void SliceOverlayWidget::setBackgroundColor( const QColor& color )
{
    backgroundColor_ = color;
    update();
}

void SliceOverlayWidget::setBorderEnabled( bool enabled )
{
    borderEnabled_ = enabled;
    updateMinimumDisplayHeight( 2 );
    updateGeometry();
    resize( preferredDisplayWidth_, minimumHeight() );
    update();
}

QColor SliceOverlayWidget::outlineShadowColor() const
{
    return QColor( 0, 0, 0, 96 );
}

void SliceOverlayWidget::paintEvent( QPaintEvent* event )
{
    Q_UNUSED( event )

    QPainter painter( this );
    painter.setRenderHint( QPainter::Antialiasing, true );

    const int shadowInset = panelShadowInset( borderEnabled_ );
    if ( borderEnabled_ ) {
        const QRectF shadowRect = rect().adjusted( shadowInset, shadowInset, -1, -1 );
        painter.setPen( Qt::NoPen );
        painter.setBrush( outlineShadowColor() );
        painter.drawRoundedRect( shadowRect, OverlayCornerRadius, OverlayCornerRadius );
    }

    const QRectF panelRect
        = QRectF( rect() ).adjusted( 0.5, 0.5, -shadowInset - 0.5, -shadowInset - 0.5 );
    if ( borderEnabled_ ) {
        QPen borderPen( QColor( 0, 0, 0, 164 ) );
        borderPen.setWidthF( 0.6 );
        borderPen.setCosmetic( true );
        painter.setPen( borderPen );
    }
    else {
        painter.setPen( Qt::NoPen );
    }
    painter.setBrush( backgroundColor_ );
    painter.drawRoundedRect( panelRect, OverlayCornerRadius, OverlayCornerRadius );
}

void SliceOverlayWidget::clearContent()
{
    while ( contentLayout_->count() > 0 ) {
        QLayoutItem* item = contentLayout_->takeAt( 0 );
        if ( item->widget() ) {
            item->widget()->deleteLater();
        }
        delete item;
    }
}

void SliceOverlayWidget::updateMinimumDisplayHeight( int minimumVisibleLines )
{
    if ( contentLayout_ == nullptr ) {
        return;
    }

    const QFontMetrics metrics( font() );
    const int lineCount = qMax( 1, minimumVisibleLines );
    const int contentHeight = metrics.height() * lineCount;

    if ( contentWidget_ != nullptr ) {
        contentWidget_->setMinimumHeight( contentHeight );
    }

    if ( auto* rootLayout = qobject_cast<QVBoxLayout*>( layout() ); rootLayout != nullptr ) {
        const QMargins margins = rootLayout->contentsMargins();
        setMinimumHeight( margins.top() + margins.bottom() + contentHeight
                          + panelShadowInset( borderEnabled_ ) );
    }
}

QString SliceOverlayWidget::formatSliceTitle( const SliceLineRange& slice ) const
{
    return QString::fromStdString( slice.ruleName );
}

QString SliceOverlayWidget::formatSliceDuration( const SliceLineRange& slice ) const
{
    if ( slice.openEnded ) {
        return tr( "Open" );
    }

    const qint64 durationNs = slice.duration;
    if ( durationNs <= 0 ) {
        return tr( "0 ns" );
    }

    if ( durationNs < 1000 ) {
        return tr( "%1 ns" ).arg( durationNs );
    }

    const auto precisionFor
        = []( qint64 value, qint64 threshold ) { return value < threshold ? 2 : 1; };

    if ( durationNs < 1000 * 1000 ) {
        return tr( "%1 μs" ).arg( QString::number( static_cast<double>( durationNs ) / 1000.0, 'f',
                                                   precisionFor( durationNs, 10000 ) ) );
    }

    if ( durationNs < 1000LL * 1000LL * 1000LL ) {
        return tr( "%1 ms" ).arg( QString::number( static_cast<double>( durationNs ) / 1000000.0,
                                                   'f', precisionFor( durationNs, 10000000 ) ) );
    }
    return tr( "%1 s" ).arg( QString::number( static_cast<double>( durationNs ) / 1000000000.0, 'f',
                                              precisionFor( durationNs, 10000000000LL ) ) );
}

void SliceOverlayWidget::setSlices( const QVector<SliceLineRange>& slices, LineNumber line )
{
    Q_UNUSED( line )
    clearContent();

    for ( const auto& slice : slices ) {
        auto* label = new QLabel( contentWidget_ );
        label->setWordWrap( false );
        label->setFont( font() );
        label->setMinimumWidth( qMax( MinimumOverlayContentWidth, preferredDisplayWidth_ - 24 ) );
        label->setStyleSheet( QStringLiteral( "color: %1; background: transparent;" )
                                  .arg( textColor_.name( QColor::HexArgb ) ) );
        label->setText( QStringLiteral( "%1  ·  %2" )
                            .arg( formatSliceTitle( slice ), formatSliceDuration( slice ) ) );
        contentLayout_->addWidget( label );
    }
    contentLayout_->addStretch();
    updateMinimumDisplayHeight( 2 );
    adjustSize();
}

void SliceOverlayWidget::showEmptyState( LineNumber line )
{
    clearContent();
    Q_UNUSED( line )
    auto* label = new QLabel( tr( "No active slice" ), contentWidget_ );
    label->setWordWrap( true );
    label->setFont( font() );
    label->setMinimumWidth( qMax( MinimumOverlayContentWidth, preferredDisplayWidth_ - 24 ) );
    label->setStyleSheet( QStringLiteral( "color: %1; background: transparent;" )
                              .arg( textColor_.name( QColor::HexArgb ) ) );
    contentLayout_->addWidget( label );
    contentLayout_->addStretch();
    updateMinimumDisplayHeight( 2 );
    adjustSize();
}

void SliceOverlayWidget::setMaximumDisplayHeight( int height )
{
    maximumDisplayHeight_ = qMax( 120, height );
    if ( scrollArea_ != nullptr ) {
        scrollArea_->setMaximumHeight( maximumDisplayHeight_ );
    }
}

void SliceOverlayWidget::setPreferredDisplayWidth( int width )
{
    preferredDisplayWidth_ = qMax( MinimumOverlayWidth, width );
    setFixedWidth( preferredDisplayWidth_ );
    updateMinimumDisplayHeight( 2 );
    updateGeometry();
    resize( preferredDisplayWidth_, minimumHeight() );
}
