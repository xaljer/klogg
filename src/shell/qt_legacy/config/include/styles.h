/*
 * Copyright (C) 2020 Anton Filimonov and other contributors
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

#ifndef KLOGG_STYLES
#define KLOGG_STYLES

#include <QStringList>
#include <QLatin1String>
#include <map>

struct ThemeManager {

    static constexpr QLatin1String FusionKey = QLatin1String( "Fusion", 6 );

    static constexpr QLatin1String LightThemeKey = QLatin1String( "Light", 5 );
    static constexpr QLatin1String SpringThemeKey = QLatin1String( "Spring", 6 );
    static constexpr QLatin1String DarkThemeKey = QLatin1String( "Dark", 4 );
    static constexpr QLatin1String NordThemeKey = QLatin1String( "Nord", 4 );
    static constexpr QLatin1String NordLightThemeKey = QLatin1String( "nord-light", 10 );

    static QStringList availableThemes();
    static std::map<QString, QString> defaultThemePalette( const QString& theme );

    static void applyFrameworkStyle();
    static void applyTheme( const QString& theme );
};

#endif
