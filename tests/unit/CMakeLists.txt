# Add test cpp file
add_executable(klogg_tests
    ansi_parser_test.cpp
    linepositionarray_test.cpp
    patternmatcher_test.cpp
    recorder_test.cpp
    wrappedstring_test.cpp
    tempdir_test.cpp
    tests_main.cpp
)

target_link_libraries(klogg_tests klogg_ui klogg_utils klogg_logging Catch2 Qt${QT_VERSION_MAJOR}::Test)
set_target_properties(klogg_tests PROPERTIES AUTOMOC ON)

add_test(
    NAME klogg_tests
    COMMAND klogg_tests -platform offscreen
)

add_executable(ansi_parser_bench
    ansi_parser_bench.cpp
)

target_link_libraries(ansi_parser_bench klogg_ansi klogg_utils klogg_settings Qt${QT_VERSION_MAJOR}::Core)
set_target_properties(ansi_parser_bench PROPERTIES AUTOMOC ON)