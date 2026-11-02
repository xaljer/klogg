set(QAPPLICATION_CLASS QApplication)

include(CPM)

set(_TMP_CPM_USE_LOCAL_PACKAGES ${CPM_USE_LOCAL_PACKAGES})

cpmaddpackage("gh:simdutf/simdutf@5.6.2")
if(MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND TARGET simdutf)
  # clang-cl compiles simdutf's MSVC branch: MSVC accepts SSE/AVX intrinsics
  # regardless of target feature, clang does not, and simdutf's /WX becomes
  # -Werror. Enable the features explicitly and relax the warning gate.
  target_compile_options(simdutf PRIVATE -mssse3 -msse4.1 -msse4.2 -mavx2 -Wno-error)
endif()
cpmaddpackage("gh:foonathan/type_safe@0.2.4")
cpmaddpackage("gh:RoaringBitmap/CRoaring@4.2.1")
if(CRoaring_ADDED)
  # Qt moc's preprocessor cannot parse the GCC-extension "#ifndef !defined(X) ||
  # !defined(Y)" in portability.h; overlay the equivalent #if defined(...) form.
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/patches/croaring-4.2.1/include/roaring/portability.h
    ${CRoaring_SOURCE_DIR}/include/roaring/portability.h
    COPYONLY
  )
endif()
cpmaddpackage("gh:lemire/streamvbyte@1.0.0")
cpmaddpackage("gh:TartanLlama/expected@1.1.0")

if(APPLE)
  cpmaddpackage(
    NAME
    macdeployqtfix
    GITHUB_REPOSITORY
    arl/macdeployqtfix
    GIT_TAG
    df888505849d3c06d20a4338af276dfa7d11826a
    DOWNLOAD_ONLY
    YES
  )
  if(macdeployqtfix_ADDED)
    configure_file(${macdeployqtfix_SOURCE_DIR}/macdeployqtfix.py ${CMAKE_BINARY_DIR}/macdeployqtfix.py COPYONLY)
  endif()
endif()

cpmaddpackage(
  NAME
  maddy
  GITHUB_REPOSITORY
  variar/maddy
  GIT_TAG
  602e26613e624535e2de883b3f2c98a16729d1d4
  EXCLUDE_FROM_ALL
  YES
)

if(KLOGG_USE_HYPERSCAN)
  cpmaddpackage(
    NAME
    hyperscan
    GITHUB_REPOSITORY
    variar/hyperscan
    GIT_TAG
    0931a40e0cf1d7f92189bc546c3491ed5c113f8b
    EXCLUDE_FROM_ALL
    YES
    OPTIONS
    "CMAKE_CXX_STANDARD 17"
    "CMAKE_CXX_STANDARD_REQUIRED ON"
  )
  
  message("Adding alias for hyperscan")
  add_library(klogg_hyperscan INTERFACE)
  target_link_libraries(klogg_hyperscan INTERFACE hs)
  target_include_directories(klogg_hyperscan INTERFACE ${hyperscan_SOURCE_DIR}/src)
elseif(KLOGG_USE_VECTORSCAN)
  cpmaddpackage(
    NAME
    vectorscan
    GITHUB_REPOSITORY
    VectorCamp/vectorscan
    GIT_TAG
    d29730e1cb9daaa66bda63426cdce83505d2c809
    EXCLUDE_FROM_ALL
    YES
    OPTIONS
    "BUILD_STATIC_LIBS ON"
    "BUILD_UNIT OFF"
    "BUILD_TOOLS OFF"
    "BUILD_EXAMPLES OFF"
    "BUILD_BENCHMARKS OFF"
    "BUILD_DOC OFF"
    "BUILD_CHIMERA OFF"
    "BUIlD_AVX2 OFF"
    "BUIlD_AVX512 OFF"
    "BUIlD_AVX512VBMI OFF"
    "FAT_RUNTIME OFF"
    "CMAKE_CXX_STANDARD 17"
    "CMAKE_CXX_STANDARD_REQUIRED ON"
  )
 
  message("Adding alias for vectorscan")
  add_library(klogg_vectorscan INTERFACE)
  target_link_libraries(klogg_vectorscan INTERFACE hs)
  target_include_directories(klogg_vectorscan INTERFACE 
    ${vectorscan_SOURCE_DIR}/src
    ${vectorscan_BINARY_DIR}
  )
