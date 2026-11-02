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

#include <QFileDialog>
#include <QTimer>

#include <algorithm>
#include <qcheckbox.h>
#include <qcolor.h>
#include <qlineedit.h>
#include <qnamespace.h>
#include <qpushbutton.h>
#include <utility>

#include "containers.h"
#include "dispatch_to.h"
#include "highlightercache.h"
#include "highlightersstore.h"
#include "settings.h"
#include "highlightersdialog.h"
#include "highlighterset.h"
#include "iconloader.h"
#include "log.h"
#include "log_qt.h"
#include "colorutils.h"

static constexpr QLatin1String DEFAULT_NAME = QLatin1String( "New Highlighter set", 19 );

// Construct the box, including a copy of the global highlighterSet
// to handle ok/cancel/apply
HighlightersDialog::HighlightersDialog( QWidget* parent )
    : QDialog( parent )
{
    setupUi( this );

    highlighterSetEdit_ = new HighlighterSetEdit( this );
    highlighterSetEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    highlighterLayout->addWidget( highlighterSetEdit_ );

    splitter->setStretchFactor( 0, 0 );
    splitter->setStretchFactor( 1, 1 );

    // The sets and the quick highlighters (color labels) both come from the
    // UI-owned collection; this is a working copy committed on OK/Apply
    highlighterSetCollection_ = klogg::ui::HighlighterCache::instance().collection();

    populateHighlighterList();

    // Start with all buttons disabled except 'add'
    removeHighlighterButton->setEnabled( false );
    upHighlighterButton->setEnabled( false );
    downHighlighterButton->setEnabled( false );

    connect( addHighlighterButton, &QToolButton::clicked, this,
             &HighlightersDialog::addHighlighterSet );
    connect( removeHighlighterButton, &QToolButton::clicked, this,
             &HighlightersDialog::removeHighlighterSet );

    connect( upHighlighterButton, &QToolButton::clicked, this,
             &HighlightersDialog::moveHighlighterSetUp );
    connect( downHighlighterButton, &QToolButton::clicked, this,
             &HighlightersDialog::moveHighlighterSetDown );

    connect( exportButton, &QPushButton::clicked, this, &HighlightersDialog::exportHighlighters );
    connect( importButton, &QPushButton::clicked, this, &HighlightersDialog::importHighlighters );

    // No highlighter selected by default
    selectedRow_ = -1;

    connect( highlighterListWidget, &QListWidget::itemSelectionChanged, this,
             &HighlightersDialog::updatePropertyFields );

    connect( highlighterSetEdit_, &HighlighterSetEdit::changed, this,
             &HighlightersDialog::updateHighlighterProperties );

    connect( buttonBox, &QDialogButtonBox::clicked, this, &HighlightersDialog::resolveDialog );

    if ( !highlighterSetCollection_.highlighters_.empty() ) {
        setCurrentRow( 0 );
    }

    quickHighlightLayout->removeWidget( colorLabelsPlaceholder );
    quickHighlightLayout->removeWidget( colorLabelsForeColor );
    quickHighlightLayout->removeWidget( colorLabelsBackColor );
    quickHighlightLayout->removeWidget( colorLabelsCycle );

    quickHighlightLayout->addWidget( colorLabelsPlaceholder, 0, 0 );
    quickHighlightLayout->addWidget( colorLabelsForeColor, 0, 1, Qt::AlignCenter );
    quickHighlightLayout->addWidget( colorLabelsBackColor, 0, 2, Qt::AlignCenter );
    quickHighlightLayout->addWidget( colorLabelsCycle, 0, 3, Qt::AlignCenter );

    const auto quickHighlighters = highlighterSetCollection_.quickHighlighters();
    for ( size_t i = 0; i < quickHighlighters.size(); ++i ) {
        const auto row = static_cast<int>( i + 1 );
        auto nameEdit = new QLineEdit( QString::fromStdString( quickHighlighters[ i ].name ) );
        auto foreButton = new QPushButton;
        auto backButton = new QPushButton;
        auto cycleCheckbox = new QCheckBox;

        HighlighterEdit::updateIcon( foreButton,
                                     klogg::ui::qcolorFromRgba( quickHighlighters[ i ].color.foreColor ) );
        HighlighterEdit::updateIcon( backButton,
                                     klogg::ui::qcolorFromRgba( quickHighlighters[ i ].color.backColor ) );
        cycleCheckbox->setChecked( quickHighlighters[ i ].useInCycle );

        connect( nameEdit, &QLineEdit::textChanged, nameEdit,
                 [ this, index = i ]( const QString& newName ) {
                     auto highlighters = highlighterSetCollection_.quickHighlighters();
                     if ( !newName.isEmpty() ) {
                         highlighters[ index ].name = newName.toStdString();
                         highlighterSetCollection_.setQuickHighlighters( highlighters );
                     }
                 } );

        connect( foreButton, &QPushButton::clicked, foreButton, [ foreButton, this, index = i ]() {
            auto highlighters = highlighterSetCollection_.quickHighlighters();
            QColor newColor;
            if ( HighlighterEdit::showColorPicker(
                     klogg::ui::qcolorFromRgba( highlighters[ index ].color.foreColor ),
                     newColor ) ) {
                highlighters[ index ].color.foreColor = klogg::ui::rgbaFromQColor( newColor );
                highlighterSetCollection_.setQuickHighlighters( highlighters );
                HighlighterEdit::updateIcon( foreButton, newColor );
            }
        } );

        connect( backButton, &QPushButton::clicked, backButton, [ backButton, this, index = i ]() {
            auto highlighters = highlighterSetCollection_.quickHighlighters();
            QColor newColor;
            if ( HighlighterEdit::showColorPicker(
                     klogg::ui::qcolorFromRgba( highlighters[ index ].color.backColor ),
                     newColor ) ) {
                highlighters[ index ].color.backColor = klogg::ui::rgbaFromQColor( newColor );
                highlighterSetCollection_.setQuickHighlighters( highlighters );
                HighlighterEdit::updateIcon( backButton, newColor );
            }
        } );

        connect( cycleCheckbox, &QCheckBox::clicked, cycleCheckbox,
                 [ this, index = i ]( bool isChecked ) {
                     auto highlighters = highlighterSetCollection_.quickHighlighters();
                     highlighters[ index ].useInCycle = isChecked;
                     highlighterSetCollection_.setQuickHighlighters( highlighters );
                 } );

        quickHighlightLayout->addWidget( nameEdit, row, 0 );
        quickHighlightLayout->addWidget( foreButton, row, 1 );
        quickHighlightLayout->addWidget( backButton, row, 2 );
        quickHighlightLayout->addWidget( cycleCheckbox, row, 3, Qt::AlignCenter );
    }

    dispatchToMainThread( [ this ] {
        IconLoader iconLoader( this );

        addHighlighterButton->setIcon( iconLoader.load( "icons8-plus-16" ) );
        removeHighlighterButton->setIcon( iconLoader.load( "icons8-minus-16" ) );
        upHighlighterButton->setIcon( iconLoader.load( "icons8-up-16" ) );
        downHighlighterButton->setIcon( iconLoader.load( "icons8-down-arrow-16" ) );
    } );
}

