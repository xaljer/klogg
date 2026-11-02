add_library(
  klogg_compression STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/compression.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/archive_format.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/decompress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/extract.cpp
)

target_include_directories(klogg_compression PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(TARGET archive_static)
  set(klogg_archive_target archive_static)
else()
  set(klogg_archive_target archive)
endif()

target_link_libraries(
  klogg_compression
  PUBLIC project_options project_warnings platform
  PRIVATE ${klogg_archive_target}
)

# libarchive exposes dllimport declarations unless the static build is declared.
target_compile_definitions(klogg_compression PRIVATE LIBARCHIVE_STATIC)

if(KLOGG_USE_LTO)
  set_property(TARGET klogg_compression PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(KLOGG_BUILD_TESTS)
  add_subdirectory(tests)
endif()