endif()

cpmaddpackage(
  NAME
  Uchardet
  GIT_REPOSITORY
  https://gitlab.freedesktop.org/uchardet/uchardet
  VERSION
  0.0.8
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "BUILD_BINARY OFF"
)
if(Uchardet_ADDED)
  message("Adding alias for uchardet")
  add_library(klogg_uchardet_wrapper INTERFACE)
  target_link_libraries(klogg_uchardet_wrapper INTERFACE libuchardet)
  target_include_directories(klogg_uchardet_wrapper INTERFACE ${Uchardet_SOURCE_DIR}/src)
else()
  add_library(klogg_uchardet_wrapper INTERFACE)
  target_link_libraries(klogg_uchardet_wrapper INTERFACE ${UCHARDET_LIBRARY})
  target_include_directories(klogg_uchardet_wrapper INTERFACE ${UCHARDET_INCLUDE_DIR})
endif()

if(NOT KLOGG_BUILD_SERVER_ONLY)
  if(QT_VERSION_MAJOR AND ${QT_VERSION_MAJOR} EQUAL 6)
    set(CPM_USE_LOCAL_PACKAGES OFF)
  endif()

  cpmaddpackage(
    NAME
    KF5Archive
    GITHUB_REPOSITORY
    variar/klogg_karchive
    GIT_TAG
    f546bf6ae66a8d34b43da5a41afcfbf4e1a47906
    EXCLUDE_FROM_ALL
    YES
  )
  if(NOT KF5Archive_ADDED)
    find_package(KF5Archive)
    add_library(klogg_karchive INTERFACE)
    target_link_libraries(klogg_karchive INTERFACE KF5::Archive)
  endif()

  if(QT_VERSION_MAJOR AND ${QT_VERSION_MAJOR} EQUAL 6)
    message("Resetting CPM_USE_LOCAL_PACKAGES to ${_TMP_CPM_USE_LOCAL_PACKAGES}")
    set(CPM_USE_LOCAL_PACKAGES ${_TMP_CPM_USE_LOCAL_PACKAGES})
  endif()
endif()

cpmaddpackage(
  NAME
  robin_hood
  GITHUB_REPOSITORY
  martinus/robin-hood-hashing
  GIT_TAG
  3.11.5
  EXCLUDE_FROM_ALL
  YES
)
if(NOT TARGET robin_hood)
  message("Adding imported target for robin_hood")
  add_library(robin_hood INTERFACE)
  target_include_directories(robin_hood INTERFACE ${ROBIN_HOOD_INCLUDE_DIRS})
endif(NOT TARGET robin_hood)

cpmaddpackage(
  NAME
  toml11
  GITHUB_REPOSITORY
  ToruNiina/toml11
  GIT_TAG
  v4.4.0
  EXCLUDE_FROM_ALL
  YES
)

# Dependency policy: normal libraries are fetched by CPM and cached in
# .cpm_cache (kept on disk, see AGENTS.md). The repository only carries patch
# files. URLs are pinned to immutable commit hashes so the cache is stable.

cpmaddpackage(
  NAME zlib
  URL https://codeload.github.com/madler/zlib/tar.gz/51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf
  URL_HASH SHA256=d9e270d46252734aa49770fbc544125391617956266f220bd63216c834f3a522
)

# zlib always defines a shared and a static target; the server links the static
# one through this alias.
add_library(klogg_zlib INTERFACE)
if(TARGET zlibstatic)
  target_link_libraries(klogg_zlib INTERFACE zlibstatic)
else()
  target_link_libraries(klogg_zlib INTERFACE zlib)
endif()
target_include_directories(klogg_zlib INTERFACE "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")

# Some dependencies (libarchive, crashpad) link ZLIB::ZLIB. CPM's generated
# Find<package>.cmake stubs in CPM_modules can shadow CMake's own FindZLIB on
# case-insensitive file systems (default macOS volumes), which leaves that
# imported target undefined; point it at the zlib added above instead.
if(NOT TARGET ZLIB::ZLIB)
  add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
  set_property(TARGET ZLIB::ZLIB PROPERTY INTERFACE_LINK_LIBRARIES klogg_zlib)
endif()

