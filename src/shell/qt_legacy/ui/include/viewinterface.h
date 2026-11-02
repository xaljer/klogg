/*
 * Copyright (C) 2013, 2014 Nicolas Bonnefon and other contributors
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

#ifndef VIEWINTERFACE_H
#define VIEWINTERFACE_H

#include <memory>

#include "klogg/api_types.h"

class QuickFindPattern;

// ViewContextInterface represents the private information
// the concrete view will be able to save and restore.
// It can be marshalled to persistent storage.
class ViewContextInterface {
  public:
    virtual ~ViewContextInterface() = default;

    virtual QString toString() const = 0;
};

// ViewInterface represents a high-level view on a log file.
// This a pure virtual class (interface) which is subclassed
// for each type of view.
class ViewInterface {
public:
    // Bind the view to a file of the process-wide Core API session; all
    // content is read through it
    void setData( klogg::api::FileId file_id )
    {
        doSetData( file_id );
    }

    // Set the (shared) quickfind pattern object
    void setQuickFindPattern( std::shared_ptr<QuickFindPattern> qfp )
    {
        doSetQuickFindPattern( qfp );
    }

    // For save/restore of the context
    void setViewContext( const QString& view_context )
    {
        doSetViewContext( view_context );
    }
    // (returned object ownership is transferred to the caller)
    std::shared_ptr<const ViewContextInterface> context( void ) const
    {
        return doGetViewContext();
    }

    // Delivers the indexing-finished notification the view missed because it
    // bound after indexing had already completed (default: no-op)
    void replayIndexingFinished()
    {
        doReplayIndexingFinished();
    }

    // To allow polymorphic destruction
    virtual ~ViewInterface() = default;

protected:
    // Virtual functions (using NVI)
    virtual void doSetData( klogg::api::FileId file_id ) = 0;
    virtual void doSetQuickFindPattern( std::shared_ptr<QuickFindPattern> qfp ) = 0;
    virtual void doSetViewContext( const QString& view_context ) = 0;
    virtual std::shared_ptr<const ViewContextInterface> doGetViewContext( void ) const = 0;
    virtual void doReplayIndexingFinished() {}
};
#endif
