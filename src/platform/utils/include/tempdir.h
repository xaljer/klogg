#ifndef KLOGG_TEMPDIR_H
#define KLOGG_TEMPDIR_H

#include <string>

namespace klogg {

// Resolves the directory under which klogg places its temporary files.
// customDir: user-configured path ("" => use systemTempDir).
// systemTempDir: absolute path of the system temp dir (injected for testability).
// Ensures the returned directory exists and is writable; on failure with a
// non-empty customDir it falls back to the system temp folder.
// Both arguments and the result are UTF-8.
std::string resolveTempRoot( const std::string& customDir, const std::string& systemTempDir );

} // namespace klogg

#endif // KLOGG_TEMPDIR_H
