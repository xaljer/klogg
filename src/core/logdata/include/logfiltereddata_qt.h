#ifndef KLOGG_LOGFILTEREDDATA_QT_H
#define KLOGG_LOGFILTEREDDATA_QT_H

#include <QMetaType>

#include "logfiltereddata.h"

// Qt side-car for the Qt-free LogFilteredData header: the UI stores Visibility
// in QVariant, which needs the metatype declaration to be visible at the
// consumer, not in the core header.
Q_DECLARE_METATYPE( LogFilteredData::Visibility )

#endif // KLOGG_LOGFILTEREDDATA_QT_H
