include(${CMAKE_CURRENT_LIST_DIR}/../version.cmake)

include_directories(${CMAKE_BINARY_DIR}/generated)

# Container builds read the bind-mounted checkout as root, which git rejects
# unless the path is marked safe; without this the commit stamp stays empty.
execute_process(
  COMMAND git config --global --add safe.directory "${CMAKE_SOURCE_DIR}"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  ERROR_QUIET
)

# version.h is stamped at configure time so it is a normal generated include:
# the compiler's dependency scan then handles recompiles and ninja needs no
# extra rule for it. The version numbers themselves live in build/version.cmake;
# git only contributes informational values for crash reports.
execute_process(
  COMMAND git describe --tags --always --dirty
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE KLOGG_GIT_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
execute_process(
  COMMAND git log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE KLOGG_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE KLOGG_GIT_SHORT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
# Package file names carry the short commit so two builds of an unbumped version
# stay distinguishable; the binaries themselves report the clean version.
if(NOT KLOGG_GIT_SHORT)
  set(KLOGG_GIT_SHORT "nogit")
endif()

string(TIMESTAMP KLOGG_BUILD_DATE "%Y-%m-%d" UTC)

configure_file(
  "${CMAKE_SOURCE_DIR}/build/cmake/version.h.in"
  "${CMAKE_BINARY_DIR}/generated/version.h"
  @ONLY
)

# Re-run configure when the version or the checkout changes, so an incremental
# build cannot keep reporting a stale commit.
set(_klogg_version_depends
    "${CMAKE_SOURCE_DIR}/build/version.cmake"
    "${CMAKE_SOURCE_DIR}/build/cmake/version.h.in"
)
if(EXISTS "${CMAKE_SOURCE_DIR}/.git/HEAD")
  list(APPEND _klogg_version_depends "${CMAKE_SOURCE_DIR}/.git/HEAD")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_klogg_version_depends})

message("klogg ui ${KLOGG_UI_VERSION}, app ${KLOGG_APP_VERSION}, api ${KLOGG_API_VERSION} (${KLOGG_GIT_VERSION})")

include(win32_rc)

# The Qt UI executable carries the resource version, so it uses the UI version.
generate_product_version(
  ProductVersionResourceFiles
  NAME
  "${PROJECT_DESCRIPTION}"
  ORIGINAL_FILENAME
  ${PROJECT_NAME}
  ICON
  "${ICON_FILE}"
  VERSION_MAJOR
  ${PROJECT_VERSION_MAJOR}
  VERSION_MINOR
  ${PROJECT_VERSION_MINOR}
  VERSION_PATCH
  ${PROJECT_VERSION_PATCH}
  VERSION_REVISION
  0
  COMPANY_NAME
  ${COMPANY}
  COMPANY_COPYRIGHT
  ${COPYRIGHT}
)