//
// Q_SLOTS:
//

void HighlightersDialog::exportHighlighters()
{
    QString file = QFileDialog::getSaveFileName( this, tr( "Export highlighters configuration" ),
                                                 "", "Highlighters (*.conf)" );

    if ( file.isEmpty() ) {
        return;
    }

    if ( !file.endsWith( ".conf" ) ) {
        file += ".conf";
    }

    platform::Settings settings( file.toStdString() );
    klogg::ui::saveHighlighters( settings, highlighterSetCollection_ );
    settings.save();
}

void HighlightersDialog::importHighlighters()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr( "Select one or more files to open" ), "", tr( "Highlighters (*.conf)" ) );

    for ( const auto& file : files ) {
        LOG_INFO << "Loading highlighters from " << file;
        platform::Settings settings( file.toStdString() );
        settings.load();
        HighlighterSetCollection collection;
        klogg::ui::loadHighlighters( settings, collection );
        for ( const auto& set : klogg::as_const( collection.highlighters_ ) ) {
            if ( highlighterSetCollection_.hasSet( set.id() )
                 || highlighterSetCollection_.hasSetByName( set.name() ) ) {

                LOG_INFO << "Skipping set " << QString::fromStdString( set.name() ) << " ("
                         << QString::fromStdString( set.id() ) << ")";

                continue;
            }

            LOG_INFO << "Adding set " << QString::fromStdString( set.name() ) << " ("
                     << QString::fromStdString( set.id() ) << ")";

            highlighterSetCollection_.highlighters_.push_back( set );
            highlighterListWidget->addItem( QString::fromStdString( set.name() ) );
        }
    }
}

void HighlightersDialog::addHighlighterSet()
{
    LOG_DEBUG << "addHighlighter()";

    highlighterSetCollection_.highlighters_.push_back(
        HighlighterSet::createNewSet( QString( DEFAULT_NAME ).toStdString() ) );

    // Add and select the newly created highlighter
    highlighterListWidget->addItem( DEFAULT_NAME );

    setCurrentRow( highlighterListWidget->count() - 1 );
}

