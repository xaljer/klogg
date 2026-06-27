#include <catch2/catch.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "tempdir.h"

TEST_CASE( "resolveTempRoot uses system temp when custom is empty", "[tempdir]" )
{
    QTemporaryDir sysTemp;
    REQUIRE( sysTemp.isValid() );

    const QString root = klogg::resolveTempRoot( QString{}, sysTemp.path() );

    REQUIRE( root == QDir( sysTemp.path() ).filePath( "klogg_temp" ) );
    REQUIRE( QDir( root ).exists() );
}

TEST_CASE( "resolveTempRoot uses custom dir when set and writable", "[tempdir]" )
{
    QTemporaryDir sysTemp;
    QTemporaryDir customParent;
    REQUIRE( sysTemp.isValid() );
    REQUIRE( customParent.isValid() );

    const QString root = klogg::resolveTempRoot( customParent.path(), sysTemp.path() );

    REQUIRE( root == QDir( customParent.path() ).filePath( "klogg_temp" ) );
    REQUIRE( QDir( root ).exists() );
}

TEST_CASE( "resolveTempRoot falls back to system temp when custom is invalid", "[tempdir]" )
{
    QTemporaryDir sysTemp;
    REQUIRE( sysTemp.isValid() );

    // A path *under* a regular file: mkpath must fail on every platform.
    QTemporaryDir holder;
    REQUIRE( holder.isValid() );
    const QString filePath = QDir( holder.path() ).filePath( "a_file" );
    QFile f( filePath );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "x" );
    f.close();
    const QString badCustom = QDir( filePath ).filePath( "sub" );

    const QString root = klogg::resolveTempRoot( badCustom, sysTemp.path() );

    REQUIRE( root == QDir( sysTemp.path() ).filePath( "klogg_temp" ) );
    REQUIRE( QDir( root ).exists() );
}
