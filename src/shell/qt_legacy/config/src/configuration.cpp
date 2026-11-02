/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2015 Nicolas Bonnefon and other
 * contributors
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

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include <QFontInfo>
#include <QKeySequence>
#include <qcolor.h>
#include <qglobal.h>

#include "configuration.h"
#include "log.h"
#include "log_qt.h"
#include "shortcuts.h"
#include "styles.h"

namespace {
std::once_flag fontInitFlag;
static const Configuration DefaultConfiguration = {};

QColor colorFromString( const QString& value )
{
#if QT_VERSION <= QT_VERSION_CHECK( 6, 4, 0 )
    QColor color;
    color.setNamedColor( value );
    return color;
#else
    return QColor::fromString( value );
#endif
}

QString toQString( const std::string& value )
{
    return QString::fromStdString( value );
}

std::vector<std::string> toStdStrings( const QStringList& list )
{
    std::vector<std::string> result;
    result.reserve( static_cast<size_t>( list.size() ) );
    for ( const auto& entry : list ) {
        result.push_back( entry.toStdString() );
    }
    return result;
}

QStringList toQStringList( const std::vector<std::string>& list )
{
    QStringList result;
    for ( const auto& entry : list ) {
        result.push_back( QString::fromStdString( entry ) );
    }
    return result;
}
} // namespace

Configuration::Configuration()
{
    splitterSizes_ << 400 << 100;
}

// Accessor functions
QFont Configuration::mainFont() const
{
    std::call_once( fontInitFlag, [ this ]() {
        mainFont_.setStyleHint( QFont::Courier, QFont::PreferOutline );

        QFontInfo fi( mainFont_ );
        LOG_INFO << "DefaultConfiguration font is " << fi.family().toStdString() << ": "
                 << fi.pointSize();
    } );

    return mainFont_;
}

void Configuration::setMainFont( QFont newFont )
{
    LOG_DEBUG << "Configuration::setMainFont";

    mainFont_ = newFont;
}

