add_library(
  klogg_regex STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/hsregularexpression.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/regularexpression.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/booleanevaluator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pcre2regex.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/regularexpressionpattern.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/regularexpression.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/hsregularexpression.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/booleanevaluator.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pcre2regex.h
)
target_include_directories(klogg_regex PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  klogg_regex
  PUBLIC project_options
         project_warnings
         klogg_logging
         klogg_engine_options
         klogg_utils
         klogg_pcre2
         robin_hood
         exprtk
)

if(KLOGG_USE_HYPERSCAN)
  target_link_libraries(klogg_regex PUBLIC klogg_hyperscan)
  target_compile_definitions(klogg_regex PUBLIC KLOGG_HAS_HS)
elseif(KLOGG_USE_VECTORSCAN)
  target_link_libraries(klogg_regex PUBLIC klogg_vectorscan)
  target_compile_definitions(klogg_regex PUBLIC KLOGG_HAS_HS)
endif()

if(KLOGG_USE_LTO)
  set_property(TARGET klogg_regex PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(KLOGG_BUILD_TESTS)
  add_subdirectory(tests)
endif()
