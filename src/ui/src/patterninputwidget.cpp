/*
 * Copyright (C) 2024 The klogg developers
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "patterninputwidget.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QPushButton>
#include <QRegularExpression>

#include "log.h"

namespace {

const int ChipRadius = 12;
const int ChipMargin = 4;
const int ChipPadding = 8;
const int ChipHeight = 24;
const int MinEditWidth = 120;

QColor deriveColor( const QColor& base, int hueShift, int satAdjust = 0, int lightAdjust = 0 )
{
    int h, s, l, a;
    base.getHsl( &h, &s, &l, &a );
    h = ( h + hueShift ) % 360;
    if ( h < 0 ) {
        h += 360;
    }
    s = std::clamp( s + satAdjust, 0, 255 );
    l = std::clamp( l + lightAdjust, 0, 255 );
    QColor result;
    result.setHsl( h, s, l, a );
    return result;
}

QString chipDisplayText( const Chip& chip )
{
    switch ( chip.type ) {
    case ChipType::Or:
        return chip.terms.value( 0 );
    case ChipType::AndGroup:
        return chip.terms.join( QStringLiteral( " & " ) );
    case ChipType::Not:
        return QStringLiteral( "NOT " ) + chip.terms.value( 0 );
    case ChipType::NotAndGroup:
        return chip.terms.join( QStringLiteral( " & " ) );
    }
    return {};
}

QString chipPropertyText( const Chip& chip )
{
    // Used for chipText property (identification in remove/edit)
    switch ( chip.type ) {
    case ChipType::Or:
        return chip.terms.value( 0 );
    case ChipType::AndGroup:
        return chip.terms.join( QLatin1Char( '&' ) );
    case ChipType::Not:
        return QStringLiteral( "!" ) + chip.terms.value( 0 );
    case ChipType::NotAndGroup:
        return QStringLiteral( "!" ) + chip.terms.join( QLatin1Char( '&' ) );
    }
    return {};
}

} // namespace

PatternInputWidget::PatternInputWidget( QWidget* parent )
    : QWidget( parent )
{
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    setFixedHeight( ChipHeight );

    mainLayout_ = new QHBoxLayout( this );
    mainLayout_->setContentsMargins( 0, 0, 0, 0 );
    mainLayout_->setSpacing( 0 );

    chipsScrollArea_ = new QScrollArea( this );
    chipsScrollArea_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    chipsScrollArea_->setFixedHeight( ChipHeight );
    chipsScrollArea_->setWidgetResizable( true );
    chipsScrollArea_->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    chipsScrollArea_->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    chipsScrollArea_->setFrameShape( QFrame::NoFrame );
    chipsScrollArea_->setStyleSheet( "QScrollArea { background: transparent; }" );
    mainLayout_->addWidget( chipsScrollArea_, 1 );

    chipsContainer_ = new QWidget();
    chipsContainer_->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Fixed );
    chipsContainer_->setFixedHeight( ChipHeight );
    chipsContainer_->setStyleSheet( "QWidget { background: transparent; }" );
    chipsContainer_->installEventFilter( this );
    chipsLayout_ = new QHBoxLayout( chipsContainer_ );
    chipsLayout_->setContentsMargins( 0, 0, 0, 0 );
    chipsLayout_->setSpacing( ChipMargin );
    chipsLayout_->setSizeConstraint( QLayout::SetMinAndMaxSize );
    chipsScrollArea_->setWidget( chipsContainer_ );

    // NOT toggle button — placed before the input
    notButton_ = new QToolButton( chipsContainer_ );
    notButton_->setText( tr( "NOT" ) );
    notButton_->setCheckable( true );
    notButton_->setFixedHeight( ChipHeight );
    notButton_->setToolTip( tr( "Toggle to add exclude pattern" ) );
    notButton_->setStyleSheet(
        "QToolButton { border: none; border-radius: 10px; padding: 0 8px; "
        "font-size: 10px; font-weight: bold; background: transparent; color: #888; }"
        "QToolButton:hover { color: #ccc; }"
        "QToolButton:checked { background: #6b2020; color: #fff; }" );
    notButton_->hide();
    chipsLayout_->addWidget( notButton_ );
    connect( notButton_, &QToolButton::clicked, this, &PatternInputWidget::onNotButtonClicked );

    lineEdit_ = new QLineEdit( chipsContainer_ );
    lineEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    lineEdit_->setMinimumWidth( MinEditWidth );
    lineEdit_->setFixedHeight( ChipHeight );
    lineEdit_->setStyleSheet(
        "QLineEdit { border: none; background: transparent; }"
        "QLineEdit:focus { border: 1px solid #0078d4; border-radius: 4px; }" );
    chipsLayout_->addWidget( lineEdit_, 1 );

    connect( lineEdit_, &QLineEdit::textChanged, this, &PatternInputWidget::onLineEditTextChanged );
    connect( lineEdit_, &QLineEdit::returnPressed, this,
             &PatternInputWidget::onLineEditReturnPressed );

    lineEdit_->setContextMenuPolicy( Qt::CustomContextMenu );
    connect( lineEdit_, &QWidget::customContextMenuRequested, this,
             [ this ]( const QPoint& pos ) {
                 Q_EMIT contextMenuRequested( lineEdit_->mapToGlobal( pos ) );
             } );
    chipsScrollArea_->setContextMenuPolicy( Qt::CustomContextMenu );
    connect( chipsScrollArea_, &QWidget::customContextMenuRequested, this,
             [ this ]( const QPoint& pos ) {
                 Q_EMIT contextMenuRequested( chipsScrollArea_->mapToGlobal( pos ) );
             } );

    historyButton_ = new QToolButton( this );
    historyButton_->setText( QString( QChar( 0x25BE ) ) );
    historyButton_->setFixedSize( 16, ChipHeight );
    historyButton_->setToolTip( tr( "Search history" ) );
    historyButton_->setAutoRaise( true );
    historyButton_->hide();
    mainLayout_->addWidget( historyButton_ );

    connect( historyButton_, &QToolButton::clicked, this,
             &PatternInputWidget::showHistoryMenu );
}

QString PatternInputWidget::text() const
{
    if ( isChipMode_ ) {
        return combinePatterns();
    }
    return lineEdit_->text();
}

void PatternInputWidget::setText( const QString& text )
{
    if ( isChipMode_ ) {
        parsePatterns( text );
        updateChips();
        lineEdit_->clear();
        scrollToEnd();
    }
    else {
        lineEdit_->setText( text );
    }
}

void PatternInputWidget::setChips( const QVector<Chip>& chips )
{
    if ( isChipMode_ ) {
        chips_.clear();
        for ( const auto& chip : chips ) {
            if ( !chip.terms.isEmpty() && !chip.terms.first().isEmpty() ) {
                chips_.append( chip );
            }
        }
        updateChips();
        lineEdit_->clear();
        scrollToEnd();
    }
    else {
        lineEdit_->setText( combinePatterns() );
    }
}

QVector<Chip> PatternInputWidget::chips() const
{
    if ( isChipMode_ ) {
        return chips_;
    }
    if ( lineEdit_->text().isEmpty() ) {
        return {};
    }
    return { Chip{ ChipType::Or, { lineEdit_->text() } } };
}

void PatternInputWidget::setPlaceholderText( const QString& placeholder )
{
    lineEdit_->setPlaceholderText( placeholder );
}

QString PatternInputWidget::placeholderText() const
{
    return lineEdit_->placeholderText();
}

void PatternInputWidget::focusInput( Qt::FocusReason reason )
{
    lineEdit_->setFocus( reason );
}

void PatternInputWidget::setRegexMode( bool isRegex )
{
    isRegexMode_ = isRegex;
}

bool PatternInputWidget::isRegexMode() const
{
    return isRegexMode_;
}

void PatternInputWidget::setBooleanMode( bool isBoolean )
{
    isBooleanMode_ = isBoolean;
}

bool PatternInputWidget::isBooleanMode() const
{
    return isBooleanMode_;
}

void PatternInputWidget::setReadOnly( bool readOnly )
{
    isReadOnly_ = readOnly;
    lineEdit_->setReadOnly( readOnly );

    for ( auto chip : chipsContainer_->findChildren<QWidget*>() ) {
        chip->setEnabled( !readOnly );
    }
}

bool PatternInputWidget::isReadOnly() const
{
    return isReadOnly_;
}

void PatternInputWidget::clear()
{
    lineEdit_->clear();
    chips_.clear();
    clearChips();
}

void PatternInputWidget::clearChips()
{
    QList<QWidget*> chipsToRemove;
    for ( int i = 0; i < chipsLayout_->count(); ++i ) {
        QLayoutItem* item = chipsLayout_->itemAt( i );
        if ( item && item->widget() && item->widget() != lineEdit_
             && item->widget() != notButton_ ) {
            chipsToRemove.append( item->widget() );
        }
    }

    for ( QWidget* chipWidget : chipsToRemove ) {
        chipWidget->disconnect();
        chipWidget->removeEventFilter( this );
        chipWidget->deleteLater();
    }
}

void PatternInputWidget::setChipMode( bool chipMode )
{
    if ( isChipMode_ != chipMode ) {
        isChipMode_ = chipMode;

        if ( chipMode ) {
            // UI setup only — caller is responsible for setText()
            lineEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
            lineEdit_->setMinimumWidth( MinEditWidth );
            lineEdit_->setMaximumWidth( QWIDGETSIZE_MAX );
            historyButton_->show();
        }
        else {
            // Save expression to line edit, clear chips
            if ( !chips_.isEmpty() ) {
                lineEdit_->setText( combinePatterns() );
            }
            else {
                lineEdit_->clear();
            }
            clearChips();
            chips_.clear();
            notButton_->setChecked( false );
            notButtonLit_ = false;
            notButton_->hide();
            lineEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
            lineEdit_->setMinimumWidth( 0 );
            lineEdit_->setMaximumWidth( QWIDGETSIZE_MAX );
            historyButton_->hide();
        }

        chipsContainer_->adjustSize();
    }
}

bool PatternInputWidget::isChipMode() const
{
    return isChipMode_;
}

void PatternInputWidget::setSearchCompleter( QCompleter* completer )
{
    completer->setCompletionMode( QCompleter::PopupCompletion );
    lineEdit_->setCompleter( completer );
}

void PatternInputWidget::setNotButtonVisible( bool visible )
{
    notButton_->setVisible( visible );
}

bool PatternInputWidget::isNotButtonLit() const
{
    return notButtonLit_;
}

void PatternInputWidget::onNotButtonClicked()
{
    notButtonLit_ = notButton_->isChecked();
    Q_EMIT notButtonToggled( notButtonLit_ );
}

void PatternInputWidget::showHistoryMenu()
{
    if ( auto* completer = lineEdit_->completer() ) {
        completer->setCompletionPrefix( QString() );
        completer->complete();
        auto* popup = completer->popup();
        popup->resize( this->width(), popup->height() );
        popup->move( mapToGlobal( QPoint( 0, height() ) ) );
    }
}

// ── Boolean Expression ↔ Chip Conversion ──

bool PatternInputWidget::canParseToChips( const QString& booleanExpression )
{
    if ( booleanExpression.isEmpty() ) {
        return true;
    }

    // Extract quoted patterns and check operators
    // Unsupported: xor, xnor, mixed AND-OR at same level without grouping,
    // not() wrapping complex groups
    static const QRegularExpression unsupportedOp(
        QStringLiteral( R"(\b(xor|xnor)\b)" ),
        QRegularExpression::CaseInsensitiveOption );

    if ( unsupportedOp.match( booleanExpression ).hasMatch() ) {
        return false;
    }

    // Check for mixed AND/OR at top level (ambiguous without explicit grouping)
    // Simple heuristic: if we detect both " and " and " or " outside quotes,
    // and no parentheses to disambiguate, it's unsupported
    QString unquoted = booleanExpression;
    // Remove quoted content for operator check
    static const QRegularExpression quoted( QStringLiteral( R"re("[^"]*")re" ) );
    unquoted.replace( quoted, QStringLiteral( "?" ) );

    const bool hasAnd = unquoted.contains( QStringLiteral( " and " ), Qt::CaseInsensitive );
    const bool hasOr = unquoted.contains( QStringLiteral( " or " ), Qt::CaseInsensitive );
    const bool hasParen = unquoted.contains( QLatin1Char( '(' ) );

    // Mixed AND/OR without parentheses is ambiguous
    if ( hasAnd && hasOr && !hasParen ) {
        return false;
    }

    // not() wrapping a complex group (more than one operator inside)
    static const QRegularExpression notWrapGroup(
        QStringLiteral(
            R"re(not\s*\(\s*"[^"]*"\s*(?:and|or)\s*"[^"]*"\s*(?:and|or)\s*)re" ),
        QRegularExpression::CaseInsensitiveOption );
    if ( notWrapGroup.match( booleanExpression ).hasMatch() ) {
        return false;
    }

    return true;
}

void PatternInputWidget::parsePatterns( const QString& text )
{
    chips_.clear();
    if ( text.isEmpty() ) {
        return;
    }

    if ( !canParseToChips( text ) ) {
        // Fallback: store entire expression as a single pattern
        chips_.append( Chip{ ChipType::Or, { text } } );
        return;
    }

    if ( isRegexMode_ && !isBooleanMode_ ) {
        // Regex-only mode: simple | split
        const auto parts = text.split( QLatin1Char( '|' ) );
        for ( const auto& part : parts ) {
            const auto trimmed = part.trimmed();
            if ( !trimmed.isEmpty() ) {
                chips_.append( Chip{ ChipType::Or, { trimmed } } );
            }
        }
        return;
    }

    // Boolean expression parsing
    // Extract all quoted patterns in order
    static const QRegularExpression quotedPattern(
        QStringLiteral( R"re("((?:[^"\\]|\\.)*)")re" ) );
    QStringList quotedTerms;
    auto it = quotedPattern.globalMatch( text );
    while ( it.hasNext() ) {
        auto match = it.next();
        auto term = match.captured( 1 );
        term.replace( QStringLiteral( "\\\"" ), QStringLiteral( "\"" ) );
        quotedTerms.append( term );
    }

    if ( quotedTerms.isEmpty() ) {
        // No quoted patterns — treat entire string as one chip
        chips_.append( Chip{ ChipType::Or, { text } } );
        return;
    }

    // Analyze structure between the quoted patterns
    QString residual = text;
    residual.replace( quotedPattern, QStringLiteral( "?" ) );

    // Detect nand / nor for auto-conversion
    const auto nandCount = residual.count( QStringLiteral( " nand " ), Qt::CaseInsensitive );
    const auto norCount = residual.count( QStringLiteral( " nor " ), Qt::CaseInsensitive );

    if ( nandCount > 0 ) {
        // "A nand B" → not(A and B) → NotAndGroup
        chips_.append( Chip{ ChipType::NotAndGroup, quotedTerms } );
        return;
    }

    if ( norCount > 0 ) {
        // "A nor B" → not(A or B) → two Not chips
        for ( const auto& term : quotedTerms ) {
            chips_.append( Chip{ ChipType::Not, { term } } );
        }
        return;
    }

    // ── Phase 1: Extract exclude terms from not("...") patterns ──
    static const QRegularExpression notTermRe(
        QStringLiteral( R"re(not\s*\(\s*"((?:[^"\\]|\\.)*)"\s*\))re" ),
        QRegularExpression::CaseInsensitiveOption );

    QStringList excludeTerms;
    QString textWithoutNot = text;
    auto notIt = notTermRe.globalMatch( text );
    while ( notIt.hasNext() ) {
        auto m = notIt.next();
        auto term = m.captured( 1 );
        term.replace( QStringLiteral( "\\\"" ), QStringLiteral( "\"" ) );
        excludeTerms.append( term );
        textWithoutNot.replace( m.captured(), QString() );
    }

    // Clean up trailing/leading " and " or " or " left after removing not() parts
    static const QRegularExpression trailingAndOr(
        QStringLiteral( R"re(^\s*(?:and|or)\s+|\s+(?:and|or)\s*$)re" ),
        QRegularExpression::CaseInsensitiveOption );
    textWithoutNot.replace( trailingAndOr, QString() );

    // Check for NOT-and-group: not("A" and "B")
    static const QRegularExpression notAndGroupRe(
        QStringLiteral(
            R"re(not\s*\(\s*"((?:[^"\\]|\\.)*)"\s+and\s+"((?:[^"\\]|\\.)*)"\s*\))re" ),
        QRegularExpression::CaseInsensitiveOption );
    auto nagMatch = notAndGroupRe.match( text );
    if ( nagMatch.hasMatch() ) {
        auto t1 = nagMatch.captured( 1 );
        auto t2 = nagMatch.captured( 2 );
        t1.replace( QStringLiteral( "\\\"" ), QStringLiteral( "\"" ) );
        t2.replace( QStringLiteral( "\\\"" ), QStringLiteral( "\"" ) );
        chips_.append( Chip{ ChipType::NotAndGroup, { t1, t2 } } );
        textWithoutNot.replace( nagMatch.captured(), QString() );
        // Remove these from excludeTerms if present
        excludeTerms.removeAll( t1 );
        excludeTerms.removeAll( t2 );
    }

    // Add simple Not chips for remaining exclude terms
    for ( const auto& term : excludeTerms ) {
        chips_.append( Chip{ ChipType::Not, { term } } );
    }

    // ── Phase 2: Parse include expression (text without not() parts) ──
    // Extract remaining quoted terms from the include-only text
    QStringList includeTerms;
    auto it2 = quotedPattern.globalMatch( textWithoutNot );
    while ( it2.hasNext() ) {
        auto match = it2.next();
        auto term = match.captured( 1 );
        term.replace( QStringLiteral( "\\\"" ), QStringLiteral( "\"" ) );
        includeTerms.append( term );
    }

    if ( includeTerms.isEmpty() ) {
        return;
    }

    // Determine operator between include terms.
    // Check for parenthesized AND-group: ("A" and "B") or "C"
    static const QRegularExpression parenAndGroup(
        QStringLiteral( R"re(\(\s*"((?:[^"\\]|\\.)*)"\s+and\s+"((?:[^"\\]|\\.)*)"\s*\))re" ),
        QRegularExpression::CaseInsensitiveOption );
    auto parenMatch = parenAndGroup.match( textWithoutNot );

    QVector<Chip> includeChips;

    if ( parenMatch.hasMatch() ) {
        // Extract terms from the parenthesized AND-group
        auto t1 = parenMatch.captured( 1 );
        auto t2 = parenMatch.captured( 2 );
        t1.replace( QStringLiteral( "\\\"" ), QStringLiteral( "\"" ) );
        t2.replace( QStringLiteral( "\\\"" ), QStringLiteral( "\"" ) );
        includeChips.append( Chip{ ChipType::AndGroup, { t1, t2 } } );

        // Remove the AND-group part and find remaining top-level OR terms
        QString remainingText = textWithoutNot;
        remainingText.replace( parenMatch.captured(), QString() );
        auto remIt = quotedPattern.globalMatch( remainingText );
        while ( remIt.hasNext() ) {
            auto match = remIt.next();
            auto term = match.captured( 1 );
            term.replace( QStringLiteral( "\\\"" ), QStringLiteral( "\"" ) );
            if ( term != t1 && term != t2 ) {
                includeChips.append( Chip{ ChipType::Or, { term } } );
            }
        }
    }
    else {
        // No parenthesized AND-group — use top-level operator
        QString residualInclude = textWithoutNot;
        residualInclude.replace( quotedPattern, QStringLiteral( "?" ) );
        const auto includeAndCount
            = residualInclude.count( QStringLiteral( " and " ), Qt::CaseInsensitive );

        if ( includeAndCount > 0 && includeTerms.size() >= 2 ) {
            // Pure AND between include terms → AndGroup
            includeChips.append( Chip{ ChipType::AndGroup, includeTerms } );
        }
        else {
            // OR or single term → individual Or chips
            for ( const auto& term : includeTerms ) {
                includeChips.append( Chip{ ChipType::Or, { term } } );
            }
        }
    }

    // Prepend include chips before exclude chips
    for ( int i = includeChips.size() - 1; i >= 0; --i ) {
        chips_.insert( 0, includeChips.at( i ) );
    }
}

QString PatternInputWidget::combinePatterns() const
{
    if ( chips_.isEmpty() ) {
        return {};
    }

    if ( isRegexMode_ && !isBooleanMode_ ) {
        QStringList terms;
        for ( const auto& chip : chips_ ) {
            for ( const auto& term : chip.terms ) {
                terms.append( term );
            }
        }
        return terms.join( QLatin1Char( '|' ) );
    }

    // Separate include and exclude chips
    QVector<Chip> includeChips;
    QVector<Chip> excludeChips;
    for ( const auto& chip : chips_ ) {
        if ( chip.type == ChipType::Not || chip.type == ChipType::NotAndGroup ) {
            excludeChips.append( chip );
        }
        else {
            includeChips.append( chip );
        }
    }

    QStringList parts;

    // Include part
    if ( !includeChips.isEmpty() ) {
        QStringList includeParts;
        for ( const auto& chip : includeChips ) {
            if ( chip.type == ChipType::AndGroup && chip.terms.size() > 1 ) {
                QStringList quoted;
                for ( const auto& t : chip.terms ) {
                    auto escaped = t;
                    escaped.replace( QLatin1Char( '"' ), QStringLiteral( "\\\"" ) );
                    quoted.append( QStringLiteral( "\"" ) + escaped + QStringLiteral( "\"" ) );
                }
                includeParts.append( QStringLiteral( "(" ) + quoted.join( QStringLiteral( " and " ) )
                                     + QStringLiteral( ")" ) );
            }
            else {
                auto escaped = chip.terms.value( 0 );
                escaped.replace( QLatin1Char( '"' ), QStringLiteral( "\\\"" ) );
                includeParts.append( QStringLiteral( "\"" ) + escaped + QStringLiteral( "\"" ) );
            }
        }

        if ( includeParts.size() == 1 ) {
            parts.append( includeParts.first() );
        }
        else {
            parts.append( QStringLiteral( "(" ) + includeParts.join( QStringLiteral( " or " ) )
                          + QStringLiteral( ")" ) );
        }
    }

    // Exclude part
    for ( const auto& chip : excludeChips ) {
        if ( chip.type == ChipType::NotAndGroup && chip.terms.size() > 1 ) {
            QStringList quoted;
            for ( const auto& t : chip.terms ) {
                auto escaped = t;
                escaped.replace( QLatin1Char( '"' ), QStringLiteral( "\\\"" ) );
                quoted.append( QStringLiteral( "\"" ) + escaped + QStringLiteral( "\"" ) );
            }
            parts.append( QStringLiteral( "not(" ) + quoted.join( QStringLiteral( " and " ) )
                          + QStringLiteral( ")" ) );
        }
        else {
            auto escaped = chip.terms.value( 0 );
            escaped.replace( QLatin1Char( '"' ), QStringLiteral( "\\\"" ) );
            parts.append( QStringLiteral( "not(\"" ) + escaped + QStringLiteral( "\")" ) );
        }
    }

    if ( parts.isEmpty() ) {
        return {};
    }

    auto result = parts.join( QStringLiteral( " and " ) );
    return result;
}

// ── Chip Rendering ──

QColor PatternInputWidget::chipBackgroundColor( const Chip& chip ) const
{
    const QColor highlight = palette().color( QPalette::Highlight );

    switch ( chip.type ) {
    case ChipType::Or:
        return highlight;
    case ChipType::AndGroup:
        // Green hue shift from Highlight
        return deriveColor( highlight, 120, -10, 10 );
    case ChipType::Not:
        // Red hue shift
        return deriveColor( highlight, 180, 20, -5 );
    case ChipType::NotAndGroup:
        // Red hue shift with slight variation
        return deriveColor( highlight, 180, 20, -5 );
    }
    return highlight;
}

QWidget* PatternInputWidget::createChipWidget( const Chip& chip, int index )
{
    QWidget* widget = new QWidget( chipsContainer_ );
    widget->setProperty( "chipIndex", index );
    widget->setProperty( "chipText", chipPropertyText( chip ) );
    widget->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
    widget->setFixedHeight( ChipHeight );
    widget->setMouseTracking( true );

    QHBoxLayout* layout = new QHBoxLayout( widget );
    layout->setContentsMargins( ChipPadding, 2, ChipPadding, 2 );
    layout->setSpacing( 4 );
    layout->setSizeConstraint( QLayout::SetFixedSize );

    const QString displayText = chipDisplayText( chip );
    QLabel* label = new QLabel( displayText, widget );
    label->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Preferred );
    label->setStyleSheet( "QLabel { background: transparent; }" );
    label->setCursor( Qt::PointingHandCursor );
    label->installEventFilter( this );
    layout->addWidget( label );

    QPushButton* removeButton = new QPushButton( widget );
    removeButton->setText( QStringLiteral( "x" ) );
    removeButton->setFont( QFont( "Arial", 10 ) );
    removeButton->setFixedSize( 16, 16 );
    removeButton->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
    removeButton->setStyleSheet(
        "QPushButton { border: none; background: transparent; padding: 0px; font-weight: bold; "
        "color: rgba(255,255,255,0.7); }"
        "QPushButton:hover { color: rgba(255,255,255,1.0); background: rgba(0,0,0,0.2); "
        "border-radius: 8px; }" );
    removeButton->setCursor( Qt::PointingHandCursor );
    removeButton->setProperty( "chipIndex", index );
    removeButton->hide();
    layout->addWidget( removeButton );

    const QColor bgColor = chipBackgroundColor( chip );
    const QString fgColor = ( bgColor.lightness() > 128 ) ? QStringLiteral( "#1e1e1e" )
                                                            : QStringLiteral( "#ffffff" );
    QString style = QString( "QWidget { background: %1; color: %2; border-radius: %3px; }" )
                        .arg( bgColor.name(), fgColor )
                        .arg( ChipRadius );

    // AND-group and NOT-AND-group get a subtle border
    if ( chip.type == ChipType::AndGroup || chip.type == ChipType::NotAndGroup ) {
        style += QString( " QWidget { border: 1px solid %1; }" )
                     .arg( bgColor.lighter( 140 ).name() );
    }

    widget->setStyleSheet( style );

    connect( removeButton, &QPushButton::clicked, this,
             &PatternInputWidget::onChipRemoveClicked );
    widget->installEventFilter( this );

    return widget;
}

void PatternInputWidget::updateChips()
{
    clearChips();

    for ( int i = 0; i < chips_.size(); ++i ) {
        if ( !chips_[ i ].terms.isEmpty() ) {
            QWidget* chip = createChipWidget( chips_[ i ], i );
            chipsLayout_->insertWidget( i, chip );
        }
    }

    chipsContainer_->adjustSize();
}

void PatternInputWidget::addChipFromText( const QString& pattern )
{
    if ( pattern.isEmpty() ) {
        return;
    }

    // Determine chip type based on NOT button state
    ChipType type = notButtonLit_ ? ChipType::Not : ChipType::Or;

    // Check for & in the text → AND-group
    if ( pattern.contains( QLatin1Char( '&' ) ) ) {
        const auto parts = pattern.split( QLatin1Char( '&' ) );
        QStringList terms;
        for ( const auto& part : parts ) {
            const auto trimmed = part.trimmed();
            if ( !trimmed.isEmpty() ) {
                terms.append( trimmed );
            }
        }
        if ( terms.size() > 1 ) {
            type = notButtonLit_ ? ChipType::NotAndGroup : ChipType::AndGroup;
        }
    }

    // Check for regex-mode | split
    if ( isRegexMode_ ) {
        const auto parts = pattern.split( QLatin1Char( '|' ) );
        bool changed = false;
        for ( const auto& part : parts ) {
            const auto trimmed = part.trimmed();
            if ( trimmed.isEmpty() ) {
                continue;
            }
            Chip chip{ type, { trimmed } };
            if ( !chips_.contains( chip ) ) {
                chips_.append( chip );
                changed = true;
            }
        }
        if ( changed ) {
            updateChips();
            scrollToEnd();
            Q_EMIT chipChanged( text() );
        }
    }
    else {
        // Simple add with dedup
        const QStringList terms = type == ChipType::AndGroup || type == ChipType::NotAndGroup
                                      ? pattern.split( QLatin1Char( '&' ) )
                                      : QStringList{ pattern };
        QStringList cleaned;
        for ( const auto& t : terms ) {
            const auto trimmed = t.trimmed();
            if ( !trimmed.isEmpty() ) {
                cleaned.append( trimmed );
            }
        }
        if ( cleaned.isEmpty() ) {
            return;
        }

        Chip chip{ type, cleaned };
        // Dedup
        bool exists = false;
        for ( const auto& existing : std::as_const( chips_ ) ) {
            if ( existing.type == chip.type && existing.terms == chip.terms ) {
                exists = true;
                break;
            }
        }
        if ( !exists ) {
            chips_.append( chip );
            updateChips();
            scrollToEnd();
            Q_EMIT chipChanged( text() );
        }
    }

    // Reset NOT button after adding chip
    if ( notButtonLit_ ) {
        notButton_->setChecked( false );
        notButtonLit_ = false;
    }
}

void PatternInputWidget::removeChip( int index )
{
    if ( index >= 0 && index < chips_.size() ) {
        chips_.removeAt( index );
        updateChips();
        Q_EMIT chipChanged( text() );
    }
}

void PatternInputWidget::scrollToEnd()
{
    QTimer::singleShot( 0, this, [ this ]() {
        chipsScrollArea_->horizontalScrollBar()->setValue(
            chipsScrollArea_->horizontalScrollBar()->maximum() );
    } );
}

void PatternInputWidget::onChipRemoveClicked()
{
    QPushButton* button = qobject_cast<QPushButton*>( QObject::sender() );
    if ( !button ) {
        return;
    }

    QWidget* chip = button->parentWidget();
    if ( !chip ) {
        return;
    }

    if ( !chip->property( "chipIndex" ).isValid() ) {
        return;
    }

    bool ok;
    int index = chip->property( "chipIndex" ).toInt( &ok );
    if ( !ok ) {
        return;
    }

    if ( editingChipEdit_ && editingChipEdit_->parentWidget() == chip ) {
        editingChipEdit_->setProperty( "editCommitted", true );
        finishChipEdit( editingChipEdit_, false );
    }

    QTimer::singleShot( 0, this, [ this, index ]() {
        if ( !isChipMode_ ) {
            return;
        }
        if ( index >= 0 && index < chips_.size() ) {
            removeChip( index );
        }
    } );
}

void PatternInputWidget::onLineEditReturnPressed()
{
    const QString text = lineEdit_->text().trimmed();

    if ( isChipMode_ && !text.isEmpty() ) {
        addChipFromText( text );
        lineEdit_->clear();
    }
    Q_EMIT returnPressed();
}

void PatternInputWidget::onLineEditTextChanged( const QString& text )
{
    Q_UNUSED( text )
    if ( !isChipMode_ ) {
        Q_EMIT textChanged( lineEdit_->text() );
    }
}

// ── Event Filter ──

bool PatternInputWidget::eventFilter( QObject* obj, QEvent* event )
{
    if ( event->type() == QEvent::Enter || event->type() == QEvent::Leave ) {
        QWidget* chip = qobject_cast<QWidget*>( obj );
        if ( chip && chip->property( "chipText" ).isValid() ) {
            QPushButton* removeButton = chip->findChild<QPushButton*>();
            if ( removeButton ) {
                if ( event->type() == QEvent::Enter ) {
                    removeButton->show();
                }
                else {
                    removeButton->hide();
                }
            }
        }
    }
    else if ( event->type() == QEvent::MouseButtonPress
              && static_cast<QMouseEvent*>( event )->button() == Qt::LeftButton ) {
        QLabel* label = qobject_cast<QLabel*>( obj );
        if ( label ) {
            QWidget* chip = qobject_cast<QWidget*>( label->parent() );
            if ( chip && chip->property( "chipText" ).isValid() ) {
                startChipEdit( label );
                return true;
            }
        }
    }
    else if ( event->type() == QEvent::KeyPress ) {
        QLineEdit* edit = qobject_cast<QLineEdit*>( obj );
        if ( edit && edit->property( "chipIndex" ).isValid() ) {
            auto* keyEvent = static_cast<QKeyEvent*>( event );
            if ( keyEvent->key() == Qt::Key_Escape ) {
                finishChipEdit( edit, false );
                return true;
            }
        }
    }
    else if ( event->type() == QEvent::FocusOut ) {
        QLineEdit* edit = qobject_cast<QLineEdit*>( obj );
        if ( edit && edit->property( "chipIndex" ).isValid()
             && !edit->property( "editFinished" ).toBool() ) {
            edit->setProperty( "editFinished", true );
            finishChipEdit( edit, false );
        }
    }
    else if ( event->type() == QEvent::ContextMenu ) {
        auto* contextEvent = static_cast<QContextMenuEvent*>( event );
        Q_EMIT contextMenuRequested( contextEvent->globalPos() );
        return true;
    }
    return QWidget::eventFilter( obj, event );
}

// ── Inline Chip Editing ──

void PatternInputWidget::startChipEdit( QLabel* label )
{
    if ( isReadOnly_ ) {
        return;
    }

    QWidget* chip = qobject_cast<QWidget*>( label->parent() );
    if ( !chip ) {
        return;
    }

    bool ok;
    int chipIndex = chip->property( "chipIndex" ).toInt( &ok );
    if ( !ok || chipIndex < 0 || chipIndex >= chips_.size() ) {
        return;
    }

    const QString originalText = label->text();
    QHBoxLayout* layout = qobject_cast<QHBoxLayout*>( chip->layout() );
    if ( !layout ) {
        return;
    }

    const int labelIndex = layout->indexOf( label );
    layout->removeWidget( label );
    label->removeEventFilter( this );
    label->deleteLater();

    const QColor bgColor = chipBackgroundColor( chips_.at( chipIndex ) );
    QLineEdit* edit = new QLineEdit( originalText, chip );
    edit->setProperty( "chipIndex", chipIndex );
    edit->setProperty( "originalText", originalText );
    edit->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Fixed );
    edit->setFixedHeight( ChipHeight );
    edit->setStyleSheet(
        QString( "QLineEdit { border: 1px solid %1; border-radius: 4px; background: %2; "
                 "color: %3; padding-left: 2px; }" )
            .arg( bgColor.darker( 130 ).name(), bgColor.name(),
                  ( bgColor.lightness() > 128 ) ? QStringLiteral( "#1e1e1e" )
                                                 : QStringLiteral( "#ffffff" ) ) );
    edit->selectAll();
    edit->installEventFilter( this );

    layout->insertWidget( labelIndex, edit );
    editingChipEdit_ = edit;

    connect( edit, &QLineEdit::returnPressed, this, [ this, edit ]() {
        edit->setProperty( "editCommitted", true );
        finishChipEdit( edit, true );
    } );
    connect( edit, &QLineEdit::editingFinished, this, [ this, edit ]() {
        if ( !edit->property( "editCommitted" ).toBool() ) {
            finishChipEdit( edit, false );
        }
    } );

    edit->setFocus();
}

void PatternInputWidget::finishChipEdit( QLineEdit* edit, bool accept )
{
    if ( !edit ) {
        return;
    }

    if ( editingChipEdit_ == edit ) {
        editingChipEdit_ = nullptr;
    }

    edit->setProperty( "editFinished", true );
    edit->blockSignals( true );

    QWidget* chip = qobject_cast<QWidget*>( edit->parent() );
    if ( !chip ) {
        edit->deleteLater();
        return;
    }

    bool ok;
    int chipIndex = chip->property( "chipIndex" ).toInt( &ok );
    if ( !ok ) {
        edit->deleteLater();
        return;
    }

    const QString originalText = edit->property( "originalText" ).toString();
    QString newText = accept ? edit->text().trimmed() : originalText;

    QHBoxLayout* layout = qobject_cast<QHBoxLayout*>( chip->layout() );
    if ( !layout ) {
        edit->deleteLater();
        return;
    }

    const int editIndex = layout->indexOf( edit );
    if ( editIndex < 0 ) {
        edit->deleteLater();
        return;
    }

    edit->removeEventFilter( this );
    layout->removeWidget( edit );
    edit->deleteLater();

    QLabel* label = new QLabel( newText, chip );
    label->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Preferred );
    label->setStyleSheet( "QLabel { background: transparent; }" );
    label->setCursor( Qt::PointingHandCursor );
    label->installEventFilter( this );
    layout->insertWidget( editIndex, label );

    if ( accept && newText != originalText && chipIndex >= 0 && chipIndex < chips_.size() ) {
        if ( !newText.isEmpty() ) {
            auto& existingChip = chips_[ chipIndex ];
            // For simple Or/Not chips, update the term
            if ( existingChip.type == ChipType::Or || existingChip.type == ChipType::Not ) {
                existingChip.terms = QStringList{ newText };
            }
            else {
                // For AndGroup/NotAndGroup, split by &
                const auto parts = newText.split( QLatin1Char( '&' ) );
                QStringList terms;
                for ( const auto& part : parts ) {
                    const auto trimmed = part.trimmed();
                    if ( !trimmed.isEmpty() ) {
                        terms.append( trimmed );
                    }
                }
                if ( !terms.isEmpty() ) {
                    existingChip.terms = terms;
                }
            }
            chip->setProperty( "chipText", chipPropertyText( existingChip ) );
            updateChips();
            Q_EMIT chipChanged( text() );
        }
        else {
            removeChip( chipIndex );
        }
    }
}
