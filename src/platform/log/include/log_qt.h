/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
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

#ifndef KLOGG_LOG_QT_H
#define KLOGG_LOG_QT_H

#include <ostream>

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLatin1String>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringView>
#include <QUrl>
#include <QVariant>

// Qt-side iostream adapters so Qt code can log Qt types through the unified
// platform logger. This header is header-only and must not be included by the
// Qt-free core; the logging library itself does not depend on Qt.
inline std::ostream& operator<<( std::ostream& os, QLatin1String value )
{
    return os << QString( value ).toStdString();
}

inline std::ostream& operator<<( std::ostream& os, QStringView value )
{
    return os << value.toString().toStdString();
}

inline std::ostream& operator<<( std::ostream& os, const QString& value )
{
    return os << value.toStdString();
}

inline std::ostream& operator<<( std::ostream& os, const QByteArray& value )
{
    return os << value.toStdString();
}

inline std::ostream& operator<<( std::ostream& os, const QSize& value )
{
    return os << value.width() << "x" << value.height();
}

inline std::ostream& operator<<( std::ostream& os, const QPoint& value )
{
    return os << value.x() << "," << value.y();
}

inline std::ostream& operator<<( std::ostream& os, const QRect& value )
{
    return os << value.x() << "," << value.y() << " " << value.width() << "x"
              << value.height();
}

inline std::ostream& operator<<( std::ostream& os, const QColor& value )
{
    return os << value.name( QColor::HexArgb ).toStdString();
}

inline std::ostream& operator<<( std::ostream& os, const QVariant& value )
{
    return os << value.toString().toStdString();
}

inline std::ostream& operator<<( std::ostream& os, const QUrl& value )
{
    return os << value.toString().toStdString();
}

inline std::ostream& operator<<( std::ostream& os, const QFileInfo& value )
{
    return os << value.filePath().toStdString();
}

inline std::ostream& operator<<( std::ostream& os, const QDir& value )
{
    return os << value.path().toStdString();
}

inline std::ostream& operator<<( std::ostream& os, const QDateTime& value )
{
    return os << value.toString( Qt::ISODateWithMs ).toStdString();
}

#endif // KLOGG_LOG_QT_H
