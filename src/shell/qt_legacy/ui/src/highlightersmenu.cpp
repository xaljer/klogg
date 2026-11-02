#include "highlightersmenu.h"

#include "mainwindow.h"
#include <algorithm>
#include <qaction.h>
#include <vector>

#include "highlightercache.h"


HighlightersMenu::HighlightersMenu( const QString& title, QWidget* parent )
    : HoverMenu( title, parent )
    , highLighters_{ nullptr }
    , applyChange_{}
{
    connect( this, &QMenu::aboutToShow, this, &HighlightersMenu::updateActionsStatus );
}

void HighlightersMenu::applySelectionHighlighters( QAction* action ) const
{
    saveCurrentHighlighterFromAction( action );

    if ( !applyChange_ ) {
        return;
    }
    applyChange_();

    updateActionsStatus();
}

void HighlightersMenu::clearHighlightersMenu()
{
    clear();

    if ( highLighters_ ) {
        highLighters_->deleteLater();
    }
}

void HighlightersMenu::createHighlightersMenu()
{
    highLighters_ = new QActionGroup( this );
    highLighters_->setExclusive( false );
    connect( highLighters_, &QActionGroup::triggered, this,
             &HighlightersMenu::applySelectionHighlighters );
}

void HighlightersMenu::populateHighlightersMenu()
{
    const auto& collection = klogg::ui::HighlighterCache::instance().collection();
    const auto sets = collection.highlighterSets();

    auto noneAction = addAction( tr( "None" ) );
    noneAction->setActionGroup( highLighters_ );
    // The "None" entry maps to no set; the checks below use the set index
    noneAction->setData( -1 );
    noneAction->setEnabled( !collection.activeSetIds().empty() );

    addSeparator();

    for ( size_t i = 0; i < sets.size(); ++i ) {
        auto setAction = addAction( QString::fromStdString( sets[ i ].name() ) );
        setAction->setActionGroup( highLighters_ );
        setAction->setCheckable( true );
        setAction->setData( static_cast<int>( i ) );
    }

    updateActionsStatus();
}

void HighlightersMenu::saveCurrentHighlighterFromAction( const QAction* action ) const
{
    auto& cache = klogg::ui::HighlighterCache::instance();
    auto& collection = cache.collection();

    const auto index = action->data().toInt();
    if ( index < 0 ) {
        // The "None" action deactivates every set
        collection.deactivateAll();
    }
    else {
        const auto sets = collection.highlighterSets();
        if ( static_cast<size_t>( index ) < sets.size() ) {
            const auto& setId = sets[ static_cast<size_t>( index ) ].id();
            if ( action->isChecked() ) {
                collection.activateSet( setId );
            }
            else {
                collection.deactivateSet( setId );
            }
        }
    }

    cache.refresh();
    cache.persist();
}

void HighlightersMenu::updateActionsStatus() const
{
    const auto& collection = klogg::ui::HighlighterCache::instance().collection();
    const auto sets = collection.highlighterSets();
    const auto activeIds = collection.activeSetIds();

    const auto isActive = [ & ]( size_t index ) {
        return index < sets.size()
               && std::find( activeIds.begin(), activeIds.end(), sets[ index ].id() )
                      != activeIds.end();
    };

    const auto selectNone = activeIds.empty();

    for ( auto* action : highLighters_->actions() ) {
        const auto index = action->data().toInt();
        // update "None" action state
        if ( index < 0 ) {
            action->setEnabled( !selectNone );
            action->setChecked( selectNone );
            continue;
        }
        action->setChecked( isActive( static_cast<size_t>( index ) ) );
    }
}

void HighlightersMenu::addAction( QAction* action, bool seq )
{
    addAction( action );
    if ( seq ) {
        addSeparator();
    }
}
