add_executable(klogg_logdata_tests test_main.cpp linepositionarray_test.cpp logfiltereddata_event_test.cpp)

target_link_libraries(
  klogg_logdata_tests klogg_logdata klogg_regex klogg_logging Catch2
)

add_test(NAME klogg_logdata_tests COMMAND klogg_logdata_tests)

if(TARGET ci_build)
  add_dependencies(ci_build klogg_logdata_tests)
endif()