cpmaddpackage(
  NAME bzip2
  URL https://codeload.github.com/libarchive/bzip2/tar.gz/6a8690fc8d26c815e798c588f796eabe9d684cf0
  URL_HASH SHA256=bc40651201886d3e126a8fff33163076e65918d422ce1f18749362984a8ce01d
  DOWNLOAD_ONLY YES
)

# bzip2 has no build system of its own.
add_library(
  klogg_bzip2 STATIC
  "${bzip2_SOURCE_DIR}/blocksort.c"
  "${bzip2_SOURCE_DIR}/huffman.c"
  "${bzip2_SOURCE_DIR}/crctable.c"
  "${bzip2_SOURCE_DIR}/randtable.c"
  "${bzip2_SOURCE_DIR}/compress.c"
  "${bzip2_SOURCE_DIR}/decompress.c"
  "${bzip2_SOURCE_DIR}/bzlib.c"
)
target_include_directories(klogg_bzip2 PUBLIC "${bzip2_SOURCE_DIR}")
target_compile_definitions(klogg_bzip2 PRIVATE _FILE_OFFSET_BITS=64)
if(MSVC)
  target_compile_definitions(klogg_bzip2 PRIVATE _SCL_SECURE_NO_WARNINGS)
endif()

cpmaddpackage(
  NAME xz
  URL https://codeload.github.com/tukaani-project/xz/tar.gz/3ec664d3f652133136587a51d4505b1abe1acdd7
  URL_HASH SHA256=ba0aa899a72d7fab8619631fa5201aebe5087246ba48db0e79f0ed5c6c7292f7
  OPTIONS "BUILD_TESTING OFF" "ENABLE_NLS OFF"
)

add_library(klogg_liblzma INTERFACE)
target_link_libraries(klogg_liblzma INTERFACE liblzma)
target_include_directories(klogg_liblzma INTERFACE "${xz_SOURCE_DIR}/src/liblzma/api")
target_compile_definitions(klogg_liblzma INTERFACE LZMA_API_STATIC)

cpmaddpackage(
  NAME libarchive
  URL https://codeload.github.com/libarchive/libarchive/tar.gz/313aa1fa10b657de791e3202c168a6c833bc3543
  URL_HASH SHA256=fd64cb3fbcd35aa7966748a6e3ee1e27c32a50592a1f366a861b3d08960b057f
  DOWNLOAD_ONLY YES
)

# libarchive is built from the cached sources with the local codec targets and
# without its optional TLS/XML/CLI components. The two edited CMakeLists live
# under patches/ and are overlaid before add_subdirectory.
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/patches/libarchive-3.7.4/CMakeLists.txt"
  "${libarchive_SOURCE_DIR}/CMakeLists.txt"
  COPYONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/patches/libarchive-3.7.4/libarchive/CMakeLists.txt"
  "${libarchive_SOURCE_DIR}/libarchive/CMakeLists.txt"
  COPYONLY
)
set(KLOGG_ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR}")
set(KLOGG_ZLIB_BINARY_INCLUDE_DIR "${zlib_BINARY_DIR}")
set(KLOGG_BZIP2_INCLUDE_DIR "${bzip2_SOURCE_DIR}")
set(KLOGG_LZMA_INCLUDE_DIR "${xz_SOURCE_DIR}/src/liblzma/api")
add_subdirectory("${libarchive_SOURCE_DIR}" "${libarchive_BINARY_DIR}")

