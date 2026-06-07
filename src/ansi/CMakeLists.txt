add_library(
  klogg_ansi STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ansi_parser.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/ansi_parser.h
)
target_include_directories(klogg_ansi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  klogg_ansi
  PUBLIC project_options
         project_warnings
         klogg_settings
         klogg_utils
         Qt${QT_VERSION_MAJOR}::Core
)

if(KLOGG_USE_LTO)
  set_property(TARGET klogg_ansi PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
