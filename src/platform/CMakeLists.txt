add_library(
  platform STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/settings.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/userdata.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/paths.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/platform_log.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/settings.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/userdata.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/paths.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/platform_log.cpp
)

target_include_directories(platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(platform PUBLIC project_options project_warnings toml11 whereami)

if(KLOGG_USE_LTO)
  set_property(TARGET platform PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(KLOGG_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# Shared mechanism built on top of the core platform layer: the log sink and
# the general-purpose utilities.
add_subdirectory(log)
add_subdirectory(utils)

# Zero-Qt archive detection, decompression and extraction.
add_subdirectory(compression)
