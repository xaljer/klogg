find_package(QT NAMES Qt6 Qt5 COMPONENTS Test REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test REQUIRED)

# Module unit tests live next to their module (src/<layer>/<module>/tests).
# This directory keeps only cross-cutting suites: Qt integration tests, shared
# helpers and benchmarks.
add_subdirectory(helpers)
add_subdirectory(ui)
add_subdirectory(bench)

add_dependencies(klogg_itests file_write_helper)

# Module suites declare their own ci_build dependency next to the target, because
# they also build in the Qt-free tree. This directory only contributes the
# Qt-only cross-cutting suites.
if(TARGET ci_build)
  add_dependencies(ci_build klogg_ui_tests klogg_itests)
endif()