cpmaddpackage(
  NAME nlohmann_json
  URL https://codeload.github.com/nlohmann/json/tar.gz/9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03
  URL_HASH SHA256=0dbc5e40a01ff142e7e68c03e85247a4dcede2f592d12d3677dee3664d17975a
  DOWNLOAD_ONLY YES
)
add_library(nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
target_include_directories(nlohmann_json INTERFACE "${nlohmann_json_SOURCE_DIR}/single_include")

cpmaddpackage(
  NAME cpp_httplib
  URL https://codeload.github.com/yhirose/cpp-httplib/tar.gz/c419c904da4ed0d057f169c225c58cc795c07e4d
  URL_HASH SHA256=2a37947f7933d7bccbee420cee1ccf3bac6f1631ecebdc23625730df1452248e
  DOWNLOAD_ONLY YES
)
find_package(Threads REQUIRED)
add_library(klogg_httplib INTERFACE)
add_library(httplib::httplib ALIAS klogg_httplib)
target_include_directories(klogg_httplib SYSTEM INTERFACE "${cpp_httplib_SOURCE_DIR}")
target_link_libraries(klogg_httplib INTERFACE Threads::Threads)
if(WIN32)
  target_link_libraries(klogg_httplib INTERFACE ws2_32)
endif()



if(KLOGG_BUILD_TESTS)
  cpmaddpackage("gh:bombela/backward-cpp@1.6")
  cpmaddpackage(
    NAME
    Catch2
    GITHUB_REPOSITORY
    catchorg/Catch2
    VERSION
    2.13.8
    EXCLUDE_FROM_ALL
    YES
  )
  if(NOT TARGET Catch2)
    message("Adding imported target for catch2")
    add_library(Catch2 INTERFACE)
    target_include_directories(Catch2 INTERFACE ${CATCH2_INCLUDE_DIRS})
  endif(NOT TARGET Catch2)
endif()

if(NOT KLOGG_BUILD_SERVER_ONLY)
  cpmaddpackage(
    NAME
    KDSingleApplication
    GITHUB_REPOSITORY
    variar/KDSingleApplication
    GIT_TAG
    5b30db30266f92bc01f1439777803ce8dbf16c79
    DOWNLOAD_ONLY
    YES
  )
  if(KDSingleApplication_ADDED)
    add_library(
      kdsingleapp STATIC ${KDSingleApplication_SOURCE_DIR}/src/kdsingleapplication.cpp
                       ${KDSingleApplication_SOURCE_DIR}/src/kdsingleapplication_localsocket.cpp
    )

    target_include_directories(kdsingleapp PUBLIC ${KDSingleApplication_SOURCE_DIR}/src)
    target_link_libraries(kdsingleapp Qt${QT_VERSION_MAJOR}::Core)
    target_compile_definitions(kdsingleapp PUBLIC KDSINGLEAPPLICATION_STATIC_BUILD)

    set_target_properties(kdsingleapp PROPERTIES AUTOMOC ON)
  endif()
endif()


cpmaddpackage(
  NAME
  xxHash
  GITHUB_REPOSITORY
  Cyan4973/xxHash
  VERSION
  0.8.1
  DOWNLOAD_ONLY
  YES
)
if(xxHash_ADDED)
  set(XXHASH_BUILD_ENABLE_INLINE_API OFF)
  set(XXHASH_BUILD_XXHSUM OFF)
  add_subdirectory(${xxHash_SOURCE_DIR}/cmake_unofficial ${CMAKE_BINARY_DIR}/xxhash EXCLUDE_FROM_ALL)
endif()

cpmaddpackage(
  NAME
  whereami
  GITHUB_REPOSITORY
  gpakosz/whereami
  GIT_TAG
  dcb52a058dc14530ba9ae05e4339bd3ddfae0e0e
  DOWNLOAD_ONLY
  YES
)
if(whereami_ADDED)
  add_library(whereami STATIC ${whereami_SOURCE_DIR}/src/whereami.h ${whereami_SOURCE_DIR}/src/whereami.c)
  target_include_directories(whereami PUBLIC ${whereami_SOURCE_DIR}/src)
endif()

cpmaddpackage(
  NAME
  exprtk
  GITHUB_REPOSITORY
  variar/klogg_exprtk
  GIT_TAG
  1f9f4cd7d2620b7b24232de9ea22908d63913459
  DOWNLOAD_ONLY
  YES
)
if(exprtk_ADDED)
  add_library(exprtk INTERFACE)
  target_link_libraries(exprtk INTERFACE robin_hood)
  target_include_directories(exprtk SYSTEM INTERFACE ${exprtk_SOURCE_DIR})
  target_compile_definitions(
    exprtk
    INTERFACE -Dexprtk_disable_caseinsensitivity
              -Dexprtk_disable_comments
              -Dexprtk_disable_break_continue
              -Dexprtk_disable_return_statement
              -Dexprtk_disable_superscalar_unroll
              -Dexprtk_disable_rtl_io_file
              -Dexprtk_disable_rtl_vecops
              -Dexprtk_disable_string_capabilities
  )
endif()

if(NOT KLOGG_BUILD_SERVER_ONLY)
  cpmaddpackage(
    NAME
    KDToolBox
    GITHUB_REPOSITORY
    KDAB/KDToolBox
    GIT_TAG
    6468867d1a46eabe1bcb2cd342f338fe66f06675
    DOWNLOAD_ONLY
    YES
  )
  if(KDToolBox_ADDED)
    add_library(
      kdtoolbox STATIC ${KDToolBox_SOURCE_DIR}/qt/KDSignalThrottler/src/KDSignalThrottler.cpp
                       ${KDToolBox_SOURCE_DIR}/qt/KDSignalThrottler/src/KDSignalThrottler.h
    )

    target_include_directories(kdtoolbox PUBLIC ${KDToolBox_SOURCE_DIR}/qt/KDSignalThrottler/src)
    target_link_libraries(kdtoolbox Qt${QT_VERSION_MAJOR}::Core)
    set_target_properties(kdtoolbox PROPERTIES AUTOMOC ON)
  endif()
endif()

cpmaddpackage(
  NAME
  efsw
  GITHUB_REPOSITORY
  SpartanJ/efsw
  GIT_TAG
  1.4.1
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "BUILD_TEST_APP OFF"
)
if(efsw_ADDED)
  target_compile_definitions(efsw PRIVATE EFSW_FSEVENTS_NOT_SUPPORTED)
  if(WIN32)
    # Overlay patched efsw sources: (1) DestroyWatch freed the OVERLAPPED and
    # the notify buffer while a cancelled ReadDirectoryChangesW IRP could still
    # complete asynchronously, letting the kernel write into freed heap - the
    # free is now deferred until the completion packet is reaped from the IOCP;
    # (2) WatchCallback armed ReadDirectoryChangesW twice on zero-byte
    # completions, leaving multiple IRPs pending on the same OVERLAPPED
    foreach(efsw_patch_file WatcherWin32.cpp FileWatcherWin32.cpp FileWatcherWin32.hpp)
      configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/patches/efsw-1.4.1/${efsw_patch_file}
        ${efsw_SOURCE_DIR}/src/efsw/${efsw_patch_file}
        COPYONLY
      )
    endforeach()
  endif()
