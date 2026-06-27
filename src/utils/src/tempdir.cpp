#include "tempdir.h"

#include <QDir>
#include <QFileInfo>

#include "log.h"

namespace klogg {

namespace {

bool ensureUsable( const QString& dir )
{
    QDir d( dir );
    if ( !d.mkpath( QStringLiteral( "." ) ) ) {
        return false;
    }
    const QFileInfo info( d.absolutePath() );
    return info.isDir() && info.isWritable();
}

} // namespace

QString resolveTempRoot( const QString& customDir, const QString& systemTempDir )
{
    const QString base = customDir.isEmpty() ? systemTempDir : customDir;
    const QString root = QDir( base ).filePath( QStringLiteral( "klogg_temp" ) );

    if ( ensureUsable( root ) ) {
        return root;
    }

    if ( !customDir.isEmpty() ) {
        LOG_WARNING << "Custom temp folder unusable, falling back to system temp: "
                    << customDir.toStdString();
    }

    const QString fallback = QDir( systemTempDir ).filePath( QStringLiteral( "klogg_temp" ) );
    ensureUsable( fallback );
    return fallback;
}

} // namespace klogg
