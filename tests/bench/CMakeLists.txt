add_executable(klogg_engine_bench
    engine_bench.cpp
)

target_link_libraries(klogg_engine_bench klogg_logdata klogg_settings klogg_api Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Test)
set_target_properties(klogg_engine_bench PROPERTIES AUTOMOC ON)

add_executable(ansi_parser_bench
    ansi_parser_bench.cpp
)

target_link_libraries(ansi_parser_bench klogg_ansi klogg_utils Qt${QT_VERSION_MAJOR}::Core)
set_target_properties(ansi_parser_bench PROPERTIES AUTOMOC ON)

add_executable(quickfind_bench
    quickfind_bench.cpp
)

target_link_libraries(quickfind_bench klogg_ui klogg_ansi klogg_api klogg_logdata klogg_settings klogg_utils klogg_logging Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)
set_target_properties(quickfind_bench PROPERTIES AUTOMOC ON)

# The benchmarks are not part of the test run, but compiling them through
# ci_build keeps them from rotting unnoticed.
if(TARGET ci_build)
  add_dependencies(ci_build klogg_engine_bench ansi_parser_bench quickfind_bench)
endif()
