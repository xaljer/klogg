/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2007 QMUL.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include <QApplication>
#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QStyleOption>
#include <QWidget>

#include "iconloader.h"
#include "log.h"
#include "configuration.h"

#include <array>

constexpr std::array<int, 8> IconSizes{ 0, 16 };

IconLoader::IconLoader( QWidget* widget )
    : widget_{ widget }
{
}

QIcon IconLoader::load( QString name )
{
    QIcon icon;
    for ( int sz : IconSizes ) {
        QPixmap pmap( loadPixmap( name, sz ) );
        if ( !pmap.isNull() )
            icon.addPixmap( pmap );
    }
    return icon;
}

QIcon IconLoader::loadChecked( QString name, const QColor& checkedColor )
{
    auto icon = load( name );
    const auto effectiveCheckedColor = checkedColor.isValid() ? checkedColor : QColor( "#ffffff" );
    if ( !effectiveCheckedColor.isValid() ) {
        return icon;
    }

    const auto tint = [ &effectiveCheckedColor ]( const QPixmap& source ) {
        if ( source.isNull() ) {
            return QPixmap{};
        }

        auto img = source.toImage().convertToFormat( QImage::Format_ARGB32 );
        for ( int y = 0; y < img.height(); ++y ) {
            for ( int x = 0; x < img.width(); ++x ) {
                auto px = img.pixelColor( x, y );
                if ( px.alpha() == 0 ) {
                    continue;
                }

                px.setRed( effectiveCheckedColor.red() );
                px.setGreen( effectiveCheckedColor.green() );
                px.setBlue( effectiveCheckedColor.blue() );
                img.setPixelColor( x, y, px );
            }
        }

        return QPixmap::fromImage( img );
    };

    for ( int sz : IconSizes ) {
        const auto size = ( sz <= 0 ) ? QSize( 16, 16 ) : QSize( sz, sz );
        const auto off = icon.pixmap( size, QIcon::Normal, QIcon::Off );
        const auto on = tint( off );
        if ( on.isNull() ) {
            continue;
        }

        icon.addPixmap( on, QIcon::Normal, QIcon::On );
        icon.addPixmap( on, QIcon::Active, QIcon::On );
        icon.addPixmap( on, QIcon::Selected, QIcon::On );
        icon.addPixmap( on, QIcon::Disabled, QIcon::On );
    }

    if ( icon.availableSizes( QIcon::Normal, QIcon::On ).isEmpty() ) {
        const auto off = icon.pixmap( QSize( 16, 16 ), QIcon::Normal, QIcon::Off );
        const auto on = tint( off );
        if ( !on.isNull() ) {
            icon.addPixmap( on, QIcon::Normal, QIcon::On );
            icon.addPixmap( on, QIcon::Active, QIcon::On );
            icon.addPixmap( on, QIcon::Selected, QIcon::On );
            icon.addPixmap( on, QIcon::Disabled, QIcon::On );
        }
    }

    return icon;
}
bool IconLoader::shouldInvert() const
{
    QStyleOption style;
    style.initFrom( widget_ );
    auto bg = style.palette.window().color();
    bool darkBackground = ( bg.red() + bg.green() + bg.blue() <= 384 );
    return darkBackground;
}

bool IconLoader::shouldAutoInvert( QString /*name*/ ) const
{
    return true;
}

QPixmap IconLoader::loadPixmap( QString name, int size ) const
{
    bool invert = shouldInvert();
    QString nonScalableName;
    QPixmap pmap;
    // attempt to load a pixmap with the right size and inversion
    nonScalableName = makeNonScalableFilename( name, size, invert );
    pmap = QPixmap( nonScalableName );
    if ( pmap.isNull() && invert ) {
        // if that failed, and we were asking for an inverted pixmap,
        // that may mean we don't have an inverted version of it. We
        // could either auto-invert or use the uninverted version
        nonScalableName = makeNonScalableFilename( name, size, false );
        pmap = QPixmap( nonScalableName );

        if ( !pmap.isNull() && shouldAutoInvert( name ) ) {
            pmap = invertPixmap( pmap );
        }
    }

    const auto theme = Configuration::get().theme();
    if ( !pmap.isNull() && invert && theme == ThemeManager::NordThemeKey ) {
        pmap = invertPixmap( pmap );
    }

    return pmap;
}

QString IconLoader::makeNonScalableFilename( QString name, int size, bool invert ) const
{
    if ( invert ) {
        if ( size == 0 ) {
            return QString( ":/images/%1_inverse.png" ).arg( name );
        }
        else {
            return QString( ":/images/%1-%2_inverse.png" ).arg( name ).arg( size );
        }
    }
    else {
        if ( size == 0 ) {
            return QString( ":/images/%1.png" ).arg( name );
        }
        else {
            return QString( ":/images/%1-%2.png" ).arg( name ).arg( size );
        }
    }
}

QPixmap IconLoader::invertPixmap( QPixmap pmap ) const
{
    const auto theme = Configuration::get().theme();
    const auto themePalette = Configuration::get().themePalette( theme );
    const auto frostColor = [ &themePalette ] {
        if ( themePalette.count( "Link" ) > 0 ) {
            return QColor( themePalette.at( "Link" ) );
        }
        return QColor( "#88C0D0" );
    }();

    // No suitable inverted icon found for black background; try to
    // auto-invert the default one
    QImage img = pmap.toImage().convertToFormat( QImage::Format_ARGB32 );
    for ( int y = 0; y < img.height(); ++y ) {
        for ( int x = 0; x < img.width(); ++x ) {
            QRgb rgba = img.pixel( x, y );
            QColor colour = QColor( qRed( rgba ), qGreen( rgba ), qBlue( rgba ), qAlpha( rgba ) );
            int alpha = colour.alpha();
            if ( colour.saturation() < 5 && colour.alpha() > 10 ) {
                if ( theme == ThemeManager::NordThemeKey ) {
                    colour = frostColor;
                }
                else {
                    colour.setHsv( colour.hue(), colour.saturation(), 255 - colour.value() );
                }
                colour.setAlpha( alpha );
                img.setPixel( x, y, colour.rgba() );
            }
        }
    }
    pmap = QPixmap::fromImage( img );
    return pmap;
}
