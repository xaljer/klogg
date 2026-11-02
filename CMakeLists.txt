cmake_minimum_required(VERSION 3.12)

# Set minimum policy version for compatibility with dependencies using older CMake
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# All version numbers live in one file; the project version is the Qt UI one
# because the packages (deb/dmg/exe) ship that binary.
include(${CMAKE_CURRENT_LIST_DIR}/build/version.cmake)

project(
  klogg
  VERSION ${KLOGG_UI_VERSION}
  DESCRIPTION "klogg log viewer"
  LANGUAGES C CXX ASM
)

# Optional compiler cache (sccache/ccache): accelerates clean and branch-switch
# rebuilds. Detected automatically; a no-op when neither is installed. Force a
# launcher with -DCMAKE_CXX_COMPILER_LAUNCHER=..., disable with
# -DKLOGG_USE_COMPILER_CACHE=OFF.
option(KLOGG_USE_COMPILER_CACHE "Use sccache/ccache when available" ON)
if(KLOGG_USE_COMPILER_CACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  find_program(KLOGG_COMPILER_CACHE_PROGRAM NAMES sccache ccache)
  if(KLOGG_COMPILER_CACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${KLOGG_COMPILER_CACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${KLOGG_COMPILER_CACHE_PROGRAM}")
    message(STATUS "Using compiler cache: ${KLOGG_COMPILER_CACHE_PROGRAM}")
  endif()
endif()

set(PROJECT_HOMEPAGE_URL "https://github.com/variar/klogg")

string(TIMESTAMP PRODUCT_CURRENT_YEAR "%Y")
set(COMPANY "Anton Filimonov")
set(COPYRIGHT "Copyright (c) ${PRODUCT_CURRENT_YEAR} Nicolas Bonnefon, Anton Filimonov and other contributors.")
set(IDENTIFIER "com.github.variar.klogg")
set(PROJECT_DESCRIPTION "${PROJECT_NAME} log viewer")

option(KLOGG_BUILD_SERVER_ONLY "Build only the Qt-free klogg_server host" OFF)
option(KLOGG_BUILD_TESTS "Build tests" ON)
option(KLOGG_USE_LTO "Use link time optimization" ON)
option(KLOGG_USE_SENTRY "Use Sentry" OFF)
option(KLOGG_USE_PERFETTO "Instrument the api module with Perfetto tracing" OFF)
option(KLOGG_GENERIC_CPU "Build for generic CPU" OFF)
option(KLOGG_OSX_DEPLOYMENT_TARGET "Override target MacOS version" "")

# The CLI server and the core beneath it are Qt-free, so a server-only configure
# never looks for Qt. Its test suites are Qt-free as well: this mode is the
# zero-Qt gate CI runs without installing Qt at all. The cross-cutting test
# directory (UI, benchmarks) and the API tests that need a QGuiApplication host
# are gated out below.
if(KLOGG_BUILD_SERVER_ONLY)
  message(STATUS "KLOGG_BUILD_SERVER_ONLY: Qt-free build, Qt-only test suites are skipped")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(KLOGG_ARCH 32)
else()
  set(KLOGG_ARCH 64)
endif()

if(WIN32)
  if(KLOGG_ARCH EQUAL 64)
    option(KLOGG_USE_HYPERSCAN "Use Hyperscan" ON)
    option(KLOGG_USE_VECTORSCAN "Use Vectorscan" OFF)
  else()
    option(KLOGG_USE_HYPERSCAN "Use Hyperscan" OFF)
    option(KLOGG_USE_VECTORSCAN "Use Vectorscan" OFF)
  endif()
else()
    option(KLOGG_USE_HYPERSCAN "Use Hyperscan" OFF)
    option(KLOGG_USE_VECTORSCAN "Use Vectorscan" ON)
endif()


set(NOTICE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/NOTICE)
set(COPYING_FILE ${CMAKE_CURRENT_SOURCE_DIR}/COPYING)

set(README_FILE ${CMAKE_CURRENT_SOURCE_DIR}/README.md)

set(DOCUMENTATION_FILE ${CMAKE_CURRENT_SOURCE_DIR}/DOCUMENTATION.md)
set(DOCUMENTATION_HTML ${CMAKE_BINARY_DIR}/generated/documentation.html)
set(DOCUMENTATION_RESOURCE ${CMAKE_BINARY_DIR}/generated/documentation.qrc)

if(WIN32)
  set(ICON_NAME klogg.ico)
elseif(APPLE)
  set(ICON_NAME klogg.icns)
endif()

if(NOT
   ${ICON_NAME}
   STREQUAL
   ""
)
  set(ICON_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/shell/qt_legacy/app/resources/${ICON_NAME})
endif()

# /experimental:external + /external:I are MSVC-only; clang-cl (MSVC ABI,
# Compiler ID Clang) accepts them only in recent versions, so skip them there.
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.14)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /experimental:external /external:W0 ")
  set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/external:I ")
endif()

set(out_dir "${CMAKE_BINARY_DIR}/output")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${out_dir}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${out_dir}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${out_dir}")

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/build/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)

include(StandardProjectSettings)
include(ucm)
include(prepare_version)
include(FindPkgConfig)
include(GNUInstallDirs)

if(MSVC)
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(
      REGEX
      REPLACE "/W[0-4]"
              "/W4"
              CMAKE_CXX_FLAGS
              "${CMAKE_CXX_FLAGS}"
    )
  else(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")

  if(CMAKE_C_FLAGS MATCHES "/W[0-4]")
    string(
      REGEX
      REPLACE "/W[0-4]"
              "/W4"
              CMAKE_C_FLAGS
              "${CMAKE_C_FLAGS}"
    )
  else(CMAKE_C_FLAGS MATCHES "/W[0-4]")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")
  endif(CMAKE_C_FLAGS MATCHES "/W[0-4]")

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /D __SSE4_1__=1 /D __SSE4_2__=1")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D __SSE4_1__=1 /D __SSE4_2__=1")

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj ")

  # Build-tree size: skip the incremental-link .ilk files (/INCREMENTAL:NO) and
  # embed debug info in each object (/Z7, "Embedded") instead of one compiler
  # PDB per target. Both trade a little relink/obj size for a much smaller tree.
  # These must be cache values: the generator reads them directly, normal
  # variables are ignored. The replacements are idempotent across reconfigures.
  foreach(_cfg DEBUG RELWITHDEBINFO)
    # Idempotent: only rewrite a bare /INCREMENTAL, so re-running configure
    # never produces /INCREMENTAL:NO:NO.
    foreach(_lf CMAKE_EXE_LINKER_FLAGS_${_cfg}
                CMAKE_SHARED_LINKER_FLAGS_${_cfg}
                CMAKE_MODULE_LINKER_FLAGS_${_cfg})
      set(_flags "${${_lf}}")
      string(FIND "${_flags}" "/INCREMENTAL:NO" _already_no)
      if(_already_no EQUAL -1)
        string(REPLACE "/INCREMENTAL" "/INCREMENTAL:NO" _flags "${_flags}")
        set(${_lf}
            "${_flags}"
            CACHE STRING "Linker flags"
            FORCE
        )
      endif()
    endforeach()
    string(REPLACE "/Zi" "/Z7" _cxx "${CMAKE_CXX_FLAGS_${_cfg}}")
    set(CMAKE_CXX_FLAGS_${_cfg}
        "${_cxx}"
        CACHE STRING "C++ flags"
        FORCE
    )
    string(REPLACE "/Zi" "/Z7" _c "${CMAKE_C_FLAGS_${_cfg}}")
    set(CMAKE_C_FLAGS_${_cfg}
        "${_c}"
        CACHE STRING "C flags"
        FORCE
    )
  endforeach()
else()
  ucm_add_flags(CXX "-fno-sized-deallocation -fno-omit-frame-pointer")

  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
    ucm_add_flags(C CXX "-march=native -mtune=generic")
  else()
    ucm_add_flags(C CXX "-mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mpopcnt")
    if(KLOGG_GENERIC_CPU)
      ucm_add_flags(C CXX "-march=x86-64 -mtune=generic")
    else()
      ucm_add_flags(C CXX "-march=native -mtune=generic")
    endif()
  endif()
endif(MSVC)

if(WIN32)
  set(SYMS_TOOLS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/dump_syms/win)
elseif(APPLE)
  set(SYMS_TOOLS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/dump_syms/mac)
else()
  set(SYMS_TOOLS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/dump_syms/linux)
endif()

message("Dump symbols tools path ${SYMS_TOOLS}")

# standard compiler warnings
# Link this 'library' to use the warnings specified in CompilerWarnings.cmake
include(CompilerWarnings)
add_library(project_warnings INTERFACE)
set_project_warnings(project_warnings)

# Link this 'library' to set the c++ standard / compile-time options requested
include(CompilerFlags)
add_library(project_options INTERFACE)
set_project_compile_flags(project_options)

# sanitizer options if supported by compiler
include(Sanitizers)
enable_sanitizers(project_options)

# allow for static analysis options
include(StaticAnalyzers)

ucm_print_flags()

if(APPLE)
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
    message("Adding missing Threads workaround")
    set(CMAKE_THREAD_LIBS_INIT "-lpthread")
    set(CMAKE_HAVE_THREADS_LIBRARY 1)
    set(CMAKE_USE_WIN32_THREADS_INIT 0)
    set(CMAKE_USE_PTHREADS_INIT 1)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
  endif()
endif()

if(NOT KLOGG_BUILD_SERVER_ONLY)
  find_package(
    QT
    NAMES
    Qt6
    Qt5
    COMPONENTS Core
               Widgets
               Concurrent
               Network
               Xml
    REQUIRED
  )
  find_package(
    Qt${QT_VERSION_MAJOR}
    COMPONENTS Core
               Widgets
               Concurrent
               Network
               Xml
    REQUIRED
  )

  # QTextCodec survives only in the Qt UI and the tests that build Qt-encoded
  # fixtures; the Core API resolves encoding names through klogg::enc. Qt5 keeps
  # QTextCodec in QtCore, so the interface target stays empty there.
  add_library(klogg_qt_codec INTERFACE)
  if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    find_package(
      Qt6
      COMPONENTS Core5Compat
      REQUIRED
    )
    target_link_libraries(klogg_qt_codec INTERFACE Qt6::Core5Compat)
  endif()

  set(KLOGG_QT_VERSION ${Qt${QT_VERSION_MAJOR}Core_VERSION})

  message("Qt version ${KLOGG_QT_VERSION}")
endif()
message("LTO ${KLOGG_USE_LTO}")

if(APPLE)
  if(${KLOGG_OSX_DEPLOYMENT_TARGET})
    set(CMAKE_OSX_DEPLOYMENT_TARGET ${KLOGG_OSX_DEPLOYMENT_TARGET})
    message("OSX target ${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif()
endif()

set(CMAKE_AUTOMOC OFF)
set(CMAKE_INCLUDE_CURRENT_DIR OFF)
set(BUILD_SHARED_LIBS OFF)

add_custom_target(ci_build)

enable_testing()

add_subdirectory(3rdparty)
add_subdirectory(src)
if(NOT KLOGG_BUILD_SERVER_ONLY)
  add_test(NAME klogg_smoke COMMAND klogg -platform offscreen -v)
endif()

if(${KLOGG_BUILD_TESTS} AND NOT KLOGG_BUILD_SERVER_ONLY)
  add_subdirectory(tests)
endif()

# Installation and packaging
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY true)
set(CPACK_SOURCE_IGNORE_FILES "build_root;.git")

set(CPACK_OUTPUT_FILE_PREFIX "packages")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_VENDOR ${COMPANY})
set(CPACK_PACKAGE_CONTACT "anton.filimonov@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/build/package/description.txt")
set(CPACK_RESOURCE_FILE_LICENSE "${COPYING_FILE}")
set(CPACK_PACKAGE_VERSION "${KLOGG_UI_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "klogg-${KLOGG_UI_VERSION}-${KLOGG_GIT_SHORT}")
set(CPACK_PACKAGE_ICON "${ICON_FILE}")

if(UNIX)
  include(InstallRequiredSystemLibraries)

  set(CPACK_STRIP_FILES "bin/klogg")

  if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_FORMAT "UDBZ")
    set(CPACK_DMG_VOLUME_NAME "${PROJECT_NAME}")
    set(CPACK_SYSTEM_NAME "OSX")
    set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/build/package/osx/dmg_background.tif")
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/build/package/osx/dmg_setup.scpt")

  else()
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/shell/qt_legacy/app/images/hicolor/16x16/klogg.png
            DESTINATION share/icons/hicolor/16x16/apps
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/shell/qt_legacy/app/images/hicolor/32x32/klogg.png
            DESTINATION share/icons/hicolor/32x32/apps
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/shell/qt_legacy/app/images/hicolor/48x48/klogg.png
            DESTINATION share/icons/hicolor/48x48/apps
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/shell/qt_legacy/app/images/hicolor/scalable/klogg.svg
            DESTINATION share/icons/hicolor/scalable/apps
    )

    install(
      FILES ${COPYING_FILE}
            ${NOTICE_FILE}
            ${README_FILE}
            ${DOCUMENTATION_HTML}
            ${DOCUMENTATION_FILE}
      DESTINATION ${CMAKE_INSTALL_DOCDIR}
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/build/package/linux/klogg.desktop DESTINATION share/applications)

    set(CPACK_GENERATOR DEB)

    #deb
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "http://github.com/variar/klogg")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Anton Filimonov <${CPACK_PACKAGE_CONTACT}>")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS YES)

    #RPM
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
    set(CPACK_RPM_PACKAGE_LICENSE "GPLv3")
    set(CPACK_RPM_PACKAGE_AUTOREQ YES)
    #set(CPACK_RPM_PACKAGE_REQUIRES "qt5-qtbase >= ${KLOGG_QT_VERSION}")
    set(CPACK_RPM_PACKAGE_URL "http://github.com/variar/klogg")
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/share/applications
        /usr/share/icons
        /usr/share/icons/hicolor
        /usr/share/icons/hicolor/16x16
        /usr/share/icons/hicolor/16x16/apps
        /usr/share/icons/hicolor/32x32
        /usr/share/icons/hicolor/32x32/apps
        /usr/share/icons/hicolor/scalable
        /usr/share/icons/hicolor/scalable/apps
    )
  endif()
  include(CPack)
endif()

# for IDE grouping
add_custom_target(
  installation
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/build/package/windows/klogg.nsi
          ${CMAKE_CURRENT_SOURCE_DIR}/build/package/linux/klogg.desktop
          ${CMAKE_SOURCE_DIR}/build/cmake/MacOSXBundleInfo.plist.in
)

add_custom_target(
  klogg_docs
  SOURCES ${README_FILE}
          ${COPYING_FILE}
          ${NOTICE_FILE}
          ${DOCUMENTATION_FILE}
)

add_custom_target(klogg_stuff SOURCES .clang-format .clang-tidy .cmake-format)
