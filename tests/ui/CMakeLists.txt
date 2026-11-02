set(UI_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/qtests_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logdata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logfiltereddata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/crawlerwidget_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/patterninputwidget_test.cpp
)

if(NOT APPLE)
    LIST(APPEND UI_TEST_SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow_test.cpp)
    LIST(APPEND UI_TEST_SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/screenshot_test.cpp)
    LIST(APPEND UI_TEST_SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/perf_repro_test.cpp)
endif()

add_executable(klogg_itests ${UI_TEST_SOURCES} ${BACKWARD_ENABLE})

target_link_libraries(klogg_itests klogg_ui klogg_utils Catch2 Qt${QT_VERSION_MAJOR}::Test test_utils)
set_target_properties(klogg_itests PROPERTIES AUTOMOC ON)

# uic-generated ui_*.h live in the klogg_ui autogen dir, which is private to it
target_include_directories(klogg_itests
                           PRIVATE ${CMAKE_BINARY_DIR}/src/shell/qt_legacy/ui/klogg_ui_autogen/include)

# KLOGG_USE_PERFETTO propagates from klogg_ui publicly, so uitracing.h pulls
# src/api/src/tracing.h here too
if(KLOGG_USE_PERFETTO)
    target_include_directories(klogg_itests PRIVATE "${CMAKE_SOURCE_DIR}/src/core/api/src")
endif()

add_backward(klogg_itests)

add_test(
    NAME klogg_itests
    COMMAND klogg_itests -platform offscreen
)