endif()

if(WIN32)
  set(BUILD_SHARED_LIBS ON)
endif()
cpmaddpackage(
  NAME
  tbb
  GITHUB_REPOSITORY
  variar/oneTBB
  GIT_TAG
  c9be1ac2930f02dea523003ed801b4489f3e6b6e
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "TBB_TEST OFF"
  "TBB_EXAMPLES OFF"
  "TBB_STRICT OFF"
)

if(WIN32)
  set(BUILD_SHARED_LIBS OFF)
endif()

cpmaddpackage(
  NAME
  pcre2
  GITHUB_REPOSITORY
  PCRE2Project/pcre2
  GIT_TAG
  pcre2-10.44
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "PCRE2_BUILD_PCRE2_8 ON"
  "PCRE2_BUILD_PCRE2_16 OFF"
  "PCRE2_BUILD_PCRE2_32 OFF"
  "PCRE2_BUILD_PCRE2GREP OFF"
  "PCRE2_BUILD_TESTS OFF"
  "PCRE2_SUPPORT_JIT ON"
  "PCRE2_SUPPORT_UNICODE ON"
  "PCRE2_SUPPORT_LIBZ OFF"
  "PCRE2_SUPPORT_LIBBZ2 OFF"
  "PCRE2_SUPPORT_LIBREADLINE OFF"
  "PCRE2_SUPPORT_LIBEDIT OFF"
  "BUILD_SHARED_LIBS OFF"
  "BUILD_STATIC_LIBS ON"
)
if(pcre2_ADDED)
  message("Adding alias for pcre2")
  add_library(klogg_pcre2 INTERFACE)
  target_link_libraries(klogg_pcre2 INTERFACE pcre2-8)
else()
  add_library(klogg_pcre2 INTERFACE)
  target_link_libraries(klogg_pcre2 INTERFACE ${PCRE2_LIBRARY})
  target_include_directories(klogg_pcre2 INTERFACE ${PCRE2_INCLUDE_DIR})
endif()

