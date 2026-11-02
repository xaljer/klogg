add_executable(klogg_compression_tests test_main.cpp test_compression.cpp)

target_link_libraries(
  klogg_compression_tests
  klogg_compression
  klogg_zlib
  klogg_bzip2
  klogg_liblzma
  Catch2
)

target_compile_definitions(
  klogg_compression_tests
  PRIVATE KLOGG_COMPRESSION_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

add_test(NAME klogg_compression_tests COMMAND klogg_compression_tests)

if(TARGET ci_build)
  add_dependencies(ci_build klogg_compression_tests)
endif()
