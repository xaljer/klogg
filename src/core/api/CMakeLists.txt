add_library(
  klogg_api STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/api_session.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/session_impl.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/session_impl.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tracing.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tracing.h
)
set_target_properties(klogg_api PROPERTIES AUTOMOC OFF)
target_include_directories(klogg_api PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  klogg_api
  PUBLIC project_options
         project_warnings
         klogg_logdata
         klogg_ansi
         klogg_state
         klogg_render # viewport composition (renderViewport)
         klogg_version
         tl::expected
  # Qt Widgets, the crash handler and klogg_settings left this target with the
  # Qt-free Core API work (D1a-D1d); the shared state documents now come from
  # the Qt-free platform registry (platform::userdata), so nothing here pulls Qt
  # into consumers
  PRIVATE platform
)

if(KLOGG_USE_PERFETTO)
  target_link_libraries(klogg_api PRIVATE perfetto_sdk)
  target_compile_definitions(klogg_api PRIVATE KLOGG_USE_PERFETTO)
endif()

if(KLOGG_USE_LTO)
  set_property(TARGET klogg_api PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(KLOGG_BUILD_TESTS)
  add_subdirectory(tests)
endif()
