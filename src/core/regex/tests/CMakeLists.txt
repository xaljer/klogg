add_executable(klogg_regex_tests test_main.cpp patternmatcher_test.cpp pcre2regex_test.cpp)

target_link_libraries(klogg_regex_tests klogg_regex klogg_logging Catch2)

add_test(NAME klogg_regex_tests COMMAND klogg_regex_tests)

if(TARGET ci_build)
  add_dependencies(ci_build klogg_regex_tests)
endif()

# Differential tests compare the wrapper against QRegularExpression as an
# independent reference, which only exists on the Qt line.
if(NOT KLOGG_BUILD_SERVER_ONLY)
  target_sources(klogg_regex_tests PRIVATE qt_parity_test.cpp)
  target_link_libraries(klogg_regex_tests Qt${QT_VERSION_MAJOR}::Core)
endif()
