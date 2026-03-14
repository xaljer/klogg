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

#include <QLabel>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QScrollBar>
#include <QTimer>
#include <QPushButton>

namespace {

const int ChipRadius = 12;
const int ChipMargin = 4;
const int ChipPadding = 8;
const int ChipHeight = 24;
const int MinEditWidth = 150;

} // namespace

PatternInputWidget::PatternInputWidget( QWidget* parent )
    : QWidget( parent )
{
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    setFixedHeight( ChipHeight );

    mainLayout_ = new QHBoxLayout( this );
    mainLayout_->setContentsMargins( 0, 0, 0, 0 );
    mainLayout_->setSpacing( 0 );

    // Scroll area for chips and input
    chipsScrollArea_ = new QScrollArea( this );
    chipsScrollArea_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    chipsScrollArea_->setFixedHeight( ChipHeight );
    chipsScrollArea_->setWidgetResizable( true );
    chipsScrollArea_->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    chipsScrollArea_->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    chipsScrollArea_->setFrameShape( QFrame::NoFrame );
    chipsScrollArea_->setStyleSheet( "QScrollArea { background: transparent; }" );
    mainLayout_->addWidget( chipsScrollArea_, 1 );

    // Chips container inside scroll area (includes chips + line edit)
    chipsContainer_ = new QWidget();
    chipsContainer_->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Fixed );
    chipsContainer_->setFixedHeight( ChipHeight );
    chipsContainer_->setStyleSheet( "QWidget { background: transparent; }" );
    chipsLayout_ = new QHBoxLayout( chipsContainer_ );
    chipsLayout_->setContentsMargins( 0, 0, 0, 0 );
    chipsLayout_->setSpacing( ChipMargin );
    chipsLayout_->setSizeConstraint( QLayout::SetMinAndMaxSize );
    chipsScrollArea_->setWidget( chipsContainer_ );

    // Line edit for input - placed inside chips layout to follow chips
    lineEdit_ = new QLineEdit( chipsContainer_ );
    lineEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    lineEdit_->setMinimumWidth( MinEditWidth );
    lineEdit_->setFixedHeight( ChipHeight );
    lineEdit_->setStyleSheet(
        "QLineEdit { border: none; background: transparent; }"
        "QLineEdit:focus { border: 1px solid #0078d4; border-radius: 4px; }" );
    chipsLayout_->addWidget( lineEdit_, 1 );

    connect( lineEdit_, &QLineEdit::textChanged, this, &PatternInputWidget::onLineEditTextChanged );
    connect( lineEdit_, &QLineEdit::returnPressed, this, &PatternInputWidget::onLineEditReturnPressed );
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

void PatternInputWidget::setPlaceholderText( const QString& placeholder )
{
    lineEdit_->setPlaceholderText( placeholder );
}

QString PatternInputWidget::placeholderText() const
{
    return lineEdit_->placeholderText();
}

void PatternInputWidget::setRegexMode( bool isRegex )
{
    isRegexMode_ = isRegex;
}

bool PatternInputWidget::isRegexMode() const
{
    return isRegexMode_;
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
    patterns_.clear();
    clearChips();
}

void PatternInputWidget::clearChips()
{
    // Remove chip widgets but keep lineEdit_ in the layout
    QList<QWidget*> chipsToRemove;
    for ( int i = 0; i < chipsLayout_->count(); ++i ) {
        QLayoutItem* item = chipsLayout_->itemAt( i );
        if ( item && item->widget() && item->widget() != lineEdit_ ) {
            chipsToRemove.append( item->widget() );
        }
    }
    
    for ( QWidget* chipWidget : chipsToRemove ) {
        // Disconnect all signals before deleting to prevent callbacks on deleted objects
        chipWidget->disconnect();
        // Remove event filter to prevent handling events during deletion
        chipWidget->removeEventFilter( this );
        chipWidget->deleteLater();
    }
}

void PatternInputWidget::setChipMode( bool chipMode )
{
    if ( isChipMode_ != chipMode ) {
        isChipMode_ = chipMode;

        if ( chipMode ) {
            const QString currentText = lineEdit_->text();
            if ( !currentText.isEmpty() ) {
                parsePatterns( currentText );
                updateChips();
                lineEdit_->clear();
            }
            // In chip mode: lineEdit_ has minimum width, expands to fill remaining space
            lineEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
            lineEdit_->setMinimumWidth( MinEditWidth );
            lineEdit_->setMaximumWidth( QWIDGETSIZE_MAX );
        }
        else {
            if ( !patterns_.isEmpty() ) {
                lineEdit_->setText( combinePatterns() );
            }
            clearChips();
            patterns_.clear();
            // In normal mode: lineEdit_ fills all available space
            lineEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
            lineEdit_->setMinimumWidth( 0 );
            lineEdit_->setMaximumWidth( QWIDGETSIZE_MAX );
        }
        
        // Update container size
        chipsContainer_->adjustSize();
    }
}

