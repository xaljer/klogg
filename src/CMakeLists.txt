add_subdirectory(version)

# L1: zero-Qt mechanism layer. Owns paths/settings/userdata, the log sink and
# the shared utility helpers.
add_subdirectory(platform)

# L2: zero-Qt engine + Core API. Defines klogg_core and the `core` target.
add_subdirectory(core)

# L3: zero-Qt HTTP/SSE endpoint over the Core API (klogg_server_core + the
# temporary klogg_server host).
add_subdirectory(server)

# L4: product shells. The Qt shell is maintenance mode (qt_legacy); the web and
# electron shells are npm projects built outside CMake.
if(NOT KLOGG_BUILD_SERVER_ONLY)
  add_subdirectory(shell)
endif()
