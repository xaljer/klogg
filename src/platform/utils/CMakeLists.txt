if(WIN32)
  set(klogg_utils_filehandle_impl ${CMAKE_CURRENT_SOURCE_DIR}/src/filehandle_win.cpp)
else()
  set(klogg_utils_filehandle_impl ${CMAKE_CURRENT_SOURCE_DIR}/src/filehandle_unix.cpp)
endif()

add_library(
  klogg_utils
  ${CMAKE_CURRENT_SOURCE_DIR}/include/atomicflag.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/uuid.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/perfcounter.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/progress.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/synchronization.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/overload_visitor.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/resourcewrapper.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/crc32.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/cpu_info.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tempdir.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/filehandle.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/string_utils.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/posttarget.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/color.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/executor.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/throttler.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/cpu_info.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tempdir.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/executor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/throttler.cpp
  ${klogg_utils_filehandle_impl}
)

target_include_directories(klogg_utils PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  klogg_utils
  PUBLIC project_options
         project_warnings
         platform
         klogg_version
         klogg_logging
         whereami
         type_safe
         klogg_mimalloc_wrapper
)

if(KLOGG_BUILD_TESTS)
  add_subdirectory(tests)
endif()
