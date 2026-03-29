/*
 * Copyright (C) 2009, 2010, 2011, 2013 Nicolas Bonnefon and other contributors
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

#include <QColorDialog>
#include <QInputDialog>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QtGui>
#include <vector>

#include "encodings.h"
#include "fontutils.h"
#include "highlighteredit.h"
#include "log.h"
#include "mainwindow.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "shortcuts.h"
#include "styles.h"

#include "optionsdialog.h"

static constexpr int PollIntervalMin = 10;
static constexpr int PollIntervalMax = 3600000;

namespace {
QColor previewTextColor( const QColor& background )
{
    const auto yiq = ( background.red() * 299 + background.green() * 587 + background.blue() * 114 )
                     / 1000;
    return yiq >= 160 ? QColor( "#111111" ) : QColor( "#f2f2f2" );
}

void updateColorPreviewButton( QPushButton* button, const QColor& color )
{
    if ( button == nullptr ) {
        return;
    }

    const auto textColor = previewTextColor( color );
    const auto borderColor = color.lightness() > 220 ? QColor( "#6b6b6b" ) : color.darker( 170 );

    button->setMinimumWidth( 150 );
    button->setFixedHeight( 22 );
    button->setText( color.name( QColor::HexRgb ).toUpper() );
    button->setStyleSheet( QString( "QPushButton {"
                                    "background-color: %1;"
                                    "color: %2;"
                                    "border: 1px solid %3;"
                                    "border-radius: 3px;"
                                    "padding: 2px 6px;"
                                    "text-align: left;"
                                    "}" )
                               .arg( color.name( QColor::HexRgb ), textColor.name(),
                                     borderColor.name() ) );
}
} // namespace

// Constructor
OptionsDialog::OptionsDialog( QWidget* parent )
    : QDialog( parent )
{
    setupUi( this );

    setupTabs();
    setupFontList();
    setupRegexp();
    setupEncodings();
    setupLanguageList();
    setupColorsTab();

    // Validators
    QValidator* pollingIntervalValidator = new QIntValidator( PollIntervalMin, PollIntervalMax );
    pollIntervalLineEdit->setValidator( pollingIntervalValidator );

    connect( buttonBox, &QDialogButtonBox::clicked, this, &OptionsDialog::onButtonBoxClicked );
    connect( fontFamilyBox, &QComboBox::currentTextChanged, this, &OptionsDialog::updateFontSize );
    connect( pollingCheckBox, &QCheckBox::toggled, [ this ]( auto ) { this->setupPolling(); } );
    connect( searchResultsCacheCheckBox, &QCheckBox::toggled,
             [ this ]( auto ) { this->setupSearchResultsCache(); } );
    connect( loggingCheckBox, &QCheckBox::toggled, [ this ]( auto ) { this->setupLogging(); } );

    connect( extractArchivesCheckBox, &QCheckBox::toggled,
             [ this ]( auto ) { this->setupArchives(); } );

    connect( mainSearchColorButton, &QPushButton::clicked, this, &OptionsDialog::changeMainColor );
    mainSearchColorButton->setToolTip( tr( "Managed by current theme in Colors & Themes." ) );
    connect( quickFindColorButton, &QPushButton::clicked, this, &OptionsDialog::changeQfColor );
    quickFindColorButton->setToolTip( tr( "Managed by current theme in Colors & Themes." ) );

    connect( restoreShortcutsDefaults, &QPushButton::clicked, this, [ this ]() {
        auto ret = QMessageBox::question(
            this, "Restore Default Shortcuts", "Do you want to restore default shortcuts?",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel );
        if ( ret == QMessageBox::Yes )
            buildShortcutsTable( true );
    } );

    updateDialogFromConfig();

    setupPolling();
    setupSearchResultsCache();
    setupLogging();
    setupArchives();
}

//
// Private functions
//

// Setups the tabs depending on the configuration
void OptionsDialog::setupTabs()
{
#ifndef Q_OS_WIN
    keepFileClosedCheckBox->setVisible( false );
#endif

#ifdef Q_OS_MAC
    minimizeToTrayCheckBox->setVisible( false );
#endif

#ifndef KLOGG_HAS_HS
    regexpEngineLabel->setVisible( false );
    regexpEngineComboBox->setVisible( false );
#endif

}

// Populates the 'family' ComboBox
void OptionsDialog::setupFontList()
{
    const auto families = FontUtils::availableFonts();
    for ( const QString& str : families ) {
        fontFamilyBox->addItem( str );
    }
}

// Populate the regexp ComboBoxes
void OptionsDialog::setupRegexp()
{
    QStringList regexpTypes;
    regexpTypes << tr( "Extended Regexp" ) << tr( "Fixed Strings" );

    mainSearchBox->addItems( regexpTypes );
    quickFindSearchBox->addItems( regexpTypes );

    QStringList regexpEngines;
    regexpEngines << tr( "Hyperscan" ) << tr( "Qt" );

    regexpEngineComboBox->addItems( regexpEngines );
}

void OptionsDialog::setupEncodings()
{
    const auto availableEncodings = EncodingMenu::supportedEncodings();
    encodingComboBox->addItem( "Auto", -1 );

    std::map<QString, int> allMibs;

    for ( const auto& group : availableEncodings ) {
        for ( const auto& mib : group.second ) {
            auto codec = QTextCodec::codecForMib( mib );
            if ( codec ) {
                allMibs.emplace( codec->name(), mib );
            }
        }
    }

    for ( const auto& codec : allMibs ) {
        encodingComboBox->addItem( codec.first, codec.second );
    }
}

void OptionsDialog::setupLanguageList()
{
    QResource resource( ":/i18n/Languages.xml" );
    QByteArray bytes( reinterpret_cast<const char*>( resource.data() ), (int)resource.size() );
    QXmlStreamReader xml( bytes );

    while ( !xml.atEnd() ) {
        QXmlStreamReader::TokenType token = xml.readNext();
        if ( xml.hasError() ) {
            LOG_ERROR << "load language error";
            return;
        }

        if ( xml.name() == QString( "language" ) && token == QXmlStreamReader::StartElement ) {
            QXmlStreamAttributes attributes = xml.attributes();
            languageComboBox->addItem( attributes.value( "name" ).toString(),
                                       attributes.value( "ietfCode" ).toString() );
        }
    }
}

void OptionsDialog::setupColorsTab()
{
    auto* colorsTab = new QWidget( this );
    auto* colorsTabLayout = new QVBoxLayout( colorsTab );

    auto* darkPaletteBox = new QGroupBox( tr( "Theme palette colors" ), colorsTab );
    auto* darkPaletteRootLayout = new QVBoxLayout( darkPaletteBox );
    auto* selectorLayout = new QHBoxLayout();
    auto* themeSelectorLabel = new QLabel( tr( "Theme:" ), darkPaletteBox );
    themeComboBox_ = new QComboBox( darkPaletteBox );
    resetThemeButton_ = new QPushButton( tr( "Reset to default" ), darkPaletteBox );
    cloneThemeButton_ = new QPushButton( tr( "Create from current" ), darkPaletteBox );
    themeSelectorLabel->setToolTip( tr( "Theme controls colors." ) );
    themeComboBox_->setToolTip( themeSelectorLabel->toolTip() );
    selectorLayout->addWidget( themeSelectorLabel );
    selectorLayout->addWidget( themeComboBox_, 1 );
    selectorLayout->addWidget( resetThemeButton_ );
    selectorLayout->addWidget( cloneThemeButton_ );
    selectorLayout->addStretch();
    darkPaletteRootLayout->addLayout( selectorLayout );

    struct PaletteField {
        QString section;
        QString key;
        QString title;
        QString usage;
    };
    struct PaletteRow {
        QString section;
        PaletteField left;
        PaletteField right;
    };

    const std::vector<PaletteRow> paletteRows = {
        { tr( "General UI" ),
          { tr( "General UI" ), "Window", tr( "Window background" ),
            tr( "Dialogs and panel backgrounds (for example, Preferences window)." ) },
          { tr( "General UI" ), "WindowText", tr( "Window text" ),
            tr( "Text shown on window/panel backgrounds." ) } },
        { tr( "General UI" ),
          { tr( "General UI" ), "Base", tr( "Content background" ),
            tr( "Editable/input content areas, such as text inputs and list content." ) },
          { tr( "General UI" ), "LineNumberText", tr( "Line number text" ),
            tr( "Text color of line numbers in log views." ) } },
        { tr( "General UI" ),
          { tr( "General UI" ), "AlternateBase", tr( "Secondary content background" ),
            tr( "Alternate rows/side areas and secondary content backgrounds." ) },
          {} },
        { tr( "General UI" ),
          { tr( "General UI" ), "MatchAccent", tr( "Matches overview accent" ),
            tr( "Color for matches markers in overview and match bullets." ) },
          {} },

        { tr( "Buttons" ),
          { tr( "Buttons" ), "Button", tr( "Button background" ),
            tr( "Button face/background color." ) },
          { tr( "Buttons" ), "ButtonText", tr( "Button foreground" ),
            tr( "Foreground color on buttons." ) } },
        { tr( "Buttons" ),
          { tr( "Buttons" ), "ToggleCheckedBackground", tr( "Toggle checked background" ),
            tr( "Background color for checked toggle buttons." ) },
          { tr( "Buttons" ), "ToggleCheckedText", tr( "Toggle checked foreground" ),
            tr( "Foreground color (text/icon) for checked toggle buttons." ) } },

        { tr( "Text and selection" ),
          { tr( "Text and selection" ), "Text", tr( "Primary text" ),
            tr( "Main readable text in content areas." ) },
          { tr( "Text and selection" ), "Link", tr( "Link" ), tr( "Hyperlink color." ) } },
        { tr( "Text and selection" ),
          { tr( "Text and selection" ), "Highlight", tr( "Selection background" ),
            tr( "Selected item/text background." ) },
          { tr( "Text and selection" ), "HighlightedText", tr( "Selection text" ),
            tr( "Text color on selected background." ) } },
        { tr( "Search highlights" ),
          { tr( "Search highlights" ), "MainSearchBack", tr( "Main search highlight" ),
            tr( "Background color for highlighted main search matches." ) },
          { tr( "Search highlights" ), "QuickFindBack", tr( "QuickFind highlight" ),
            tr( "Background color for QuickFind matches." ) } },

        { tr( "Tooltips" ),
          { tr( "Tooltips" ), "ToolTipBase", tr( "Tooltip background" ),
            tr( "Tooltip popup background." ) },
          { tr( "Tooltips" ), "ToolTipText", tr( "Tooltip text" ),
            tr( "Tooltip text color." ) } },

        { tr( "Borders and separators" ),
          { tr( "Borders and separators" ), "Mid", tr( "Separator" ),
            tr( "Medium separators and subtle borders." ) },
          { tr( "Borders and separators" ), "Dark", tr( "Strong separator" ),
            tr( "Darker separators and outlines." ) } },
        { tr( "Borders and separators" ),
          { tr( "Borders and separators" ), "Shadow", tr( "Shadow" ),
            tr( "Strongest border/shadow color." ) },
          {} },

        { tr( "Disabled state" ),
          { tr( "Disabled state" ), "DisabledText", tr( "Disabled text" ),
            tr( "Disabled text in content areas." ) },
          { tr( "Disabled state" ), "DisabledWindowText", tr( "Disabled window text" ),
            tr( "Disabled text shown on window/panel background." ) } },
        { tr( "Disabled state" ),
          { tr( "Disabled state" ), "DisabledButtonText", tr( "Disabled button text" ),
            tr( "Text color for disabled buttons." ) },
          {} },
    };

    auto currentSection = QString();
    auto sectionItemIndex = 0;
    QGridLayout* currentSectionLayout = nullptr;

    for ( std::size_t i = 0; i < paletteRows.size(); ++i ) {
        const auto& rowDef = paletteRows[ i ];

        if ( rowDef.section != currentSection ) {
            auto* sectionBox = new QGroupBox( rowDef.section, darkPaletteBox );
            auto* sectionLayout = new QGridLayout( sectionBox );
            sectionBox->setLayout( sectionLayout );
            darkPaletteRootLayout->addWidget( sectionBox );

            currentSection = rowDef.section;
            sectionItemIndex = 0;
            currentSectionLayout = sectionLayout;
        }

        const auto row = sectionItemIndex;

        auto addFieldToColumn = [ this, currentSectionLayout, row ]( const PaletteField& field,
                                                                      int col ) {
            if ( field.key.isEmpty() ) {
                return;
            }

            auto* keyLabel = new QLabel( field.title + ":", currentSectionLayout->parentWidget() );
            keyLabel->setToolTip( field.usage );
            auto* keyButton = new QPushButton( currentSectionLayout->parentWidget() );
            keyButton->setText( "" );
            keyButton->setToolTip( field.usage );
            keyButton->setProperty( "colorKey", field.key );
            connect( keyButton, &QPushButton::clicked, this, &OptionsDialog::changeDarkPaletteColor );

            darkPaletteButtons_[ field.key ] = keyButton;

            currentSectionLayout->addWidget( keyLabel, row, col );
            currentSectionLayout->addWidget( keyButton, row, col + 1 );
        };

        addFieldToColumn( rowDef.left, 0 );
        addFieldToColumn( rowDef.right, 2 );
        ++sectionItemIndex;
    }

    connect( themeComboBox_, qOverload<int>( &QComboBox::currentIndexChanged ), this,
             [ this ]( int index ) {
                 if ( index < 0 ) {
                     return;
                 }

                 selectedThemeName_ = themeComboBox_->itemData( index ).toString();
                 if ( selectedThemeName_.isEmpty() ) {
                     selectedThemeName_ = themeComboBox_->itemText( index );
                 }

                 if ( themePaletteColors_.count( selectedThemeName_ ) == 0 ) {
                     themePaletteColors_[ selectedThemeName_ ]
                         = Configuration::get().themePalette( selectedThemeName_ );
                 }

                 applySearchColorsFromTheme( selectedThemeName_ );
                 applyThemePaletteToButtons( selectedThemeName_ );
             } );
    connect( resetThemeButton_, &QPushButton::clicked, this, &OptionsDialog::resetCurrentThemePalette );
    connect( cloneThemeButton_, &QPushButton::clicked, this, &OptionsDialog::createThemeFromCurrent );

    auto* colorsScrollArea = new QScrollArea( colorsTab );
    colorsScrollArea->setWidgetResizable( true );
    colorsScrollArea->setFrameShape( QFrame::NoFrame );

    auto* colorsScrollContent = new QWidget( colorsScrollArea );
    auto* colorsScrollLayout = new QVBoxLayout( colorsScrollContent );
    colorsScrollLayout->setContentsMargins( 0, 0, 0, 0 );
    colorsScrollLayout->addWidget( darkPaletteBox );
    colorsScrollLayout->addStretch();

    colorsScrollArea->setWidget( colorsScrollContent );
    colorsTabLayout->addWidget( colorsScrollArea );

    tabWidget->addTab( colorsTab, tr( "Colors & Themes" ) );
}

void OptionsDialog::refreshThemeSelector()
{
    if ( themeComboBox_ == nullptr ) {
        return;
    }

    const auto previouslySelectedTheme = selectedThemeName_;

    QSignalBlocker blocker( themeComboBox_ );
    themeComboBox_->clear();

    const auto windowsDarkLegacyName = QString( "%1 Dark" ).arg( QString( "Windows" ) );
    const auto isStyleLikeThemeName = [ &windowsDarkLegacyName ]( const QString& themeName ) {
        static const QStringList legacyStyleNames = {
            QString( ThemeManager::FusionKey ),
            QString( "Windows" ),
            QString( "WindowsVista" ),
            QString( "macintosh" ),
            QString( "Gtk2" ),
            QString( "bb10" ),
            QString( "OneDark" ),
            QString( "Custom" ),
        };

        return legacyStyleNames.contains( themeName ) || themeName == windowsDarkLegacyName;
    };

    QStringList themes;
    for ( const auto& [ themeName, palette ] : themePaletteColors_ ) {
        Q_UNUSED( palette );
        if ( isStyleLikeThemeName( themeName ) ) {
            continue;
        }
        themes.append( themeName );
    }

    std::sort( themes.begin(), themes.end(), []( const auto& lhs, const auto& rhs ) {
        return lhs.compare( rhs, Qt::CaseInsensitive ) < 0;
    } );
    for ( const auto& theme : themes ) {
        themeComboBox_->addItem( displayThemeName( theme ), theme );
    }

    auto selected = previouslySelectedTheme;
    if ( selected.isEmpty() || themeComboBox_->findData( selected ) < 0 ) {
        selected = QString( ThemeManager::NordLightThemeKey );
    }
    if ( themeComboBox_->findData( selected ) < 0 && themeComboBox_->count() > 0 ) {
        selected = themeComboBox_->itemData( 0 ).toString();
        if ( selected.isEmpty() ) {
            selected = themeComboBox_->itemText( 0 );
        }
    }

    selectedThemeName_ = selected;
    if ( !selectedThemeName_.isEmpty() ) {
        const auto idx = themeComboBox_->findData( selectedThemeName_ );
        if ( idx >= 0 ) {
            themeComboBox_->setCurrentIndex( idx );
        }
    }
}

QString OptionsDialog::displayThemeName( const QString& themeName ) const
{
    if ( themeName == ThemeManager::LightThemeKey ) {
        return tr( "Light" );
    }
    if ( themeName == ThemeManager::SpringThemeKey ) {
        return tr( "Spring" );
    }
    if ( themeName == ThemeManager::DarkThemeKey ) {
        return tr( "Dark" );
    }
    if ( themeName == ThemeManager::NordThemeKey ) {
        return tr( "Nord" );
    }
    if ( themeName == ThemeManager::NordLightThemeKey ) {
        return tr( "Nord Light" );
    }

    return themeName;
}

QString OptionsDialog::currentSelectedThemeFromCombo() const
{
    if ( themeComboBox_ == nullptr ) {
        return QString();
    }

    auto theme = themeComboBox_->currentData().toString();
    if ( theme.isEmpty() ) {
        theme = themeComboBox_->currentText();
    }
    return theme;
}

void OptionsDialog::applyThemePaletteToButtons( const QString& themeName )
{
    if ( themeName.isEmpty() || themePaletteColors_.count( themeName ) == 0 ) {
        return;
    }

    const auto& palette = themePaletteColors_.at( themeName );
    for ( const auto& [ colorKey, button ] : darkPaletteButtons_ ) {
        if ( button != nullptr && palette.count( colorKey ) > 0 ) {
            updateColorPreviewButton( button, QColor( palette.at( colorKey ) ) );
        }
    }
}

void OptionsDialog::applySearchColorsFromTheme( const QString& themeName )
{
    if ( themeName.isEmpty() || themePaletteColors_.count( themeName ) == 0 ) {
        return;
    }

    const auto& palette = themePaletteColors_.at( themeName );
    if ( const auto it = palette.find( "MainSearchBack" ); it != palette.end() ) {
        const auto color = QColor( it->second );
        if ( color.isValid() ) {
            mainSearchColor_ = color;
            HighlighterEdit::updateIcon( mainSearchColorButton, mainSearchColor_ );
        }
    }

    if ( const auto it = palette.find( "QuickFindBack" ); it != palette.end() ) {
        const auto color = QColor( it->second );
        if ( color.isValid() ) {
            qfSearchColor_ = color;
            HighlighterEdit::updateIcon( quickFindColorButton, qfSearchColor_ );
        }
    }
}

void OptionsDialog::setupPolling()
{
    pollIntervalLineEdit->setEnabled( pollingCheckBox->isChecked() );
}

void OptionsDialog::setupSearchResultsCache()
{
    searchCacheSpinBox->setEnabled( searchResultsCacheCheckBox->isChecked() );
}

void OptionsDialog::setupLogging()
{
    verbositySpinBox->setEnabled( loggingCheckBox->isChecked() );
}

void OptionsDialog::setupArchives()
{
    extractArchivesAlwaysCheckBox->setEnabled( extractArchivesCheckBox->isChecked() );
}

// Convert a regexp type to its index in the list
int OptionsDialog::getRegexpTypeIndex( SearchRegexpType syntax ) const
{
    int index;

    switch ( syntax ) {
    case SearchRegexpType::FixedString:
        index = 1;
        break;
    default:
        index = 0;
        break;
    }

    return index;
}

// Convert the index of a regexp type to its type
SearchRegexpType OptionsDialog::getRegexpTypeFromIndex( int index ) const
{
    SearchRegexpType type;

    switch ( index ) {
    case 1:
        type = SearchRegexpType::FixedString;
        break;
    default:
        type = SearchRegexpType::ExtendedRegexp;
        break;
    }

    return type;
}

int OptionsDialog::getRegexpEngineIndex( RegexpEngine engine ) const
{
    int index;

    switch ( engine ) {
    case RegexpEngine::QRegularExpression:
        index = 1;
        break;
    default:
        index = 0;
        break;
    }

    return index;
}

RegexpEngine OptionsDialog::getRegexpEngineFromIndex( int index ) const
{
    RegexpEngine type;

    switch ( index ) {
    case 1:
        type = RegexpEngine::QRegularExpression;
        break;
    default:
        type = RegexpEngine::Hyperscan;
        break;
    }

    return type;
}

// Updates the dialog box using values in global Config()
void OptionsDialog::updateDialogFromConfig()
{
    const auto& config = Configuration::get();

    // Main font
    QFontInfo fontInfo = QFontInfo( config.mainFont() );

    int familyIndex = fontFamilyBox->findText( fontInfo.family() );
    if ( familyIndex != -1 )
        fontFamilyBox->setCurrentIndex( familyIndex );

    updateFontSize( fontInfo.family() );

    int sizeIndex = fontSizeBox->findText( QString::number( fontInfo.pointSize() ) );
    if ( sizeIndex != -1 )
        fontSizeBox->setCurrentIndex( sizeIndex );

    fontSmoothCheckBox->setChecked( config.forceFontAntialiasing() );
    boldFontCheckBox->setChecked( config.useBoldFont() );
    wrapTextCheckBox->setChecked( config.useTextWrap() );
    enableQtHiDpiCheckBox->setChecked( config.enableQtHighDpi() );
    scaleRoundingComboBox->setCurrentIndex( config.scaleFactorRounding() - 1 );

    // Language
    auto langIdx = languageComboBox->findData( { config.language() } );
    if ( langIdx == -1 ) {
        langIdx = 0;
    }
    languageComboBox->setCurrentIndex( langIdx );

    hideAnsiColorsCheckBox->setChecked( config.hideAnsiColorSequences() );

    // Regexp types
    mainSearchBox->setCurrentIndex( getRegexpTypeIndex( config.mainRegexpType() ) );
    mainSearchColor_ = config.mainSearchBackColor();
    HighlighterEdit::updateIcon( mainSearchColorButton, mainSearchColor_ );

    mainSearchForeColor_ = config.mainSearchForeColor();

    quickFindSearchBox->setCurrentIndex( getRegexpTypeIndex( config.quickfindRegexpType() ) );
    qfSearchColor_ = config.qfBackColor();
    HighlighterEdit::updateIcon( quickFindColorButton, qfSearchColor_ );

    themePaletteColors_ = config.themePalettes();
    defaultThemePaletteColors_.clear();
    for ( const auto& [ themeName, palette ] : themePaletteColors_ ) {
        if ( ThemeManager::availableThemes().contains( themeName ) ) {
            defaultThemePaletteColors_[ themeName ] = ThemeManager::defaultThemePalette( themeName );
        }
        else {
            defaultThemePaletteColors_[ themeName ] = palette;
        }
    }

    selectedThemeName_ = config.theme();
    refreshThemeSelector();
    if ( themeComboBox_ != nullptr ) {
        auto idx = themeComboBox_->findData( selectedThemeName_ );
        if ( idx < 0 ) {
            idx = themeComboBox_->findData( QString( ThemeManager::NordLightThemeKey ) );
        }
        if ( idx >= 0 ) {
            themeComboBox_->setCurrentIndex( idx );
        }
    }
    applySearchColorsFromTheme( selectedThemeName_ );
    applyThemePaletteToButtons( selectedThemeName_ );
    regexpEngineComboBox->setCurrentIndex( getRegexpEngineIndex( config.regexpEngine() ) );
    autoRunSearchOnAddCheckBox->setChecked( config.autoRunSearchOnPatternChange() );

    highlightMainSearchCheckBox->setChecked( config.mainSearchHighlight() );
    variateHighlightCheckBox->setChecked( config.variateMainSearchHighlight() );
    incrementalCheckBox->setChecked( config.isQuickfindIncremental() );
    caseSensitiveCheckBox->setChecked( !config.isSearchIgnoreCaseDefault() );
    logicalCombiningCheckBox->setChecked( config.isSearchLogicalCombiningDefault() );
    autoRefreshCheckBox->setChecked( config.isSearchAutoRefreshDefault() );

    // Polling
    nativeFileWatchCheckBox->setChecked( config.nativeFileWatchEnabled() );
    fastModificationDetectionCheckBox->setChecked( config.fastModificationDetection() );
    pollingCheckBox->setChecked( config.pollingEnabled() );
    pollIntervalLineEdit->setText( QString::number( config.pollIntervalMs() ) );
    allowFollowOnScrollCheckBox->setChecked( config.allowFollowOnScroll() );

    // Last session
    loadLastSessionCheckBox->setChecked( config.loadLastSession() );
    followFileOnLoadCheckBox->setChecked( config.followFileOnLoad() );
    minimizeToTrayCheckBox->setChecked( config.minimizeToTray() );
    multipleWindowsCheckBox->setChecked( config.allowMultipleWindows() );

    loggingCheckBox->setChecked( config.enableLogging() );
    verbositySpinBox->setValue( config.loggingLevel() );

    extractArchivesCheckBox->setChecked( config.extractArchives() );
    extractArchivesAlwaysCheckBox->setChecked( config.extractArchivesAlways() );

    // Perf
    parallelSearchCheckBox->setChecked( config.useParallelSearch() );
    searchResultsCacheCheckBox->setChecked( config.useSearchResultsCache() );
    searchCacheSpinBox->setValue( static_cast<int>( config.searchResultsCacheLines() ) );
    indexReadBufferSpinBox->setValue( config.indexReadBufferSizeMb() );
    searchReadBufferSpinBox->setValue( config.searchReadBufferSizeLines() );
    keepFileClosedCheckBox->setChecked( config.keepFileClosed() );
    compressedIndexCheckBox->setChecked( config.useCompressedIndex() );
    optimizeForNotLatinEncodingsCheckBox->setChecked( config.optimizeForNotLatinEncodings() );

    // version checking
    checkForNewVersionCheckBox->setChecked( config.versionCheckingEnabled() );

    // downloads
    verifySslCheckBox->setChecked( config.verifySslPeers() );

    const auto encodingIndex = encodingComboBox->findData( config.defaultEncodingMib() );
    encodingComboBox->setCurrentIndex( encodingIndex < 0 ? 0 : encodingIndex );

    buildShortcutsTable( false );

    const auto& savedSearches = SavedSearches::get();
    searchHistorySpinBox->setValue( savedSearches.historySize() );

    const auto& recentFiles = RecentFiles::get();
    filesHistoryMaxItemsSpinBox->setMinimum( 1 );
    filesHistoryMaxItemsSpinBox->setMaximum( MAX_RECENT_FILES );
    filesHistoryMaxItemsSpinBox->setValue( recentFiles.filesHistoryMaxItems() );
}

//
// Q_SLOTS:
//

void OptionsDialog::updateFontSize( const QString& fontFamily )
{
    QString oldFontSize = fontSizeBox->currentText();
    const auto sizes = FontUtils::availableFontSizes( fontFamily );

    fontSizeBox->clear();
    for ( int size : sizes ) {
        fontSizeBox->addItem( QString::number( size ) );
    }
    // Now restore the size we had before
    int i = fontSizeBox->findText( oldFontSize );
    if ( i != -1 )
        fontSizeBox->setCurrentIndex( i );
}

void OptionsDialog::changeMainColor()
{
    QColor newColor;
    if ( HighlighterEdit::showColorPicker( mainSearchColor_, newColor ) ) {
        mainSearchColor_ = newColor;
        HighlighterEdit::updateIcon( mainSearchColorButton, mainSearchColor_ );
        if ( !selectedThemeName_.isEmpty() && themePaletteColors_.count( selectedThemeName_ ) > 0 ) {
            themePaletteColors_[ selectedThemeName_ ]["MainSearchBack"]
                = newColor.name( QColor::HexArgb );
            applyThemePaletteToButtons( selectedThemeName_ );
        }
    }
}

void OptionsDialog::changeMainForeColor()
{
    QColor newColor;
    if ( HighlighterEdit::showColorPicker( mainSearchForeColor_, newColor ) ) {
        mainSearchForeColor_ = newColor;
    }
}

void OptionsDialog::changeQfColor()
{
    QColor newColor;
    if ( HighlighterEdit::showColorPicker( qfSearchColor_, newColor ) ) {
        qfSearchColor_ = newColor;
        HighlighterEdit::updateIcon( quickFindColorButton, qfSearchColor_ );
        if ( !selectedThemeName_.isEmpty() && themePaletteColors_.count( selectedThemeName_ ) > 0 ) {
            themePaletteColors_[ selectedThemeName_ ]["QuickFindBack"]
                = newColor.name( QColor::HexArgb );
            applyThemePaletteToButtons( selectedThemeName_ );
        }
    }
}

void OptionsDialog::changeDarkPaletteColor()
{
    auto* button = qobject_cast<QPushButton*>( sender() );
    if ( button == nullptr ) {
        return;
    }

    const auto colorKey = button->property( "colorKey" ).toString();
    if ( selectedThemeName_.isEmpty() || colorKey.isEmpty()
         || themePaletteColors_.count( selectedThemeName_ ) == 0
         || themePaletteColors_.at( selectedThemeName_ ).count( colorKey ) == 0 ) {
        return;
    }

    auto currentColor = QColor( themePaletteColors_.at( selectedThemeName_ ).at( colorKey ) );
    QColor newColor;
    if ( HighlighterEdit::showColorPicker( currentColor, newColor ) ) {
        themePaletteColors_[ selectedThemeName_ ][ colorKey ] = newColor.name( QColor::HexArgb );

        if ( colorKey == QString( "MainSearchBack" ) ) {
            mainSearchColor_ = newColor;
            HighlighterEdit::updateIcon( mainSearchColorButton, mainSearchColor_ );
        }
        else if ( colorKey == QString( "QuickFindBack" ) ) {
            qfSearchColor_ = newColor;
            HighlighterEdit::updateIcon( quickFindColorButton, qfSearchColor_ );
        }

        updateColorPreviewButton( button, newColor );
    }
}

void OptionsDialog::resetCurrentThemePalette()
{
    if ( selectedThemeName_.isEmpty() ) {
        return;
    }

    if ( defaultThemePaletteColors_.count( selectedThemeName_ ) == 0 ) {
        defaultThemePaletteColors_[ selectedThemeName_ ]
            = ThemeManager::defaultThemePalette( selectedThemeName_ );
    }

    themePaletteColors_[ selectedThemeName_ ] = defaultThemePaletteColors_.at( selectedThemeName_ );
    applySearchColorsFromTheme( selectedThemeName_ );
    applyThemePaletteToButtons( selectedThemeName_ );
}

void OptionsDialog::createThemeFromCurrent()
{
    if ( selectedThemeName_.isEmpty() || themePaletteColors_.count( selectedThemeName_ ) == 0 ) {
        return;
    }

    bool ok = false;
    const auto name = QInputDialog::getText( this, tr( "Create theme" ), tr( "Theme name:" ),
                                             QLineEdit::Normal, QString(), &ok )
                          .trimmed();
    if ( !ok || name.isEmpty() ) {
        return;
    }

    if ( themePaletteColors_.count( name ) > 0 ) {
        QMessageBox::warning( this, tr( "Create theme" ), tr( "Theme already exists." ) );
        return;
    }

    const auto windowsDarkLegacyName = QString( "%1 Dark" ).arg( QString( "Windows" ) );
    static const QStringList reservedThemeNames = {
        QString( ThemeManager::FusionKey ),
        QString( "Windows" ),
        QString( "WindowsVista" ),
        QString( "macintosh" ),
        QString( "Gtk2" ),
        QString( "bb10" ),
    };
    if ( reservedThemeNames.contains( name ) || name == windowsDarkLegacyName
         || name == QString( "OneDark" )
         || name == QString( "Custom" ) ) {
        QMessageBox::warning( this, tr( "Create theme" ),
                              tr( "Theme name conflicts with reserved style/theme names." ) );
        return;
    }

    themePaletteColors_[ name ] = themePaletteColors_.at( selectedThemeName_ );
    defaultThemePaletteColors_[ name ] = themePaletteColors_[ name ];
    selectedThemeName_ = name;

    refreshThemeSelector();
    if ( themeComboBox_ != nullptr ) {
        const auto idx = themeComboBox_->findData( selectedThemeName_ );
        if ( idx >= 0 ) {
            themeComboBox_->setCurrentIndex( idx );
        }
    }
    applyThemePaletteToButtons( selectedThemeName_ );
}

void OptionsDialog::checkShortcutsOnDuplicate() const
{
    static constexpr int PRIMARY_COL = 1;
    static constexpr int SECONDARY_COL = 2;

    if ( !shortcutsTable->rowCount() ) {
        return;
    }

    const auto DEFAULT_BACKGROUND = shortcutsTable->item( 0, PRIMARY_COL )->background();

    for ( auto shortcutRow = 0; shortcutRow < shortcutsTable->rowCount(); ++shortcutRow ) {
        shortcutsTable->item( shortcutRow, PRIMARY_COL )->setBackground( DEFAULT_BACKGROUND );
        shortcutsTable->item( shortcutRow, SECONDARY_COL )->setBackground( DEFAULT_BACKGROUND );
    }

    std::unordered_map<std::string, std::pair<int, int>> uniqueShortcuts;
    bool hasDuplicateShortcuts = false;
    for ( auto shortcutRow = 0; shortcutRow < shortcutsTable->rowCount(); ++shortcutRow ) {

        auto hasDuplicates = [ &uniqueShortcuts, shortcutRow, this ]( int ncol ) {
            auto keySequence = static_cast<KeySequencePresenter*>(
                                   shortcutsTable->cellWidget( shortcutRow, ncol ) )
                                   ->keySequence();

            if ( !keySequence.isEmpty() ) {
                if ( auto it = uniqueShortcuts.find( keySequence.toStdString() );
                     it != uniqueShortcuts.end() ) {

                    shortcutsTable->item( it->second.first, it->second.second )
                        ->setBackground( Qt::red );
                    shortcutsTable->item( shortcutRow, ncol )->setBackground( Qt::red );

                    return true;
                }

                uniqueShortcuts.try_emplace( keySequence.toStdString(),
                                             std::make_pair( shortcutRow, ncol ) );
            }

            return false;
        };

        if ( hasDuplicates( PRIMARY_COL ) || hasDuplicates( SECONDARY_COL ) ) {
            hasDuplicateShortcuts = true;
        }
    }

    buttonBox->button( QDialogButtonBox::Ok )->setEnabled( !hasDuplicateShortcuts );
    buttonBox->button( QDialogButtonBox::Apply )->setEnabled( !hasDuplicateShortcuts );
}

int OptionsDialog::updateTranslate()
{
    auto mw = dynamic_cast<MainWindow*>( parent() );
    return mw->installLanguage( languageComboBox->currentData().toString() );
}

void OptionsDialog::updateConfigFromDialog()
{
    const auto oldLanguage = Configuration::get().language();
    const auto oldEnableQtHighDpi = Configuration::get().enableQtHighDpi();
    const auto oldScaleFactorRounding = Configuration::get().scaleFactorRounding();

    bool restartAppMessage = false;
    auto& config = Configuration::get();

    QFont font = QFont( fontFamilyBox->currentText(), ( fontSizeBox->currentText() ).toInt() );
    config.setMainFont( font );
    config.setForceFontAntialiasing( fontSmoothCheckBox->isChecked() );
    config.setUseBoldFont( boldFontCheckBox->isChecked() );
    config.setUseTextWrap( wrapTextCheckBox->isChecked() );
    config.setEnableQtHighDpi( enableQtHiDpiCheckBox->isChecked() );
    config.setScaleFactorRounding( scaleRoundingComboBox->currentIndex() + 1 );

    config.setMainRegexpType( getRegexpTypeFromIndex( mainSearchBox->currentIndex() ) );
    config.setMainSearchBackColor( mainSearchColor_ );
    config.setMainSearchForeColor( mainSearchForeColor_ );
    config.setEnableMainSearchHighlight( highlightMainSearchCheckBox->isChecked() );
    config.setVariateMainSearchHighlight( variateHighlightCheckBox->isChecked() );
    config.setSearchIgnoreCaseDefault( !caseSensitiveCheckBox->isChecked() );
    config.setSearchAutoRefreshDefault( autoRefreshCheckBox->isChecked() );
    config.setSearchLogicalCombiningDefault( logicalCombiningCheckBox->isChecked() );
    config.setQuickfindRegexpType( getRegexpTypeFromIndex( quickFindSearchBox->currentIndex() ) );
    config.setQfBackColor( qfSearchColor_ );
    config.setQuickfindIncremental( incrementalCheckBox->isChecked() );
    config.setRegexpEnging( getRegexpEngineFromIndex( regexpEngineComboBox->currentIndex() ) );
    config.setAutoRunSearchOnPatternChange( autoRunSearchOnAddCheckBox->isChecked() );

    config.setNativeFileWatchEnabled( nativeFileWatchCheckBox->isChecked() );
    config.setPollingEnabled( pollingCheckBox->isChecked() );
    auto pollInterval = pollIntervalLineEdit->text().toInt();
    if ( pollInterval < PollIntervalMin )
        pollInterval = PollIntervalMin;
    else if ( pollInterval > PollIntervalMax )
        pollInterval = PollIntervalMax;

    config.setPollIntervalMs( pollInterval );
    config.setFastModificationDetection( fastModificationDetectionCheckBox->isChecked() );
    config.setAllowFollowOnScroll( allowFollowOnScrollCheckBox->isChecked() );

    config.setLoadLastSession( loadLastSessionCheckBox->isChecked() );
    config.setFollowFileOnLoad( followFileOnLoadCheckBox->isChecked() );
    config.setAllowMultipleWindows( multipleWindowsCheckBox->isChecked() );
    config.setMinimizeToTray( minimizeToTrayCheckBox->isChecked() );
    config.setEnableLogging( loggingCheckBox->isChecked() );
    config.setLoggingLevel( verbositySpinBox->value() );

    config.setExtractArchives( extractArchivesCheckBox->isChecked() );
    config.setExtractArchivesAlways( extractArchivesAlwaysCheckBox->isChecked() );

    config.setUseParallelSearch( parallelSearchCheckBox->isChecked() );
    config.setUseSearchResultsCache( searchResultsCacheCheckBox->isChecked() );
    config.setSearchResultsCacheLines( static_cast<unsigned>( searchCacheSpinBox->value() ) );
    config.setIndexReadBufferSizeMb( indexReadBufferSpinBox->value() );
    config.setSearchReadBufferSizeLines( searchReadBufferSpinBox->value() );
    config.setKeepFileClosed( keepFileClosedCheckBox->isChecked() );
    config.setUseCompressedIndex( compressedIndexCheckBox->isChecked() );
    config.setOptimizeForNotLatinEncodings( optimizeForNotLatinEncodingsCheckBox->isChecked() );

    // version checking
    config.setVersionCheckingEnabled( checkForNewVersionCheckBox->isChecked() );

    config.setVerifySslPeers( verifySslCheckBox->isChecked() );

    const auto themeChanged
        = ( themeComboBox_ != nullptr ) && ( config.theme() != currentSelectedThemeFromCombo() );

    const auto themePaletteChanged = config.themePalettes() != themePaletteColors_;

    config.setTheme( currentSelectedThemeFromCombo() );
    for ( const auto& [ themeName, palette ] : themePaletteColors_ ) {
        config.setThemePalette( themeName, palette );
    }

    if ( themeChanged || themePaletteChanged ) {
        ThemeManager::applyFrameworkStyle();
        ThemeManager::applyTheme( config.theme() );
    }

    config.setHideAnsiColorSequences( hideAnsiColorsCheckBox->isChecked() );

    config.setDefaultEncodingMib( encodingComboBox->currentData().toInt() );

    auto shortcuts = config.shortcuts();
    for ( auto shortcutRow = 0; shortcutRow < shortcutsTable->rowCount(); ++shortcutRow ) {
        QStringList actionKeys;

        auto primaryKeySequence
            = static_cast<KeySequencePresenter*>( shortcutsTable->cellWidget( shortcutRow, 1 ) )
                  ->keySequence();
        auto secondaryKeySequence
            = static_cast<KeySequencePresenter*>( shortcutsTable->cellWidget( shortcutRow, 2 ) )
                  ->keySequence();
        actionKeys << primaryKeySequence << secondaryKeySequence;

        auto action
            = shortcutsTable->item( shortcutRow, 0 )->data( Qt::UserRole ).toString().toStdString();
        shortcuts[ action ] = actionKeys;
    }
    config.setShortcuts( shortcuts );

    // update translate when accept or apply clicked
    restartAppMessage |= oldLanguage != languageComboBox->currentData().toString();
    updateTranslate();
    config.setLanguage( languageComboBox->currentData().toString() );
    retranslateUi( this );

    config.save();

    auto& savedSearches = SavedSearches::get();
    savedSearches.setHistorySize( searchHistorySpinBox->value() );
    savedSearches.save();

    auto& recentFiles = RecentFiles::get();
    recentFiles.setFilesHistoryMaxItems( filesHistoryMaxItemsSpinBox->value() );
    recentFiles.save();

    restartAppMessage |= oldEnableQtHighDpi != enableQtHiDpiCheckBox->isChecked();
    restartAppMessage |= oldScaleFactorRounding != ( scaleRoundingComboBox->currentIndex() + 1 );

    if ( restartAppMessage ) {
        QMessageBox::warning(
            this, "klogg",
            QApplication::translate( "OptionsDialog",
                                     "Klogg needs to be restarted to apply some changes. " ) );
    }

    Q_EMIT optionsChanged();
}

void OptionsDialog::onButtonBoxClicked( QAbstractButton* button )
{
    QDialogButtonBox::ButtonRole role = buttonBox->buttonRole( button );
    if ( ( role == QDialogButtonBox::AcceptRole ) || ( role == QDialogButtonBox::ApplyRole ) ) {
        updateConfigFromDialog();
    }

    if ( role == QDialogButtonBox::AcceptRole )
        accept();
    else if ( role == QDialogButtonBox::RejectRole )
        reject();
}

KeySequencePresenter::KeySequencePresenter( const QString& keySequence )
{
    keySequenceLabel_
        = new QLabel( QKeySequence( keySequence ).toString( QKeySequence::NativeText ) );

    auto editButton = new QPushButton();
    editButton->setText( "..." );
    editButton->setFixedWidth( 50 );

    auto layout = new QHBoxLayout();

    connect( editButton, &QPushButton::clicked, this, &KeySequencePresenter::showEditor );
    layout->addWidget( keySequenceLabel_ );
    layout->addStretch();
    layout->addWidget( editButton );
    layout->setContentsMargins( 4, 4, 4, 4 );

    this->setLayout( layout );
}

QString KeySequencePresenter::keySequence() const
{
    return keySequenceLabel_->text();
}

void KeySequencePresenter::showEditor()
{
    QDialog keyEditDialog;

    auto label = new QLabel( "Press new key combination" );
    auto editor = new QKeySequenceEdit( QKeySequence( keySequenceLabel_->text() ) );
    auto clearButton = new QToolButton();
    clearButton->setText( "Clear" );
    auto dialogButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );

    auto layout = new QVBoxLayout();
    layout->addWidget( label );
    auto editorLayout = new QHBoxLayout();
    editorLayout->addWidget( editor );
    editorLayout->addWidget( clearButton );
    layout->addLayout( editorLayout );
    layout->addWidget( dialogButtons );
    keyEditDialog.setLayout( layout );

    connect( clearButton, &QToolButton::clicked, editor, &QKeySequenceEdit::clear );
    connect( dialogButtons, &QDialogButtonBox::accepted, &keyEditDialog, &QDialog::accept );
    connect( dialogButtons, &QDialogButtonBox::rejected, &keyEditDialog, &QDialog::reject );

    if ( keyEditDialog.exec() == QDialog::Accepted ) {
        keySequenceLabel_->setText( editor->keySequence().toString() );
        Q_EMIT edited(); // NOTE: it's important to emit this signal only after changing
                         // \keySequenceLabel_'s text
    }
}

void OptionsDialog::buildShortcutsTable( bool useDefaultsOnly )
{
    shortcutsTable->setRowCount( 0 );

    const auto& config = Configuration::get();
    auto shortcutList = ShortcutAction::defaultShortcutList();
    if ( !useDefaultsOnly ) {
        for ( const auto& [ action, keys ] : config.shortcuts() ) {
            shortcutList[ action ].keySequence = keys;
        }
    }

    for ( const auto& [ action, shortCut ] : shortcutList ) {
        auto currentRow = shortcutsTable->rowCount();
        shortcutsTable->insertRow( currentRow );

        auto keyItem = new QTableWidgetItem( shortCut.name );
        keyItem->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable );
        keyItem->setData( Qt::UserRole, QString::fromStdString( action ) );
        shortcutsTable->setItem( currentRow, 0, keyItem );

        auto primaryKeySequence = new KeySequencePresenter(
            shortCut.keySequence.size() > 0 ? shortCut.keySequence[ 0 ] : "" );
        shortcutsTable->setItem( currentRow, 1, new QTableWidgetItem );
        shortcutsTable->setCellWidget( currentRow, 1, primaryKeySequence );
        connect( primaryKeySequence, &KeySequencePresenter::edited, this,
                 &OptionsDialog::checkShortcutsOnDuplicate );

        auto secondaryKeySequence = new KeySequencePresenter(
            shortCut.keySequence.size() > 1 ? shortCut.keySequence[ 1 ] : "" );
        shortcutsTable->setItem( currentRow, 2, new QTableWidgetItem );
        shortcutsTable->setCellWidget( currentRow, 2, secondaryKeySequence );
        connect( secondaryKeySequence, &KeySequencePresenter::edited, this,
                 &OptionsDialog::checkShortcutsOnDuplicate );
    }

    shortcutsTable->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
    shortcutsTable->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::Interactive );
    shortcutsTable->horizontalHeader()->setMinimumSectionSize( 150 );
    shortcutsTable->resizeColumnToContents( 0 );
    shortcutsTable->setHorizontalHeaderItem( 0, new QTableWidgetItem( tr( "Action" ) ) );
    shortcutsTable->setHorizontalHeaderItem( 1, new QTableWidgetItem( tr( "Primary shortcut" ) ) );
    shortcutsTable->setHorizontalHeaderItem( 2,
                                             new QTableWidgetItem( tr( "Secondary shortcut" ) ) );

    // in case if user set duplicate keys and after restores defaults
    // it is need to enable back standard buttons
    checkShortcutsOnDuplicate();

    shortcutsTable->sortItems( 0 );
}
