/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

#pragma once

#include <QFile>
#include <QString>
#include <QtCore/qglobal.h>

// Helper to get test log file path from environment variable
// Usage: set KLOGG_TEST_LOG_FILE=D:\path\to\test.log
inline QString getTestLogFile()
{
    QString envPath = qEnvironmentVariable( "KLOGG_TEST_LOG_FILE" );
    if ( !envPath.isEmpty() && QFile::exists( envPath ) ) {
        return envPath;
    }
    return QString();
}
