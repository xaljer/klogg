#include "shellprocess.h"

#include <QTimer>

namespace ShellProcess {

void setup( QProcess* process, const QString& command )
{
    process->setProcessChannelMode( QProcess::ForwardedErrorChannel );
    process->start( QString( "/bin/sh" ), { QString( "-c" ), command } );
}

void stop( QProcess* process )
{
    const auto pid = process->processId();
    if ( pid <= 0 ) {
        return;
    }

    QProcess::startDetached( QString( "kill" ),
                             { QString( "-TERM" ), QString( "-%1" ).arg( pid ) } );

    QTimer::singleShot( 2000, process, [ pid ]() {
        QProcess::startDetached( QString( "kill" ),
                                 { QString( "-KILL" ), QString( "-%1" ).arg( pid ) } );
    } );
}

} // namespace ShellProcess
