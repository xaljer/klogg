add_executable(
  klogg_ui_tests
  test_main.cpp
  quickfind_test.cpp
  quickfindengine_test.cpp
  quickfindmatcher_test.cpp
  highlightersstore_test.cpp
  logtypesstore_test.cpp
  recorder_test.cpp
)

set_target_properties(klogg_ui_tests PROPERTIES AUTOMOC ON)

target_link_libraries(
  klogg_ui_tests
  klogg_ui
  klogg_utils
  Catch2
  Qt${QT_VERSION_MAJOR}::Widgets
)

add_test(NAME klogg_ui_tests COMMAND klogg_ui_tests -platform offscreen)
