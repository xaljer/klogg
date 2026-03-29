/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
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
#include <QPalette>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleFactory>
#include <qcolor.h>
#include <map>

#include "configuration.h"
#include "log.h"
#include "styles.h"

namespace {
class KloggProxyStyle final : public QProxyStyle {
  public:
    explicit KloggProxyStyle( QStyle* style )
        : QProxyStyle( style )
    {
    }

    void setSuppressDisabledTextEffects( bool suppress )
    {
        suppressDisabledTextEffects_ = suppress;
    }

    int styleHint( StyleHint hint, const QStyleOption* option = nullptr,
                   const QWidget* widget = nullptr,
                   QStyleHintReturn* returnData = nullptr ) const override
    {
        if ( suppressDisabledTextEffects_
             && ( hint == QStyle::SH_EtchDisabledText || hint == QStyle::SH_DitherDisabledText ) ) {
            return 0;
        }

        return QProxyStyle::styleHint( hint, option, widget, returnData );
    }

  private:
    bool suppressDisabledTextEffects_ = false;
};
} // namespace

std::map<QString, QString> ThemeManager::defaultThemePalette( const QString& theme )
{
    static const std::map<QString, QString> darkPalette = {
        { "Window", "#282c34" },
        { "WindowText", "#ffd8e1f6" },
        { "LineNumberText", "#ff87a8c3" },
        { "Base", "#21252b" },
        { "AlternateBase", "#2c313c" },
        { "ToolTipBase", "#2c313c" },
        { "ToolTipText", "#abb2bf" },
        { "Text", "#abb2bf" },
        { "Button", "#ff3e4451" },
        { "ButtonText", "#abb2bf" },
        { "Link", "#ff618ea5" },
        { "Highlight", "#3e4451" },
        { "HighlightedText", "#fff0f5e5" },
        { "Mid", "#ff2c313c" },
        { "Dark", "#2c313c" },
        { "Shadow", "#1b1f26" },
        { "DisabledButtonText", "#5c6370" },
        { "DisabledWindowText", "#5c6370" },
        { "DisabledText", "#5c6370" },
        { "DisabledLight", "#2c313c" },
        { "ToggleCheckedText", "#ffd8e1f6" },
        { "ToggleCheckedBackground", "#ff2a475f" },
        { "MatchAccent", "#BF616A" },
        { "MainSearchBack", "#ff8a988e" },
        { "QuickFindBack", "#ffebcb8b" },
    };

    static const std::map<QString, QString> lightPalette = {
        { "Window", "#ffefefef" },
        { "WindowText", "#ff0f0f0f" },
        { "LineNumberText", "#ff383a3f" },
        { "Base", "#ffffffff" },
        { "AlternateBase", "#fff7f7f7" },
        { "ToolTipBase", "#ffffffdc" },
        { "ToolTipText", "#ff1f2124" },
        { "Text", "#ff0f0f0f" },
        { "Button", "#fffffef8" },
        { "ButtonText", "#ff0f0f0f" },
        { "Link", "#ff158bb8" },
        { "Highlight", "#ff308cc6" },
        { "HighlightedText", "#fffffef8" },
        { "Mid", "#ffbebebe" },
        { "Dark", "#ffbebebe" },
        { "Shadow", "#ff767676" },
        { "DisabledButtonText", "#ffbebebe" },
        { "DisabledWindowText", "#ffbebebe" },
        { "DisabledText", "#ffbebebe" },
        { "DisabledLight", "#ffffffff" },
        { "ToggleCheckedText", "#fffffef8" },
        { "ToggleCheckedBackground", "#ff2e5a6f" },
        { "MatchAccent", "#fff03752" },
        { "MainSearchBack", "#ffe2e7bf" },
        { "QuickFindBack", "#fffccb16" },
    };

    static const std::map<QString, QString> springPalette = {
        { "Window", "#ffc6dfc8" },
        { "WindowText", "#ff2e3440" },
        { "LineNumberText", "#ff1ba784" },
        { "Base", "#ffeef7f2" },
        { "AlternateBase", "#ffc6d7db" },
        { "ToolTipBase", "#ffc6e6e8" },
        { "ToolTipText", "#ff2e3440" },
        { "Text", "#ff2a475f" },
        { "Button", "#ffeef7f2" },
        { "ButtonText", "#ff2e3440" },
        { "Link", "#ff2a52be" },
        { "Highlight", "#ff2c9678" },
        { "HighlightedText", "#fffffef8" },
        { "Mid", "#ffa3a1a1" },
        { "Dark", "#ff424242" },
        { "Shadow", "#ff424242" },
        { "DisabledButtonText", "#ffa3a1a1" },
        { "DisabledWindowText", "#ffa3a1a1" },
        { "DisabledText", "#ffa3a1a1" },
        { "DisabledLight", "#ffbebebe" },
        { "ToggleCheckedText", "#ff1ba784" },
        { "ToggleCheckedBackground", "#ffc3e7b4" },
        { "MatchAccent", "#ffeb3c70" },
        { "MainSearchBack", "#ffb6d7a8" },
        { "QuickFindBack", "#ffffa545" },
    };

    static const std::map<QString, QString> nordPalette = {
        { "Window", "#ff2e3440" },
        { "WindowText", "#ffd8dee9" },
        { "LineNumberText", "#ff88c0d0" },
        { "Base", "#ff3b4252" },
        { "AlternateBase", "#ff2e3440" },
        { "ToolTipBase", "#ffb48ead" },
        { "ToolTipText", "#ff2e3440" },
        { "Text", "#ffd8dee9" },
        { "Button", "#ff3b4252" },
        { "ButtonText", "#abb2bf" },
        { "Link", "#ffd8dee9" },
        { "Highlight", "#ff5e81ac" },
        { "HighlightedText", "#ffeceff4" },
        { "Mid", "#3e4451" },
        { "Dark", "#2c313c" },
        { "Shadow", "#1b1f26" },
        { "DisabledButtonText", "#5c6370" },
        { "DisabledWindowText", "#5c6370" },
        { "DisabledText", "#5c6370" },
        { "DisabledLight", "#2c313c" },
        { "ToggleCheckedText", "#ffeceff4" },
        { "ToggleCheckedBackground", "#ff5e81ac" },
        { "MatchAccent", "#BF616A" },
        { "MainSearchBack", "#ff8fbcbb" },
        { "QuickFindBack", "#ffebcb8b" },
    };

    static const std::map<QString, QString> nordLightPalette = {
        { "Window", "#ffd8dee9" },
        { "WindowText", "#ff2e3440" },
        { "LineNumberText", "#ff4c566a" },
        { "Base", "#ffeceff4" },
        { "AlternateBase", "#ffd8dee9" },
        { "ToolTipBase", "#ff81a1c1" },
        { "ToolTipText", "#ff2e3440" },
        { "Text", "#ff2e3440" },
        { "Button", "#ffd8dee9" },
        { "ButtonText", "#ff2e3440" },
        { "Link", "#ffb48ead" },
        { "Highlight", "#ff5e81ac" },
        { "HighlightedText", "#ffeceff4" },
        { "Mid", "#ffb8b8b8" },
        { "Dark", "#ff9f9f9f" },
        { "Shadow", "#ff767676" },
        { "DisabledButtonText", "#ffbebebe" },
        { "DisabledWindowText", "#ffbebebe" },
        { "DisabledText", "#ffbebebe" },
        { "DisabledLight", "#ffffffff" },
        { "ToggleCheckedText", "#ffeceff4" },
        { "ToggleCheckedBackground", "#ff5e81ac" },
        { "MatchAccent", "#ffbf616a" },
        { "MainSearchBack", "#ff8fbcbb" },
        { "QuickFindBack", "#ffebcb8b" },
    };

    if ( theme == NordLightThemeKey ) {
        return nordLightPalette;
    }

    if ( theme == NordThemeKey ) {
        return nordPalette;
    }

    if ( theme == DarkThemeKey ) {
        return darkPalette;
    }

    if ( theme == LightThemeKey ) {
        return lightPalette;
    }

    if ( theme == SpringThemeKey ) {
        return springPalette;
    }

    auto* styleObject = QStyleFactory::create( FusionKey );
    if ( styleObject == nullptr ) {
        styleObject = QStyleFactory::create( FusionKey );
    }

    const auto palette = styleObject->standardPalette();
    return {
        { "Window", palette.color( QPalette::Window ).name( QColor::HexArgb ) },
        { "WindowText", palette.color( QPalette::WindowText ).name( QColor::HexArgb ) },
        { "LineNumberText", palette.color( QPalette::WindowText ).name( QColor::HexArgb ) },
        { "Base", palette.color( QPalette::Base ).name( QColor::HexArgb ) },
        { "AlternateBase", palette.color( QPalette::AlternateBase ).name( QColor::HexArgb ) },
        { "ToolTipBase", palette.color( QPalette::ToolTipBase ).name( QColor::HexArgb ) },
        { "ToolTipText", palette.color( QPalette::ToolTipText ).name( QColor::HexArgb ) },
        { "Text", palette.color( QPalette::Text ).name( QColor::HexArgb ) },
        { "Button", palette.color( QPalette::Button ).name( QColor::HexArgb ) },
        { "ButtonText", palette.color( QPalette::ButtonText ).name( QColor::HexArgb ) },
        { "Link", palette.color( QPalette::Link ).name( QColor::HexArgb ) },
        { "Highlight", palette.color( QPalette::Highlight ).name( QColor::HexArgb ) },
        { "HighlightedText", palette.color( QPalette::HighlightedText ).name( QColor::HexArgb ) },
        { "Mid", palette.color( QPalette::Mid ).name( QColor::HexArgb ) },
        { "Dark", palette.color( QPalette::Dark ).name( QColor::HexArgb ) },
        { "Shadow", palette.color( QPalette::Shadow ).name( QColor::HexArgb ) },
        { "DisabledButtonText", palette.color( QPalette::Disabled, QPalette::ButtonText ).name( QColor::HexArgb ) },
        { "DisabledWindowText", palette.color( QPalette::Disabled, QPalette::WindowText ).name( QColor::HexArgb ) },
        { "DisabledText", palette.color( QPalette::Disabled, QPalette::Text ).name( QColor::HexArgb ) },
        { "DisabledLight", palette.color( QPalette::Disabled, QPalette::Light ).name( QColor::HexArgb ) },
        { "ToggleCheckedText", palette.color( QPalette::HighlightedText ).name( QColor::HexArgb ) },
        { "ToggleCheckedBackground", palette.color( QPalette::Highlight ).name( QColor::HexArgb ) },
        { "MatchAccent", QString( "#FF0000" ) },
        { "MainSearchBack", QString( "#ff8fbcbb" ) },
        { "QuickFindBack", QString( "#ffebcb8b" ) },
    };
}

