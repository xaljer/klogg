add_executable(render_tests
  test_main.cpp
  test_wrap.cpp
  test_highlight.cpp
  test_layout.cpp
  test_quickfindmatch.cpp
  test_frame.cpp
)

target_link_libraries(render_tests klogg_render Catch2)

add_test(NAME render_tests COMMAND render_tests)

if(TARGET ci_build)
  add_dependencies(ci_build render_tests)
endif()
