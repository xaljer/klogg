// Precompiled header for the Qt UI library.
//
// Every translation unit in klogg_ui pulls in the same Qt Widgets surface, so
// precompiling it once cuts a large part of the per-file parse cost. Keep this
// list limited to headers that are actually used everywhere: adding rarely used
// headers only grows the PCH without paying it back.
//
// Not named ui_*.h on purpose: AUTOUIC treats such includes as uic output and
// would look for a matching .ui file.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QDialog>
#include <QFile>
#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVector>
#include <QWidget>
