add_executable(platform_tests test_main.cpp test_settings.cpp test_paths.cpp test_log.cpp test_userdata.cpp)

target_link_libraries(platform_tests platform Catch2)

add_test(NAME platform_tests COMMAND platform_tests)

# Links only klogg_core; used by the C6 accepted build to verify the core link
# line contains no Qt library.
add_executable(core_link_probe core_link_probe.cpp)
target_link_libraries(core_link_probe PRIVATE klogg_core)
add_test(NAME core_link_probe COMMAND core_link_probe)

# Keep the zero-Qt suites compiled by CI. Verify the core link line has no
# Qt library (see execution-plan C6) when touching core dependencies.
if(TARGET ci_build)
  add_dependencies(ci_build core_link_probe platform_tests)
endif()
