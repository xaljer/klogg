add_executable(klogg_ansi_tests test_main.cpp ansi_parser_test.cpp)

target_link_libraries(klogg_ansi_tests klogg_ansi Catch2)

add_test(NAME klogg_ansi_tests COMMAND klogg_ansi_tests)

if(TARGET ci_build)
  add_dependencies(ci_build klogg_ansi_tests)
endif()
