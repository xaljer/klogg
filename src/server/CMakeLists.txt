# Zero-Qt HTTP endpoint over the Core API. The library never owns the Session:
# the CLI host creates one and the GUI host passes the session it already uses.
add_library(
  klogg_server_core STATIC
  clients/client_registry.cpp
  clients/client_registry.h
  http/server.cpp
  json/json_convert.cpp
  json/json_convert.h
  recorder/recorder_config.cpp
  recorder/recorder_config.h
  recorder/recorder_manager.cpp
  recorder/recorder_manager.h
  recorder/recorder_process.h
  rpc/rpc_dispatcher.cpp
  rpc/rpc_dispatcher.h
  sse/event_hub.cpp
  sse/event_hub.h
)

if(WIN32)
  target_sources(klogg_server_core PRIVATE recorder/recorder_process_win.cpp)
else()
  target_sources(klogg_server_core PRIVATE recorder/recorder_process_unix.cpp)
endif()

# Sources use flat includes ("event_hub.h"), so every module directory is on the
# private include path instead of using relative paths.
target_include_directories(
  klogg_server_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/clients" "${CMAKE_CURRENT_SOURCE_DIR}/http"
          "${CMAKE_CURRENT_SOURCE_DIR}/json" "${CMAKE_CURRENT_SOURCE_DIR}/recorder"
          "${CMAKE_CURRENT_SOURCE_DIR}/rpc" "${CMAKE_CURRENT_SOURCE_DIR}/sse"
)
target_link_libraries(
  klogg_server_core
  PUBLIC project_options
         project_warnings
         klogg_api
  PRIVATE nlohmann_json
          klogg_httplib
          klogg_compression
          platform
)

# Temporary standalone host; the product CLI will host the same core later.
add_executable(klogg_server main.cpp)
target_link_libraries(klogg_server PRIVATE klogg_server_core)

if(WIN32 AND TARGET tbb)
  # TBB comes in through klogg_logdata as a shared library; without the DLL next
  # to the executable Windows refuses to start it (STATUS_DLL_NOT_FOUND).
  add_custom_command(
    TARGET klogg_server
    POST_BUILD
    COMMAND cmake -E copy_if_different $<TARGET_FILE:tbb> $<TARGET_FILE_DIR:klogg_server>
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Copy tbb"
  )
endif()

if(TARGET ci_build)
  add_dependencies(ci_build klogg_server)
endif()

if(KLOGG_BUILD_TESTS)
  add_subdirectory(tests)
endif()
