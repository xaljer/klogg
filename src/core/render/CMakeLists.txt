add_library(
  klogg_render STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/wrap.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/wrap.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/highlight.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/highlight.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/layout.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/layout.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/quickfindmatch.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/quickfindmatch.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/frame.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/frame.cpp
)

target_include_directories(klogg_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(
  klogg_render
  PUBLIC project_options
         project_warnings
         klogg_logdata # linetypes.h
         klogg_utils
         klogg_state # HighlighterSet, HighlightedMatchRanges
         klogg_ansi # AnsiSegment, AnsiProcessing
         klogg_regex # Pcre2Regex (inline QuickFind matching)
         simdutf
)

if(KLOGG_USE_LTO)
  set_property(TARGET klogg_render PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(KLOGG_BUILD_TESTS)
  add_subdirectory(tests)
endif()
