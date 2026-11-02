add_executable(klogg_utils_tests
    test_main.cpp
    test_crc32.cpp
    test_executor.cpp
    test_filehandle.cpp
    test_string_utils.cpp
    test_tempdir.cpp
    test_throttler.cpp
    test_uuid.cpp
)

target_link_libraries(klogg_utils_tests klogg_utils Catch2)

add_test(
    NAME klogg_utils_tests
    COMMAND klogg_utils_tests
)

if(TARGET ci_build)
  add_dependencies(ci_build klogg_utils_tests)
endif()
