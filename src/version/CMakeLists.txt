add_library(
  klogg_version STATIC ${CMAKE_CURRENT_SOURCE_DIR}/include/klogg_version.h
                       ${CMAKE_CURRENT_SOURCE_DIR}/src/klogg_version.cpp
)

target_include_directories(klogg_version PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(klogg_version PUBLIC project_options project_warnings)

# generated/version.h is written at configure time and included normally, so the
# dependency scan already covers it.
