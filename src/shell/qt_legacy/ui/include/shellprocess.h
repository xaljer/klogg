#ifndef KLOGG_SHELLPROCESS_H
#define KLOGG_SHELLPROCESS_H

#include <QProcess>

namespace ShellProcess {

void setup( QProcess* process, const QString& command );
void stop( QProcess* process );

} // namespace ShellProcess

#endif
