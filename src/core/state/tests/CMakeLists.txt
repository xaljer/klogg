add_executable(
  klogg_state_tests
  test_main.cpp
  eventvisualization_manager_test.cpp
  eventvisualization_export_test.cpp
  eventvisualization_integration_test.cpp
)

target_link_libraries(
  klogg_state_tests
  klogg_state
  klogg_logdata
  klogg_regex
  klogg_logging
  Catch2
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Gui
)

# Event visualization is not shipped yet, so the suite is compiled but its ctest
# entry stays disabled: a failing suite would only point at a feature nobody can
# use. Clear the DISABLED property together with shipping the feature.
add_test(NAME klogg_state_tests COMMAND klogg_state_tests)
set_tests_properties(klogg_state_tests PROPERTIES DISABLED TRUE)

if(TARGET ci_build)
  add_dependencies(ci_build klogg_state_tests)
endif()