void HighlightersDialog::removeHighlighterSet()
{
    int index = highlighterListWidget->currentRow();
    LOG_DEBUG << "removeHighlighter() index " << index;

    if ( index >= 0 ) {
        setCurrentRow( -1 );
        dispatchToMainThread( [ this, index ] {
            {
                const auto& set = highlighterSetCollection_.highlighters_.at(
                    static_cast<std::size_t>( index ) );
                highlighterSetCollection_.deactivateSet( set.id() );
            }

            highlighterSetCollection_.highlighters_.erase(
                highlighterSetCollection_.highlighters_.begin() + index );
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
    }
}

void HighlightersDialog::moveHighlighterSetUp()
{
    int index = highlighterListWidget->currentRow();
    LOG_DEBUG << "moveHighlighterUp() index " << index;

    if ( index > 0 ) {
        std::swap(
            highlighterSetCollection_.highlighters_[ static_cast<std::size_t>( index ) ],
            highlighterSetCollection_.highlighters_[ static_cast<std::size_t>( index - 1 ) ] );

        dispatchToMainThread( [ this, index ] {
            QListWidgetItem* item = highlighterListWidget->takeItem( index );
            highlighterListWidget->insertItem( index - 1, item );

            setCurrentRow( index - 1 );
        } );
    }
}

void HighlightersDialog::moveHighlighterSetDown()
{
    int index = highlighterListWidget->currentRow();
    LOG_DEBUG << "moveHighlighterDown() index " << index;

    if ( ( index >= 0 ) && ( index < ( highlighterListWidget->count() - 1 ) ) ) {
        std::swap(
            highlighterSetCollection_.highlighters_[ static_cast<std::size_t>( index ) ],
            highlighterSetCollection_.highlighters_[ static_cast<std::size_t>( index + 1 ) ] );

        dispatchToMainThread( [ this, index ] {
            QListWidgetItem* item = highlighterListWidget->takeItem( index );
            highlighterListWidget->insertItem( index + 1, item );

            setCurrentRow( index + 1 );
        } );
    }
}

void HighlightersDialog::resolveDialog( QAbstractButton* button )
{
    LOG_DEBUG << "resolveDialog()";

    QDialogButtonBox::ButtonRole role = buttonBox->buttonRole( button );
    if ( role == QDialogButtonBox::RejectRole ) {
        reject();
        return;
    }

    if ( role != QDialogButtonBox::AcceptRole && role != QDialogButtonBox::ApplyRole ) {
        LOG_ERROR << "unhandled role : " << role;
        return;
    }

    // The edited collection replaces the UI-owned one, then is merged and
    // pushed to the core session and persisted to the platform store
    auto& cache = klogg::ui::HighlighterCache::instance();
    cache.collection() = highlighterSetCollection_;
    cache.refresh();
    cache.persist();

    if ( role == QDialogButtonBox::AcceptRole ) {
        accept();
    }
    Q_EMIT optionsChanged();
}

void HighlightersDialog::setCurrentRow( int row )
{
    // ugly hack for mac
    dispatchToMainThread( [ this, row ]() { highlighterListWidget->setCurrentRow( row ); } );
}

void HighlightersDialog::updatePropertyFields()
{
    if ( highlighterListWidget->selectedItems().count() >= 1 )
        selectedRow_ = highlighterListWidget->row( highlighterListWidget->selectedItems().at( 0 ) );
    else
        selectedRow_ = -1;

    LOG_DEBUG << "updatePropertyFields(), row = " << selectedRow_;

    if ( selectedRow_ >= 0 ) {
        const HighlighterSet& currentSet = highlighterSetCollection_.highlighters_.at(
            static_cast<std::size_t>( selectedRow_ ) );
        highlighterSetEdit_->setHighlighters( currentSet );

        // Enable the buttons if needed
        removeHighlighterButton->setEnabled( true );
        upHighlighterButton->setEnabled( selectedRow_ > 0 );
        downHighlighterButton->setEnabled( selectedRow_ < ( highlighterListWidget->count() - 1 ) );
    }
    else {
        highlighterSetEdit_->reset();

        removeHighlighterButton->setEnabled( false );
        upHighlighterButton->setEnabled( false );
        downHighlighterButton->setEnabled( false );
    }
}

void HighlightersDialog::updateHighlighterProperties()
{
    LOG_DEBUG << "updateHighlighterProperties()";

    // If a row is selected
    if ( selectedRow_ >= 0 ) {
        HighlighterSet& currentSet
            = highlighterSetCollection_.highlighters_[ static_cast<std::size_t>( selectedRow_ ) ];
        currentSet = highlighterSetEdit_->highlighters();
        // Update the entry in the highlighterList widget
        highlighterListWidget->currentItem()->setText(
            QString::fromStdString( currentSet.name() ) );
    }
}

//
// Private functions
//

void HighlightersDialog::populateHighlighterList()
{
    highlighterListWidget->clear();
    for ( const HighlighterSet& highlighterSet :
          klogg::as_const( highlighterSetCollection_.highlighters_ ) ) {
        auto* new_item = new QListWidgetItem( QString::fromStdString( highlighterSet.name() ) );
        // new_item->setFlags( Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled );
        highlighterListWidget->addItem( new_item );
    }
}
