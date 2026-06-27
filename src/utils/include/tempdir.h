#ifndef KLOGG_TEMPDIR_H
#define KLOGG_TEMPDIR_H

#include <QString>

namespace klogg {

// Resolves the directory under which klogg places its temporary files.
// customDir: user-configured path ("" => use systemTempDir).
// systemTempDir: absolute path of the system temp dir (injected for testability).
// Ensures the returned directory exists and is writable; on failure with a
// non-empty customDir it falls back to the system temp folder.
QString resolveTempRoot( const QString& customDir, const QString& systemTempDir );

} // namespace klogg

#endif // KLOGG_TEMPDIR_H
