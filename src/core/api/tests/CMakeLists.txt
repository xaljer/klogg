# Links only klogg_api; used to verify the api link line contains no Qt library
# (see execution-plan D1d). Kept outside the GUI gate so a core-only tree still
# builds and runs the zero-Qt probe.
add_executable(api_link_probe api_link_probe.cpp)
target_link_libraries(api_link_probe PRIVATE klogg_api)
add_test(NAME api_link_probe COMMAND api_link_probe)

if(TARGET ci_build)
  add_dependencies(ci_build api_link_probe)
endif()

# The api library itself is Qt-free, but this suite drives it through a
# QGuiApplication host and the Qt-backed settings documents, so it only builds
# on the Qt line. The zero-Qt line runs the Qt-free engine suites instead.
if(NOT KLOGG_BUILD_SERVER_ONLY)
  add_executable(klogg_api_tests
      api_tests_main.cpp
      test_api_concurrency.cpp
      test_api_engine.cpp
      test_api_engine2.cpp
      test_api_events.cpp
      test_api_projection.cpp
      test_api_scrollmap.cpp
      test_api_state.cpp
      test_api_viewport.cpp
      test_linetypes_no_qt.cpp
    )

  # klogg_settings is the Qt side of the userdata documents (PersistentInfo /
  # Configuration); the api library itself reaches them through the Qt-free
  # platform::userdata registry
  target_link_libraries(klogg_api_tests klogg_api Catch2 Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui
                        klogg_crash_handler klogg_settings platform Qt${QT_VERSION_MAJOR}::Widgets klogg_qt_codec)

  add_test(
      NAME klogg_api_tests
      COMMAND klogg_api_tests
  )

  if(TARGET ci_build)
    add_dependencies(ci_build klogg_api_tests)
  endif()
endif()
