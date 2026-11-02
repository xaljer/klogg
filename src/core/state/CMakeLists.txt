add_library(
  klogg_state STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/encodings.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/event_mapping.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/eventvisualization.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/highlighterset.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/highlightedmatch.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/eventvisualization.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/highlighterset.cpp
)
target_include_directories(klogg_state PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  klogg_state
  PUBLIC project_options
         project_warnings
         platform
         klogg_utils
         klogg_logging
         klogg_regex
         klogg_logdata
         nlohmann_json
)

if(KLOGG_USE_LTO)
  set_property(TARGET klogg_state PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# The event visualization suite still uses Qt string/JSON types and
# QRegularExpression as its reference model, so it stays on the Qt line until
# that reference is replaced.
if(KLOGG_BUILD_TESTS AND NOT KLOGG_BUILD_SERVER_ONLY)
  add_subdirectory(tests)
endif()
