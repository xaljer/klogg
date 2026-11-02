#include "shellprocess.h"

#include <QStringList>

namespace ShellProcess {

void setup( QProcess* process, const QString& command )
{
    process->setProcessChannelMode( QProcess::ForwardedErrorChannel );
    process->setNativeArguments( QString( "/c \"%1\"" ).arg( command ) );
    process->start( QString( "cmd" ), QStringList{} );
}

void stop( QProcess* process )
{
    const auto pid = process->processId();
    if ( pid > 0 ) {
        QProcess::startDetached( QString( "taskkill" ),
                                 { QString( "/F" ), QString( "/T" ),
                                   QString( "/PID" ), QString::number( pid ) } );
    }
}

} // namespace ShellProcess
