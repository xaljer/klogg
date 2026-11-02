add_executable(file_write_helper
        file_write_helper.cpp
        file_write_helper.h
        persistentinfo_test_stub.cpp
)

target_link_libraries(file_write_helper klogg_utils klogg_settings tbb)

add_executable(log_generator
        log_generator.cpp
)

add_library(test_utils INTERFACE)
target_include_directories(test_utils SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