set(CPM_USE_LOCAL_PACKAGES OFF)
cpmaddpackage(
  NAME
  mimalloc
  GITHUB_REPOSITORY
  microsoft/mimalloc
  VERSION
  2.1.7
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "MI_BUILD_TESTS OFF"
  "MI_SECURE OFF"
  "MI_BUILD_SHARED OFF"
  "MI_BUILD_STATIC ON"
  "MI_BUILD_OBJECT OFF"
  "MI_OVERRIDE OFF"
)
if(mimalloc_ADDED)
  message("Adding alias for mimalloc")
  add_library(klogg_mimalloc_wrapper INTERFACE)
  target_link_libraries(klogg_mimalloc_wrapper INTERFACE mimalloc-static)
  target_include_directories(klogg_mimalloc_wrapper INTERFACE ${mimalloc_SOURCE_DIR}/include)
else()
  add_library(klogg_mimalloc_wrapper INTERFACE)
  target_link_libraries(klogg_mimalloc_wrapper INTERFACE ${MIMALLOC_LIBRARY})
  target_include_directories(klogg_mimalloc_wrapper INTERFACE ${MIMALLOC_INCLUDE_DIR})
endif()

set(CPM_USE_LOCAL_PACKAGES ${_TMP_CPM_USE_LOCAL_PACKAGES})

if(KLOGG_USE_SENTRY)
  set(SENTRY_BACKEND
     "crashpad"
     CACHE INTERNAL "" FORCE
  )
  set(SENTRY_TRANSPORT
     "none"
     CACHE INTERNAL "" FORCE
  )
  set(SENTRY_BUILD_EXAMPLES
     OFF
     CACHE INTERNAL "" FORCE
  )
  set(CRASHPAD_ENABLE_INSTALL
     OFF
     CACHE INTERNAL "" FORCE
  )
  set(CRASHPAD_ENABLE_INSTALL_DEV
     OFF
     CACHE INTERNAL "" FORCE
  )

  # sentry-native 0.13.2
  cpmaddpackage(
    NAME
    sentry
    GITHUB_REPOSITORY
    getsentry/sentry-native
    GIT_TAG
    45e4a1d3f8078043eb23e3d695689a1a1fa24020
    EXCLUDE_FROM_ALL
    YES
  )

  if(WIN32)
    set_target_properties(crashpad_handler PROPERTIES OUTPUT_NAME "klogg_crashpad_handler.exe")
  else()
    set_target_properties(crashpad_handler PROPERTIES OUTPUT_NAME "klogg_crashpad_handler")
  endif()
endif(KLOGG_USE_SENTRY)

# Vendored amalgamated Perfetto SDK, used as-is: no CPM download, no patches
if(KLOGG_USE_PERFETTO)
  add_library(perfetto_sdk STATIC perfetto-sdk/perfetto.cc)
  target_include_directories(perfetto_sdk SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/perfetto-sdk)
  if(MSVC)
    # The amalgamation is a single ~3MB translation unit
    target_compile_options(perfetto_sdk PRIVATE /bigobj)
  endif()
  if(WIN32)
    # NOMINMAX: windows.h min/max macros break std::min/std::max; without
    # WIN32_LEAN_AND_MEAN windows.h pulls in winsock.h, which then clashes
    # with the amalgamation's own winsock2.h include
    target_compile_definitions(perfetto_sdk PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    # The in-process backend's UnixSocket uses Winsock; not inherited from
    # Qt::Network anymore since klogg_utils dropped it
    target_link_libraries(perfetto_sdk PUBLIC ws2_32)
  endif()
  if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(perfetto_sdk PUBLIC Threads::Threads)
  endif()
endif()

set(klogg_cpm_targets
    xxhash
    Catch2
    roaring
    roaring-headers-cpp
    libuchardet
    robin_hood
    whereami
    simdutf
    efsw
    SingleApplication
    hs
    tbb
    exprtk
    type_safe
    mimalloc-static
    sentry
    crashpad_client
    crashpad_compat
    crashpad_util
    mini_chromium
    streamvbyte
    FastPFor
    toml11
    pcre2-8-static
    nlohmann_json
)
foreach(target ${klogg_cpm_targets})
  if(TARGET ${target})
    get_property(
      ${target}_include_dirs
      TARGET ${target}
      PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    )
    #message("Marking ${${target}_include_dirs} as system incldue")
    set_property(TARGET ${target} PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${${target}_include_dirs})
  endif()
endforeach()