QStringList ThemeManager::availableThemes()
{
    auto themes = QStringList{
        QString( LightThemeKey ),
        QString( SpringThemeKey ),
        QString( DarkThemeKey ),
        QString( NordThemeKey ),
        QString( NordLightThemeKey ),
    };

    std::sort( themes.begin(), themes.end(), []( const auto& lhs, const auto& rhs ) {
        return lhs.compare( rhs, Qt::CaseInsensitive ) < 0;
    } );

    return themes;
}

void ThemeManager::applyFrameworkStyle()
{
    const auto effectiveStyle = QString( ThemeManager::FusionKey );

    auto* baseStyle = QStyleFactory::create( effectiveStyle );
    if ( baseStyle == nullptr ) {
        baseStyle = QStyleFactory::create( ThemeManager::FusionKey );
    }

    qApp->setStyle( new KloggProxyStyle( baseStyle ) );
    qApp->setStyleSheet( "" );
}

void ThemeManager::applyTheme( const QString& theme )
{
    auto effectiveTheme = theme;
    if ( effectiveTheme.isEmpty() ) {
        effectiveTheme = QString( NordLightThemeKey );
    }

    const auto palette = Configuration::get().themePalette( effectiveTheme );

    if ( auto* proxyStyle = dynamic_cast<KloggProxyStyle*>( qApp->style() );
         proxyStyle != nullptr ) {
        proxyStyle->setSuppressDisabledTextEffects( effectiveTheme == NordThemeKey );
    }

    QPalette themePalette = qApp->style()->standardPalette();
    auto setColorIfPresent = [ &palette ]( auto&& applyColor, const QString& key ) {
        if ( const auto it = palette.find( key ); it != palette.end() ) {
            applyColor( QColor( it->second ) );
        }
    };

    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::Window, c ); }, "Window" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::WindowText, c ); }, "WindowText" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::Base, c ); }, "Base" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::AlternateBase, c ); }, "AlternateBase" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::ToolTipBase, c ); }, "ToolTipBase" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::ToolTipText, c ); }, "ToolTipText" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::Text, c ); }, "Text" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::Button, c ); }, "Button" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::ButtonText, c ); }, "ButtonText" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::Link, c ); }, "Link" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::Highlight, c ); }, "Highlight" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::HighlightedText, c ); }, "HighlightedText" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::Mid, c ); }, "Mid" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::Dark, c ); }, "Dark" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::Shadow, c ); }, "Shadow" );

    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::Disabled, QPalette::ButtonText, c ); }, "DisabledButtonText" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::Disabled, QPalette::WindowText, c ); }, "DisabledWindowText" );
    setColorIfPresent( [ &themePalette ]( const QColor& c ) { themePalette.setColor( QPalette::Disabled, QPalette::Text, c ); }, "DisabledText" );
    if ( effectiveTheme == NordThemeKey ) {
        const auto disabledTextColor = themePalette.color( QPalette::Disabled, QPalette::Text );
        themePalette.setColor( QPalette::Disabled, QPalette::Light, disabledTextColor );
    }
    else {
        const auto styleDisabledLight = qApp->style()->standardPalette().color( QPalette::Disabled, QPalette::Light );
        themePalette.setColor( QPalette::Disabled, QPalette::Light, styleDisabledLight );
    }

    qApp->setPalette( themePalette );

}