bool PatternInputWidget::isChipMode() const
{
    return isChipMode_;
}

void PatternInputWidget::parsePatterns( const QString& text )
{
    patterns_.clear();
    if ( !text.isEmpty() ) {
        if ( isRegexMode_ ) {
            patterns_ = text.split( QLatin1Char( '|' ) );
        }
        else {
            patterns_ = text.split( QStringLiteral( " or " ) );
        }
    }
}

QString PatternInputWidget::combinePatterns() const
{
    if ( isRegexMode_ ) {
        return patterns_.join( QLatin1Char( '|' ) );
    }
    else {
        return patterns_.join( QStringLiteral( " or " ) );
    }
}

QWidget* PatternInputWidget::createChipWidget( const QString& text, int index )
{
    QWidget* chip = new QWidget( chipsContainer_ );
    chip->setProperty( "chipIndex", index );
    chip->setProperty( "chipText", text );
    chip->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
    chip->setFixedHeight( ChipHeight );
    chip->setMouseTracking( true );

    QHBoxLayout* layout = new QHBoxLayout( chip );
    layout->setContentsMargins( ChipPadding, 2, ChipPadding, 2 );
    layout->setSpacing( 4 );
    layout->setSizeConstraint( QLayout::SetFixedSize );

    QLabel* label = new QLabel( text, chip );
    label->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Preferred );
    label->setStyleSheet( "QLabel { background: transparent; }" );
    layout->addWidget( label );

    QPushButton* removeButton = new QPushButton( chip );
    removeButton->setText( QStringLiteral( "x" ) );
    removeButton->setFont( QFont( "Arial", 10 ) );
    removeButton->setFixedSize( 16, 16 );
    removeButton->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
    removeButton->setStyleSheet(
        "QPushButton { border: none; background: transparent; padding: 0px; font-weight: bold; color: #888; }"
        "QPushButton:hover { color: #d00; background: rgba(0,0,0,0.1); border-radius: 8px; }" );
    removeButton->setCursor( Qt::PointingHandCursor );
    removeButton->setProperty( "chipIndex", index );
    removeButton->hide();
    layout->addWidget( removeButton );

    chip->setStyleSheet(
        QString( "QWidget { background: #e0e0e0; border-radius: %1px; }" ).arg( ChipRadius ) );

    connect( removeButton, &QPushButton::clicked, this, &PatternInputWidget::onChipRemoveClicked );
    chip->installEventFilter( this );

    return chip;
}

void PatternInputWidget::updateChips()
{
    clearChips();

    // Insert chips before lineEdit_
    for ( int i = 0; i < patterns_.size(); ++i ) {
        const QString& pattern = patterns_.at( i );
        if ( !pattern.isEmpty() ) {
            QWidget* chip = createChipWidget( pattern, i );
            // Insert at position i (before lineEdit_ which is at the end)
            chipsLayout_->insertWidget( i, chip );
        }
    }

    // Update container width
    chipsContainer_->adjustSize();
}

void PatternInputWidget::addChip( const QString& pattern )
{
    if ( !pattern.isEmpty() && !patterns_.contains( pattern ) ) {
        patterns_.append( pattern );
        updateChips();
        scrollToEnd();
        Q_EMIT chipChanged( text() );
    }
}

void PatternInputWidget::removeChip( int index )
{
    if ( index >= 0 && index < patterns_.size() ) {
        patterns_.removeAt( index );
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
    
    // Check if this is a valid chip widget
    if ( !chip->property( "chipText" ).isValid() ) {
        return;
    }
    
    QString chipText = chip->property( "chipText" ).toString();
    if ( chipText.isEmpty() ) {
        return;
    }
    
    // Capture chipText instead of index to avoid index invalidation
    // if patterns_ changes between now and the deferred execution
    QTimer::singleShot( 0, this, [ this, chipText ]() {
        // Double-check that this object is still valid
        if ( !isChipMode_ ) {
            return;
        }
        int index = patterns_.indexOf( chipText );
        if ( index >= 0 ) {
            removeChip( index );
        }
    } );
}

void PatternInputWidget::onLineEditReturnPressed()
{
    const QString text = lineEdit_->text().trimmed();

    if ( isChipMode_ && !text.isEmpty() ) {
        addChip( text );
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
    return QWidget::eventFilter( obj, event );
}