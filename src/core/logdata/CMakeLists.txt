set(
  KLOGG_LOGDATA_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/include/abstractlogdata.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/compressedlinestorage.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/encoding.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/encodingdetector.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/linepositionarray.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/loadingstatus.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logdata.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logdataoperation.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logdataworker.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logfiltereddata.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logfiltereddataworker.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/linetypes.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/fileholder.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/filedigest.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/readablesize.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/abstractlogdata.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/compressedlinestorage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/encoding.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/encodingdetector.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logdata.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logdataoperation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logdataworker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logfiltereddata.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logfiltereddataworker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/fileholder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/filedigest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/readablesize.cpp
)

if(WIN32)
  list(APPEND KLOGG_LOGDATA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/encoding_win.cpp)
  list(APPEND KLOGG_LOGDATA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/fileholder_win.cpp)
else()
  list(APPEND KLOGG_LOGDATA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/encoding_unix.cpp)
  list(APPEND KLOGG_LOGDATA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/fileholder_unix.cpp)
endif()

add_library(
  klogg_logdata STATIC
  ${KLOGG_LOGDATA_SOURCES}
)

# encoding_unix.cpp talks to iconv directly. glibc ships it, macOS keeps it in a
# separate libiconv, so let CMake pick the right spelling for the platform.
if(NOT WIN32)
  find_package(Iconv)
  if(Iconv_FOUND)
    target_link_libraries(klogg_logdata PRIVATE Iconv::Iconv)
  endif()
endif()

target_include_directories(klogg_logdata PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(klogg_logdata PUBLIC "-DQT_NO_KEYWORDS")

target_link_libraries(
  klogg_logdata
  PUBLIC project_options
         project_warnings
         klogg_engine_options
         klogg_utils
         platform
         klogg_regex
         klogg_filewatch
         roaring
         roaring-headers-cpp
         roaring-headers
         tbb
         type_safe
         klogg_uchardet_wrapper
         robin_hood
         simdutf
         streamvbyte
         klogg_mimalloc_wrapper
)

target_link_libraries(klogg_logdata PRIVATE xxhash)

if(KLOGG_USE_LTO)
  set_property(TARGET klogg_logdata PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(KLOGG_BUILD_TESTS)
  add_subdirectory(tests)
endif()
