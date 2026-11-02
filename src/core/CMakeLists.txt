# Zero-Qt core: engine + shared state + the Core API. Nothing here may include
# Qt headers or link a Qt library; core_link_probe verifies the link line.

# Pure engine value types (EngineOptions / EngineOptionsHolder): zero-Qt and
# header-only. Engine modules link this instead of the retired coreconfig.
add_library(klogg_engine_options INTERFACE)
target_include_directories(klogg_engine_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(ansi)
add_subdirectory(filewatch)
add_subdirectory(logdata)
add_subdirectory(regex)
add_subdirectory(render)
add_subdirectory(state)
add_subdirectory(api)

# The whole zero-Qt core, including the platform infrastructure it builds on.
# Consumers link this; core_link_probe links it to prove the link line carries
# no Qt.
add_library(klogg_core INTERFACE)
target_link_libraries(
  klogg_core
  INTERFACE platform
            klogg_engine_options
            klogg_utils
            klogg_logging
            klogg_regex
            klogg_logdata
            klogg_filewatch
            klogg_ansi
            klogg_state
            klogg_render
            klogg_api
)

# `--target core` builds only the engine + Core API (and the platform
# infrastructure they need), never the Qt GUI. Incremental rebuilds are the
# normal Ninja behavior: changing core recompiles core and only relinks the GUI.
add_custom_target(core)
add_dependencies(core klogg_api)
