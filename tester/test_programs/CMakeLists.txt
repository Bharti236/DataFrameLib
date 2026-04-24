cmake_minimum_required(VERSION 3.16)
project(DataFrameLibTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Student project path passed via -DSTUDENT_DIR=...
if(NOT DEFINED STUDENT_DIR)
    message(FATAL_ERROR "STUDENT_DIR must be set to the student's project root")
endif()

add_subdirectory(${STUDENT_DIR} ${CMAKE_BINARY_DIR}/student_build)

set(TEST_PROGRAMS
    test_io
    test_select_filter
    test_groupby_agg
    test_join
    test_expressions
    test_string_ops
    test_sort_head
    test_with_column
    test_lazy
    test_null_types
    test_edge_cases
    test_operators
    test_advanced_join
    test_chained_ops
    test_advanced_lazy
    test_performance
    test_from_columns
)

foreach(test ${TEST_PROGRAMS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE dataframelib)
endforeach()
