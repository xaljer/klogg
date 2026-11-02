/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
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

#include <QApplication>
#include <QColorDialog>
#include <QTimer>

#include <utility>

#include "highlighterset.h"
#include "highlightersetedit.h"

#include "containers.h"
#include "dispatch_to.h"
#include "iconloader.h"
#include "log.h"
#include "log_qt.h"
#include "colorutils.h"

namespace {
static constexpr const char* DEFAULT_PATTERN = "New Highlighter";
static constexpr bool DEFAULT_IGNORE_CASE = false;
static constexpr bool DEFAULT_ONLY_MATCH = false;

static constexpr klogg::Rgba DEFAULT_FORE_COLOUR( 0, 0, 0, 255 );
static constexpr klogg::Rgba DEFAULT_BACK_COLOUR; // invalid = use theme background
} // namespace

// Construct the box, including a copy of the global highlighterSet
// to handle ok/cancel/apply
HighlighterSetEdit::HighlighterSetEdit( QWidget* parent )
    : QWidget( parent )
{
    setupUi( this );

    highlighterEdit_
        = new HighlighterEdit( Highlighter{ "", DEFAULT_IGNORE_CASE, DEFAULT_ONLY_MATCH,
                                            DEFAULT_FORE_COLOUR, DEFAULT_BACK_COLOUR },
                               this );
    highlighterEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    highlighterLayout->addWidget( highlighterEdit_ );

    connect( nameEdit, &QLineEdit::textEdited, this, &HighlighterSetEdit::setName );

    connect( addHighlighterButton, &QToolButton::clicked, this,
             &HighlighterSetEdit::addHighlighter );
    connect( removeHighlighterButton, &QToolButton::clicked, this,
             &HighlighterSetEdit::removeHighlighter );

    connect( upHighlighterButton, &QToolButton::clicked, this,
             &HighlighterSetEdit::moveHighlighterUp );
    connect( downHighlighterButton, &QToolButton::clicked, this,
             &HighlighterSetEdit::moveHighlighterDown );

    // No highlighter selected by default
    selectedRow_ = -1;

    connect( highlighterListWidget, &QListWidget::itemSelectionChanged, this,
             &HighlighterSetEdit::updatePropertyFields );

    connect( highlighterEdit_, &HighlighterEdit::changed, this,
             &HighlighterSetEdit::updateHighlighterProperties );

    dispatchToMainThread( [ this ] {
        IconLoader iconLoader( this );

        addHighlighterButton->setIcon( iconLoader.load( "icons8-plus-16" ) );
        removeHighlighterButton->setIcon( iconLoader.load( "icons8-minus-16" ) );
        upHighlighterButton->setIcon( iconLoader.load( "icons8-up-16" ) );
        downHighlighterButton->setIcon( iconLoader.load( "icons8-down-arrow-16" ) );
    } );

    reset();
}

void HighlighterSetEdit::reset()
{
    addHighlighterButton->setEnabled( false );
    removeHighlighterButton->setEnabled( false );
    upHighlighterButton->setEnabled( false );
    downHighlighterButton->setEnabled( false );

    nameEdit->clear();
    nameEdit->setEnabled(false);
    highlighterListWidget->clear();

    highlighterEdit_->reset();
}

HighlighterSet HighlighterSetEdit::highlighters() const
{
    return highlighterSet_;
}

void HighlighterSetEdit::setHighlighters( HighlighterSet set )
{
    highlighterSet_ = std::move( set );
    populateHighlighterList();

    if ( !highlighterSet_.highlighters().empty() ) {
        setCurrentRow( 0 );
    }

    nameEdit->setEnabled(true);
    nameEdit->setText( QString::fromStdString( highlighterSet_.name() ) );
    addHighlighterButton->setEnabled(true);
}

void HighlighterSetEdit::setName( const QString& name )
{
    highlighterSet_.setName( name.toStdString() );
    Q_EMIT changed();
}

void HighlighterSetEdit::addHighlighter()
{
    LOG_DEBUG << "addHighlighter()";

    highlighterSet_.highlighters().push_back( Highlighter( DEFAULT_PATTERN, DEFAULT_IGNORE_CASE,
                                                             DEFAULT_ONLY_MATCH, DEFAULT_FORE_COLOUR,
                                                             DEFAULT_BACK_COLOUR ) );

    // Add and select the newly created highlighter
    highlighterListWidget->addItem( DEFAULT_PATTERN );

    setCurrentRow( highlighterListWidget->count() - 1 );

    Q_EMIT changed();
}

void HighlighterSetEdit::removeHighlighter()
{
    int index = highlighterListWidget->currentRow();
    LOG_DEBUG << "removeHighlighter() index " << index;

    if ( index >= 0 ) {
        setCurrentRow( -1 );
        highlighterSet_.highlighters().erase(
            highlighterSet_.highlighters().begin() + index );

        dispatchToMainThread( [ this, index ] {
            delete highlighterListWidget->takeItem( index );

            int count = highlighterListWidget->count();
            if ( index < count ) {
                // Select the new item at the same index
                setCurrentRow( index );
            }
            else {
                // or the previous index if it is at the end
                setCurrentRow( count - 1 );
            }
        } );

        Q_EMIT changed();
    }
}

