/*
 * Copyright (C) 2009, 2010, 2012 Nicolas Bonnefon and other contributors
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

#ifndef FILTEREDVIEW_H
#define FILTEREDVIEW_H

#include <optional>
#include <vector>

#include <QTextCodec>

#include "abstractlogview.h"

#include "logfiltereddata.h"

#include "klogg/api_types.h"

#include <QKeyEvent>

// Class implementing the filtered (bottom) view widget.
class FilteredView : public AbstractLogView {
    Q_OBJECT
public:
    // The FileId identifies the file in the process-wide Core API session;
    // line fetching switches to the API once a search id is bound via
    // useNewFilteredData (B5b), otherwise the view renders the search-less
    // empty projection pushed by the owning CrawlerWidget
    FilteredView( const QuickFindPattern* const quickFindPattern,
                  std::optional<klogg::api::FileId> fileId = std::nullopt,
                  QWidget* parent = nullptr );

    // What is visible in the view.
    using Visibility = LogFilteredData::Visibility;
    void setVisibility( Visibility visi );

    // Bind the view to the API projection of a search (nullopt: back to the
    // search-less empty projection)
    void useNewFilteredData( std::optional<klogg::api::SearchId> searchId = std::nullopt );

    // Drops the cached session marks used by mark navigation; the owning
    // CrawlerWidget calls it whenever the file's marks change
    void invalidateMarksCache();

    // Search-less projection (marks unioned with the crawler's populated
    // extraction event lines, visibility-filtered, sorted ascending): read
    // when no search is bound, replacing the empty per-tab data set
    void setEmptyProjection( std::vector<LineNumber> lines );

protected:
    LogFilteredData::LineType lineType( LineNumber lineNumber ) const override;

    // Maps the frame through the projection, then one lineFlagsBatch covers
    // the non-contiguous source lines (B5c)
    void prefetchGutterFlags( LineNumber firstLine, LinesCount count ) override;

    // Number of the filtered line relative to the unfiltered source
    LineNumber displayLineNumber( LineNumber lineNumber ) const override;
    LineNumber lineIndex( LineNumber lineNumber ) const override;
    LineNumber maxDisplayLineNumber() const override;

    void doRegisterShortcuts() override;

    // LineDataSource through the Core API session when a search is bound
    // (B5b), same shape as the LogMainView overrides (B4)
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
    std::optional<klogg::api::FileId> fileId_;
    std::optional<klogg::api::SearchId> searchId_;

    // Mark navigation over the session-level marks: sorted source lines,
    // lazily fetched and dropped when the marks change
    void selectFilteredMark( bool backward );
    // False when the view has no API identity or the query fails (the
    // navigation is skipped then)
    bool selectFilteredMarkApi( bool backward );
    std::optional<std::vector<LineNumber>> marksCache_;

    // True once the owning CrawlerWidget pushed the search-less projection
    bool emptyProjectionActive_ = false;
    std::vector<LineNumber> emptyProjectionLines_;
    // Lazily computed on the first scroll-range query after a push
    mutable LineLength emptyProjectionMaxLength_ = 0_length;
    mutable bool emptyProjectionMaxLengthComputed_ = false;
    bool hasEmptyProjection() const
    {
        return emptyProjectionActive_ && !searchId_;
    }
    // Projection index -> source line through the active non-search path
    LineNumber emptyProjectionSourceLine( LineNumber index ) const;

    // Source line numbers of the most recent API getLines fetch, reused by
    // the same frame's gutter prefetch instead of a second projection fetch
    // (B5c); cleared whenever a fetch does not go through the API
    mutable LineNumber lastGetLinesStart_ = 0_lnum;
    mutable std::vector<LineNumber> lastGetLinesSourceLines_;
};

#endif
