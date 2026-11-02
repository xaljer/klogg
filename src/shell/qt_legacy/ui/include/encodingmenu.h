//
// Created by omni on 16.12.2019.
//

#ifndef KLOGG_ENCODINGMENU_H
#define KLOGG_ENCODINGMENU_H

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif // Q_OS_WIN

#include <QActionGroup>
#include <QApplication>
#include <QMenu>
#include <QString>
#include <QTextCodec>

#include <configuration.h>
#include <encodings.h>
#include <mainwindowtext.h>

class EncodingMenu {
public:
    static QMenu* generate( QActionGroup* actionGroup )
    {
        const auto supportedEncodings = Encodings::supportedEncodings();
        const auto defaultEncodingMib = Configuration::get().defaultEncodingMib();

        using namespace klogg::mainwindow;
        QMenu* encodingsMenu = new QMenu(
            QApplication::translate( "klogg::mainwindow::menu", menu::encodingTitle ) );

        auto autoEncoding = encodingsMenu->addAction(
            QApplication::translate( "klogg::mainwindow::action", action::autoEncodingText ) );
        autoEncoding->setStatusTip(
            QApplication::translate( "klogg::mainwindow::action", action::autoEncodingStatusTip ) );
        autoEncoding->setCheckable( true );
        autoEncoding->setActionGroup( actionGroup );
        autoEncoding->setChecked( defaultEncodingMib < 0 );

        encodingsMenu->addSeparator();

        QTextCodec* systemCodec = nullptr;

#ifdef Q_OS_WIN
        auto systemCodePage = ::GetACP();
        systemCodec
            = QTextCodec::codecForName( QString( "CP%1" ).arg( systemCodePage ).toLatin1() );
#endif
        if ( systemCodec == nullptr ) {
            systemCodec = QTextCodec::codecForLocale();
        }

        auto systemEncodingName
            = QApplication::tr( "System (%1)" ).arg( systemCodec->name().constData() );

        auto systemEncoding = encodingsMenu->addAction( systemEncodingName );
        systemEncoding->setCheckable( true );
        systemEncoding->setActionGroup( actionGroup );
        systemEncoding->setData( systemCodec->mibEnum() );

        encodingsMenu->addSeparator();

        for ( const auto& group : supportedEncodings ) {
            auto menu = encodingsMenu->addMenu( QString::fromStdString( group.first ) );
            for ( const auto mib : group.second ) {
                auto codec = QTextCodec::codecForMib( mib );
                if ( codec ) {
                    auto action = menu->addAction( QString::fromLatin1( codec->name() ) );
                    action->setActionGroup( actionGroup );
                    action->setData( mib );
                    action->setCheckable( true );
                    action->setChecked( defaultEncodingMib == mib );
                }
            }
        }

        return encodingsMenu;
    }
};

#endif // KLOGG_ENCODINGMENU_H
