add_executable(klogg_server_tests test_server.cpp)
target_link_libraries(
  klogg_server_tests
  PRIVATE klogg_server_core
          Catch2
          nlohmann_json
          klogg_httplib
)
add_test(NAME klogg_server_tests COMMAND klogg_server_tests)

if(TARGET ci_build)
  add_dependencies(ci_build klogg_server_tests)
endif()