void Configuration::retrieveFromStorage( platform::Settings& settings )
{
    LOG_DEBUG << "Configuration::retrieveFromStorage";

    // Engine / filewatch behavior keys; the UI maps these to EngineOptions.
    useParallelSearch_ = settings.getBool( "perf/useParallelSearch", useParallelSearch_ );
    searchThreadPoolSize_
        = settings.getInt( "perf/searchThreadPoolSize", searchThreadPoolSize_ );
    searchReadBufferSizeLines_
        = settings.getInt( "perf/searchReadBufferSizeLines", searchReadBufferSizeLines_ );
    useSearchResultsCache_
        = settings.getBool( "perf/useSearchResultsCache", useSearchResultsCache_ );
    searchResultsCacheLines_
        = settings.getUInt( "perf/searchResultsCacheLines", searchResultsCacheLines_ );
    indexReadBufferSizeMb_
        = settings.getInt( "perf/indexReadBufferSizeMb", indexReadBufferSizeMb_ );
    keepFileClosed_ = settings.getBool( "perf/keepFileClosed", keepFileClosed_ );
    useCompressedIndex_ = settings.getBool( "perf/useCompressedIndex", useCompressedIndex_ );

    fastModificationDetection_
        = settings.getBool( "filewatch/fastModificationDetection", fastModificationDetection_ );
    nativeFileWatchEnabled_
        = settings.getBool( "filewatch/useNative", nativeFileWatchEnabled_ );
    pollingEnabled_ = settings.getBool( "filewatch/usePolling", pollingEnabled_ );
    pollIntervalMs_ = settings.getInt( "filewatch/pollingIntervalMs", pollIntervalMs_ );

    regexpEngine_ = static_cast<RegexpEngine>(
        settings.getInt( "regexpType/engine", static_cast<int>( regexpEngine_ ) ) );
    ansiProcessing_ = static_cast<AnsiProcessing>(
        settings.getInt( "view/ansiProcessing", static_cast<int>( ansiProcessing_ ) ) );
    defaultEncodingMib_ = settings.getInt( "defaultView/encodingMib", defaultEncodingMib_ );

    // Fonts
    QString family = toQString(
        settings.getString( "mainFont/family", DefaultConfiguration.mainFont_.family().toStdString() ) );
    int size = settings.getInt( "mainFont/size", DefaultConfiguration.mainFont_.pointSize() );

    // If no config read, keep the DefaultConfiguration
    if ( !family.isNull() )
        mainFont_ = QFont( family, size );

    forceFontAntialiasing_
        = settings.getBool( "mainFont/antialiasing", DefaultConfiguration.forceFontAntialiasing_ );
    useBoldFont_ = settings.getBool( "mainFont/bold", DefaultConfiguration.useBoldFont_ );

    language_
        = toQString( settings.getString( "view/language", DefaultConfiguration.language_.toStdString() ) );

    enableQtHighDpi_
        = settings.getBool( "view/qtHiDpi", DefaultConfiguration.enableQtHighDpi_ );

    scaleFactorRounding_
        = settings.getInt( "view/scaleFactorRounding", DefaultConfiguration.scaleFactorRounding_ );

    // Regexp types
    mainRegexpType_ = static_cast<SearchRegexpType>( settings.getInt(
        "regexpType/main", static_cast<int>( DefaultConfiguration.mainRegexpType_ ) ) );
    quickfindRegexpType_ = static_cast<SearchRegexpType>( settings.getInt(
        "regexpType/quickfind", static_cast<int>( DefaultConfiguration.quickfindRegexpType_ ) ) );
    quickfindIncremental_
        = settings.getBool( "quickfind/incremental", DefaultConfiguration.quickfindIncremental_ );

    enableMainSearchHighlight_ = settings.getBool( "regexpType/mainHighlight",
                                                   DefaultConfiguration.enableMainSearchHighlight_ );

    enableMainSearchHighlightVariance_ = settings.getBool(
        "regexpType/mainHighlightVariate", DefaultConfiguration.enableMainSearchHighlightVariance_ );

    const auto mainSearchBackColorSetting = toQString( settings.getString(
        "regexpType/mainBackColor",
        DefaultConfiguration.mainSearchBackColor_.name( QColor::HexArgb ).toStdString() ) );
    mainSearchBackColor_ = colorFromString( mainSearchBackColorSetting );

    mainSearchForeColor_ = colorFromString( toQString( settings.getString(
        "regexpType/mainForeColor",
        DefaultConfiguration.mainSearchForeColor_.name( QColor::HexArgb ).toStdString() ) ) );

    const auto qfBackColorSetting = toQString( settings.getString(
        "regexpType/quickfindBackColor",
        DefaultConfiguration.qfBackColor_.name( QColor::HexArgb ).toStdString() ) );
    qfBackColor_ = colorFromString( qfBackColorSetting );

    // Migrate legacy default yellow quickfind color to new default Nord amber
    const auto normalizedQfSetting = qfBackColorSetting.toLower();
    if ( settings.contains( "regexpType/quickfindBackColor" )
         && ( normalizedQfSetting == QString( "#ffff00" )
              || normalizedQfSetting == QString( "#ffffff00" ) ) ) {
        qfBackColor_ = QColor( "#ebcb8b" );
    }

    sliceOverlayTextColor_ = colorFromString( toQString( settings.getString(
        "eventVisualization/sliceOverlayTextColor",
        DefaultConfiguration.sliceOverlayTextColor_.name( QColor::HexArgb ).toStdString() ) ) );

    sliceOverlayBackgroundColor_ = colorFromString( toQString( settings.getString(
        "eventVisualization/sliceOverlayBackgroundColor",
        DefaultConfiguration.sliceOverlayBackgroundColor_.name( QColor::HexArgb ).toStdString() ) ) );

    sliceOverlayBorderEnabled_ = settings.getBool(
        "eventVisualization/sliceOverlayBorderEnabled", DefaultConfiguration.sliceOverlayBorderEnabled_ );

    qfIgnoreCase_ = settings.getBool( "quickfind/ignore_case", DefaultConfiguration.qfIgnoreCase_ );

    autoRunSearchOnPatternChange_ = settings.getBool( "regexpType/autoRunSearch",
                                                      DefaultConfiguration.autoRunSearchOnPatternChange_ );

    // "Advanced" settings
    allowFollowOnScroll_
        = settings.getBool( "filewatch/allowFollowOnScroll", DefaultConfiguration.allowFollowOnScroll_ );

    loadLastSession_
        = settings.getBool( "session/loadLast", DefaultConfiguration.loadLastSession_ );
    allowMultipleWindows_
        = settings.getBool( "session/multipleWindows", DefaultConfiguration.allowMultipleWindows_ );
    followFileOnLoad_
        = settings.getBool( "session/followOnLoad", DefaultConfiguration.followFileOnLoad_ );

    enableLogging_
        = settings.getBool( "logging/enableLogging", DefaultConfiguration.enableLogging_ );
    loggingLevel_ = settings.getInt( "logging/verbosity", DefaultConfiguration.loggingLevel_ );

    enableVersionChecking_
        = settings.getBool( "versionchecker/enabled", DefaultConfiguration.enableVersionChecking_ );

    extractArchives_
        = settings.getBool( "archives/extract", DefaultConfiguration.extractArchives_ );
    extractArchivesAlways_
        = settings.getBool( "archives/extractAlways", DefaultConfiguration.extractArchivesAlways_ );

    tempDirectory_ = toQString(
        settings.getString( "files/temp_directory", DefaultConfiguration.tempDirectory_.toStdString() ) );

    // "Perf" settings
    optimizeForNotLatinEncodings_ = settings.getBool(
        "perf/optimizeForNotLatinEncodings", DefaultConfiguration.optimizeForNotLatinEncodings_ );

    verifySslPeers_ = settings.getBool( "net/verifySslPeers", DefaultConfiguration.verifySslPeers_ );

    // View settings
    overviewVisible_
        = settings.getBool( "view/overviewVisible", DefaultConfiguration.overviewVisible_ );
    lineNumbersVisibleInMain_ = settings.getBool( "view/lineNumbersVisibleInMain",
                                                  DefaultConfiguration.lineNumbersVisibleInMain_ );
    lineNumbersVisibleInFiltered_ = settings.getBool(
        "view/lineNumbersVisibleInFiltered", DefaultConfiguration.lineNumbersVisibleInFiltered_ );
    minimizeToTray_
        = settings.getBool( "view/minimizeToTray", DefaultConfiguration.minimizeToTray_ );

    useTextWrap_ = settings.getBool( "view/textWrap", DefaultConfiguration.useTextWrap() );
    chipMode_ = settings.getBool( "view/chipMode", DefaultConfiguration.chipMode() );

    settings.remove( "view/style" );

    theme_
        = toQString( settings.getString( "view/theme", DefaultConfiguration.theme_.toStdString() ) );

    // DefaultConfiguration crawler settings
    searchAutoRefresh_
        = settings.getBool( "defaultView/searchAutoRefresh", DefaultConfiguration.searchAutoRefresh_ );
    searchIgnoreCase_
        = settings.getBool( "defaultView/searchIgnoreCase", DefaultConfiguration.searchIgnoreCase_ );
    searchLogicalCombining_ = settings.getBool( "defaultView/searchLogicalCombining",
                                                DefaultConfiguration.searchLogicalCombining_ );

    if ( settings.contains( "defaultView/splitterSizes" ) ) {
        const auto sizes = settings.getIntList( "defaultView/splitterSizes" );
        // Keep the built-in default when the stored value is empty/unreadable,
        // so a malformed or partially migrated document cannot collapse the UI.
        if ( !sizes.empty() ) {
            splitterSizes_.clear();
            for ( const auto value : sizes ) {
                splitterSizes_ << value;
            }
        }
    }

    const auto shortcutsCount = settings.beginReadArray( "shortcuts" );
    for ( int shortcutIndex = 0; shortcutIndex < shortcutsCount; ++shortcutIndex ) {
        settings.setArrayIndex( shortcutIndex );
        auto action = settings.getString( "action", "" );
        if ( !action.empty() ) {
            if ( action == ShortcutAction::LogViewJumpToButtom ) {
                action = ShortcutAction::LogViewJumpToBottom;
            }
            const auto keys = settings.getStringList( "keys" );
            shortcuts_.insert_or_assign( action, toQStringList( keys ) );
        }
    }
    settings.endArray();

    for ( const auto& theme : ThemeManager::availableThemes() ) {
        if ( themePalettes_.count( theme ) == 0 ) {
            themePalettes_[ theme ] = ThemeManager::defaultThemePalette( theme );
        }
    }

    const auto windowsDarkLegacyName = QString( "%1 Dark" ).arg( QString( "Windows" ) );
    const auto isStyleLikeThemeName = [ &windowsDarkLegacyName ]( const QString& themeName ) {
        static const QStringList legacyStyleNames = {
            QString( ThemeManager::FusionKey ),
            QString( "WindowsVista" ),
            QString( "Windows" ),
            QString( "macintosh" ),
            QString( "Gtk2" ),
            QString( "bb10" ),
        };

        return legacyStyleNames.contains( themeName ) || themeName == windowsDarkLegacyName;
    };

    for ( const auto& groupName : settings.childGroups() ) {
        const auto qGroupName = QString::fromStdString( groupName );
        if ( !qGroupName.startsWith( "theme_" ) ) {
            continue;
        }

        const auto encodedThemeName = qGroupName.mid( 6 );
        const auto themeName = QString::fromUtf8(
            QByteArray::fromPercentEncoding( encodedThemeName.toUtf8() ) );
        if ( themeName.isEmpty() ) {
            continue;
        }

        if ( themeName == QString( "OneDark" ) || themeName == QString( "Custom" ) ) {
            continue;
        }

        if ( isStyleLikeThemeName( themeName ) ) {
            continue;
        }

        if ( themePalettes_.count( themeName ) == 0 ) {
            themePalettes_[ themeName ] = ThemeManager::defaultThemePalette( themeName );
        }
    }

    for ( auto& [ themeName, palette ] : themePalettes_ ) {
        const auto groupName = QString( "theme_%1" ).arg( themeName );
        settings.beginGroup( groupName.toStdString() );
        for ( auto& color : palette ) {
            color.second = toQString( settings.getString( color.first.toStdString(),
                                                          color.second.toStdString() ) );
        }
        settings.endGroup();

        if ( palette.count( "MainSearchBack" ) == 0 ) {
            palette[ "MainSearchBack" ] = mainSearchBackColor_.name( QColor::HexArgb );
        }
        if ( palette.count( "QuickFindBack" ) == 0 ) {
            palette[ "QuickFindBack" ] = qfBackColor_.name( QColor::HexArgb );
        }
    }

    if ( theme_.isEmpty() || themePalettes_.count( theme_ ) == 0 ) {
        theme_ = QString( ThemeManager::NordLightThemeKey );
    }

    settings.remove( "dark" );

    recorderCommands_.clear();
    const auto recorderCount = settings.beginReadArray( "recorderCommands" );
    for ( auto i = 0; i < recorderCount; ++i ) {
        settings.setArrayIndex( static_cast<int>( i ) );
        RecorderCommand cmd;
        cmd.name = toQString( settings.getString( "name" ) );
        cmd.command = toQString( settings.getString( "command" ) );
        recorderCommands_.append( cmd );
    }
    settings.endArray();

    recorderFileBindings_.clear();
    const auto bindingsCount = settings.beginReadArray( "recorderFileBindings" );
    for ( auto i = 0; i < bindingsCount; ++i ) {
        settings.setArrayIndex( static_cast<int>( i ) );
        const auto filePath = settings.getString( "file" );
        const auto commandName = settings.getString( "command" );
        if ( !filePath.empty() ) {
            recorderFileBindings_[ toQString( filePath ) ] = toQString( commandName );
        }
    }
    settings.endArray();
}