void HighlighterSetEdit::moveHighlighterUp()
{
    int index = highlighterListWidget->currentRow();
    LOG_DEBUG << "moveHighlighterUp() index " << index;

    if ( index > 0 ) {
        std::swap( highlighterSet_.highlighters()[ static_cast<std::size_t>( index ) ],
                   highlighterSet_.highlighters()[ static_cast<std::size_t>( index - 1 ) ] );

        dispatchToMainThread( [ this, index ] {
            QListWidgetItem* item = highlighterListWidget->takeItem( index );
            highlighterListWidget->insertItem( index - 1, item );

            setCurrentRow( index - 1 );
        } );

        Q_EMIT changed();
    }
}

void HighlighterSetEdit::moveHighlighterDown()
{
    int index = highlighterListWidget->currentRow();
    LOG_DEBUG << "moveHighlighterDown() index " << index;

    if ( ( index >= 0 ) && ( index < ( highlighterListWidget->count() - 1 ) ) ) {
        std::swap( highlighterSet_.highlighters()[ static_cast<std::size_t>( index ) ],
                   highlighterSet_.highlighters()[ static_cast<std::size_t>( index + 1 ) ] );

        dispatchToMainThread( [ this, index ] {
            QListWidgetItem* item = highlighterListWidget->takeItem( index );
            highlighterListWidget->insertItem( index + 1, item );

            setCurrentRow( index + 1 );
        } );

        Q_EMIT changed();
    }
}

void HighlighterSetEdit::setCurrentRow( int row )
{
    // ugly hack for mac
    dispatchToMainThread( [ this, row ]() { highlighterListWidget->setCurrentRow( row ); } );
}

void HighlighterSetEdit::updatePropertyFields()
{
    if ( highlighterListWidget->selectedItems().count() >= 1 )
        selectedRow_ = highlighterListWidget->row( highlighterListWidget->selectedItems().at( 0 ) );
    else
        selectedRow_ = -1;

    LOG_DEBUG << "updatePropertyFields(), row = " << selectedRow_;

    if ( selectedRow_ >= 0 ) {
        const Highlighter& currentHighlighter
            = highlighterSet_.highlighters().at( static_cast<std::size_t>( selectedRow_ ) );
        highlighterEdit_->setHighlighter( currentHighlighter );

        // Enable the buttons if needed
        removeHighlighterButton->setEnabled( true );
        upHighlighterButton->setEnabled( selectedRow_ > 0 );
        downHighlighterButton->setEnabled( selectedRow_ < ( highlighterListWidget->count() - 1 ) );
    }
    else {
        highlighterEdit_->reset();
        
        removeHighlighterButton->setEnabled( false );
        upHighlighterButton->setEnabled( false );
        downHighlighterButton->setEnabled( false );
    }
}

void HighlighterSetEdit::updateHighlighterProperties()
{
    LOG_DEBUG << "updateHighlighterProperties()";

    // If a row is selected
    if ( selectedRow_ >= 0 ) {
        Highlighter& currentHighlighter
            = highlighterSet_.highlighters()[ static_cast<std::size_t>( selectedRow_ ) ];

        currentHighlighter = highlighterEdit_->highlighter();

        // Update the entry in the highlighterList widget
        highlighterListWidget->currentItem()->setText(
            QString::fromStdString( currentHighlighter.pattern() ) );
        highlighterListWidget->currentItem()->setForeground(
            QBrush( klogg::ui::qcolorFromRgba( currentHighlighter.foreColor() ) ) );
        const auto itemBackColor = currentHighlighter.backColor().a != 0
                                       ? klogg::ui::qcolorFromRgba(
                                             currentHighlighter.backColor() )
                                       : QApplication::palette().color( QPalette::Base );
        highlighterListWidget->currentItem()->setBackground( QBrush( itemBackColor ) );

        Q_EMIT changed();
    }
}

void HighlighterSetEdit::populateHighlighterList()
{
    highlighterListWidget->clear();
    for ( const Highlighter& highlighter : klogg::as_const( highlighterSet_.highlighters() ) ) {
        auto* new_item
            = new QListWidgetItem( QString::fromStdString( highlighter.pattern() ) );
        // new_item->setFlags( Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled );
        new_item->setForeground( QBrush( klogg::ui::qcolorFromRgba( highlighter.foreColor() ) ) );
        const auto itemBackColor = highlighter.backColor().a != 0
                                       ? klogg::ui::qcolorFromRgba( highlighter.backColor() )
                                       : QApplication::palette().color( QPalette::Base );
        new_item->setBackground( QBrush( itemBackColor ) );
        highlighterListWidget->addItem( new_item );
    }
}
