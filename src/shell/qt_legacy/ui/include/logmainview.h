/*
 * Copyright (C) 2009, 2010, 2011, 2013 Nicolas Bonnefon
 * and other contributors
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
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#ifndef LOGMAINVIEW_H
#define LOGMAINVIEW_H

#include <atomic>
#include <optional>

#include <QTextCodec>

#include "abstractlogview.h"
#include "klogg/api_types.h"

// Class implementing the main (top) view widget.
class LogMainView : public AbstractLogView {
    Q_OBJECT
public:
    // The FileId identifies the file in the process-wide Core API session;
    // when set, line fetching goes through the API (B4), otherwise the
    // base-class defaults (empty lines) apply.
    LogMainView( const QuickFindPattern* const quickFindPattern, Overview* overview,
                 OverviewWidget* overview_widget,
                 std::optional<klogg::api::FileId> fileId = std::nullopt,
                 QWidget* parent = nullptr );

    // Binds the gutter flags and overview to that search's matches (B5b);
    // nullopt leaves only the mark/event bits
    void useNewFiltering( std::optional<klogg::api::SearchId> searchId = std::nullopt );

    // Refreshes the cached FileInfo before the base repaint logic uses it
    void updateData() override;

protected:
    // Implements the virtual function
    AbstractLogData::LineType lineType( LineNumber lineNumber ) const override;

    // One range lineFlags query covers the contiguous frame (B5c)
    void prefetchGutterFlags( LineNumber firstLine, LinesCount count ) override;

    // Composes the visible frame through the Core API session (new paint
    // path); nullopt (no FileId, failed read) keeps the per-line path
    std::optional<klogg::api::ViewportResult> renderFrame( LineNumber firstLine,
                                                           LinesCount count ) override;

    // Scroll position mapping through the Core API session (R4); nullopt
    // (no FileId, failed read) keeps the base computing from the local line
    // count
    std::optional<double> sessionScrollMultiplier() const override;
    std::optional<int64_t> sessionScrollPosToLine( int64_t scrollPos ) const override;
    std::optional<int64_t> sessionLineToScrollPos( LineNumber line ) const override;

    void doRegisterShortcuts() override;

    // LineDataSource through the Core API session (B4). Untabify stays on the
    // consumer side (decision 3): raw lines come from the API and are
    // expanded locally, same as the engine's expanded-line getters did.
    std::string getLineString( LineNumber line ) const override;
    std::string getExpandedLineString( LineNumber line ) const override;
    klogg::vector<std::string> getLines( LineNumber firstLine, LinesCount number ) const override;
    LineNumber getLineNumber( LineNumber index ) const override;
    LinesCount getNbLine() const override;
    LineLength getMaxLength() const override;
    LineLength getLineLength( LineNumber line ) const override;
    // Session-tracked display codec (FileInfo.encoding)
    const QTextCodec* displayEncoding() const override;

private:
    // Re-reads total_lines/max_line_length from the API session; keeps the
    // cached values when the API read is not possible
    void refreshFileInfoCache();

    // Mark navigation through the session-level marks (the source of truth)
    std::optional<LineNumber> markAfter( LineNumber line ) const;
    std::optional<LineNumber> markBefore( LineNumber line ) const;

    std::optional<klogg::api::FileId> fileId_;
    // Search bound to the gutter flags (B5b)
    std::optional<klogg::api::SearchId> searchId_;
    // Written on the GUI thread in refreshFileInfoCache, also read from the
    // QuickFind worker thread through getNbLine
    std::atomic<LinesCount> cachedNbLines_{ 0_lcount };
    LineLength cachedMaxLineLength_ = 0_length;
};

#endif