void Configuration::saveToStorage( platform::Settings& settings ) const
{
    LOG_DEBUG << "Configuration::saveToStorage";

    // Engine / filewatch behavior keys.
    settings.setBool( "perf/useParallelSearch", useParallelSearch_ );
    settings.setInt( "perf/searchThreadPoolSize", searchThreadPoolSize_ );
    settings.setInt( "perf/searchReadBufferSizeLines", searchReadBufferSizeLines_ );
    settings.setBool( "perf/useSearchResultsCache", useSearchResultsCache_ );
    settings.setUInt( "perf/searchResultsCacheLines", searchResultsCacheLines_ );
    settings.setInt( "perf/indexReadBufferSizeMb", indexReadBufferSizeMb_ );
    settings.setBool( "perf/keepFileClosed", keepFileClosed_ );
    settings.setBool( "perf/useCompressedIndex", useCompressedIndex_ );

    settings.setBool( "filewatch/fastModificationDetection", fastModificationDetection_ );
    settings.setBool( "filewatch/useNative", nativeFileWatchEnabled_ );
    settings.setBool( "filewatch/usePolling", pollingEnabled_ );
    settings.setInt( "filewatch/pollingIntervalMs", pollIntervalMs_ );

    settings.setInt( "regexpType/engine", static_cast<int>( regexpEngine_ ) );
    settings.setInt( "view/ansiProcessing", static_cast<int>( ansiProcessing_ ) );
    settings.setInt( "defaultView/encodingMib", defaultEncodingMib_ );

    QFontInfo fi( mainFont_ );

    settings.setString( "mainFont/family", fi.family().toStdString() );
    settings.setInt( "mainFont/size", fi.pointSize() );
    settings.setBool( "mainFont/antialiasing", forceFontAntialiasing_ );
    settings.setBool( "mainFont/bold", useBoldFont_ );

    settings.setInt( "regexpType/main", static_cast<int>( mainRegexpType_ ) );
    settings.remove( "regexpType/mainBackColor" );
    settings.setString( "regexpType/mainForeColor",
                        mainSearchForeColor_.name( QColor::HexArgb ).toStdString() );
    settings.setBool( "regexpType/mainHighlight", enableMainSearchHighlight_ );
    settings.setBool( "regexpType/mainHighlightVariate", enableMainSearchHighlightVariance_ );
    settings.setBool( "regexpType/autoRunSearch", autoRunSearchOnPatternChange_ );

    settings.setInt( "regexpType/quickfind", static_cast<int>( quickfindRegexpType_ ) );
    settings.remove( "regexpType/quickfindBackColor" );
    settings.setString( "eventVisualization/sliceOverlayTextColor",
                        sliceOverlayTextColor_.name( QColor::HexArgb ).toStdString() );
    settings.setString( "eventVisualization/sliceOverlayBackgroundColor",
                        sliceOverlayBackgroundColor_.name( QColor::HexArgb ).toStdString() );
    settings.setBool( "eventVisualization/sliceOverlayBorderEnabled", sliceOverlayBorderEnabled_ );

    settings.setBool( "quickfind/incremental", quickfindIncremental_ );
    settings.setBool( "quickfind/ignore_case", qfIgnoreCase_ );

    settings.setBool( "filewatch/allowFollowOnScroll", allowFollowOnScroll_ );

    settings.setBool( "session/loadLast", loadLastSession_ );
    settings.setBool( "session/multipleWindows", allowMultipleWindows_ );
    settings.setBool( "session/followOnLoad", followFileOnLoad_ );

    settings.setBool( "logging/enableLogging", enableLogging_ );
    settings.setInt( "logging/verbosity", loggingLevel_ );

    settings.setBool( "versionchecker/enabled", enableVersionChecking_ );

    settings.setBool( "archives/extract", extractArchives_ );
    settings.setBool( "archives/extractAlways", extractArchivesAlways_ );

    settings.setString( "files/temp_directory", tempDirectory_.toStdString() );

    settings.setBool( "perf/optimizeForNotLatinEncodings", optimizeForNotLatinEncodings_ );

    settings.setBool( "net/verifySslPeers", verifySslPeers_ );

    settings.setBool( "view/overviewVisible", overviewVisible_ );
    settings.setBool( "view/lineNumbersVisibleInMain", lineNumbersVisibleInMain_ );
    settings.setBool( "view/lineNumbersVisibleInFiltered", lineNumbersVisibleInFiltered_ );
    settings.setBool( "view/minimizeToTray", minimizeToTray_ );
    settings.remove( "view/style" );
    settings.setString( "view/theme", theme_.toStdString() );
    settings.setString( "view/language", language_.toStdString() );
    settings.setBool( "view/textWrap", useTextWrap_ );
    settings.setBool( "view/chipMode", chipMode_ );

    settings.setBool( "view/qtHiDpi", enableQtHighDpi_ );
    settings.setInt( "view/scaleFactorRounding", scaleFactorRounding_ );

    settings.setBool( "defaultView/searchAutoRefresh", searchAutoRefresh_ );
    settings.setBool( "defaultView/searchIgnoreCase", searchIgnoreCase_ );
    settings.setBool( "defaultView/searchLogicalCombining", searchLogicalCombining_ );

    std::vector<int> splitterSizes;
    splitterSizes.reserve( static_cast<size_t>( splitterSizes_.size() ) );
    for ( const auto size : splitterSizes_ ) {
        splitterSizes.push_back( size );
    }
    settings.setIntList( "defaultView/splitterSizes", splitterSizes );

    settings.beginWriteArray( "shortcuts" );
    auto shortcutIndex = 0;
    for ( const auto& mapping : shortcuts_ ) {
        settings.setArrayIndex( shortcutIndex );
        settings.setString( "action", mapping.first );
        settings.setStringList( "keys", toStdStrings( mapping.second ) );
        shortcutIndex++;
    }
    settings.endArray();

    for ( const auto& [ themeName, palette ] : themePalettes_ ) {
        const auto groupName = QString( "theme_%1" ).arg( themeName );
        settings.beginGroup( groupName.toStdString() );
        for ( const auto& color : palette ) {
            settings.setString( color.first.toStdString(), color.second.toStdString() );
        }
        settings.endGroup();
    }

    settings.remove( "dark" );

    settings.beginWriteArray( "recorderCommands" );
    for ( auto i = 0; i < recorderCommands_.size(); ++i ) {
        settings.setArrayIndex( i );
        settings.setString( "name", recorderCommands_.at( i ).name.toStdString() );
        settings.setString( "command", recorderCommands_.at( i ).command.toStdString() );
    }
    settings.endArray();

    settings.beginWriteArray( "recorderFileBindings" );
    auto bindingIndex = 0;
    for ( auto it = recorderFileBindings_.begin(); it != recorderFileBindings_.end();
          ++it, ++bindingIndex ) {
        settings.setArrayIndex( bindingIndex );
        settings.setString( "file", it.key().toStdString() );
        settings.setString( "command", it.value().toStdString() );
    }
    settings.endArray();
}
