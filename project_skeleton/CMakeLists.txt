cmake_minimum_required(VERSION 3.15)
project(amp_project CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(OpenMP REQUIRED)

enable_testing()

# C Queue test (from Ex1.c)
add_executable(test_ex1_queue tests/test_ex1_queue.c src/Ex1.c)
target_include_directories(test_ex1_queue PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME ex1_queue_test COMMAND test_ex1_queue)

# C Concurrent test (from Ex2.c)
add_executable(test_ex2_concurrent tests/test_ex2_concurrent.c src/Ex2.c)
target_include_directories(test_ex2_concurrent PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_ex2_concurrent PRIVATE OpenMP::OpenMP_C)

add_test(NAME ex2_concurrent_test COMMAND test_ex2_concurrent)

# C Two Locks test (from Ex4.c)
add_executable(test_ex4_twolocks tests/test_ex4_twolocks.c src/Ex4.c)
target_include_directories(test_ex4_twolocks PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_ex4_twolocks PRIVATE OpenMP::OpenMP_C)

add_test(NAME ex4_twolocks_test COMMAND test_ex4_twolocks)

# C Lock Free test (from Ex5.c)
add_executable(test_ex5_lockfree tests/test_ex5_lockfree.c src/Ex5.c)
target_include_directories(test_ex5_lockfree PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_ex5_lockfree PRIVATE OpenMP::OpenMP_C)

add_test(NAME ex5_lockfree_test COMMAND test_ex5_lockfree)

# Benchmark executables
add_executable(benchmark_ex1 tests/bench.c src/Ex1.c)
target_include_directories(benchmark_ex1 PRIVATE src)
target_compile_definitions(benchmark_ex1 PRIVATE VERSION=1 DEBUG CORRECT_CHECK)
target_link_libraries(benchmark_ex1 PUBLIC OpenMP::OpenMP_C)

#add_custom_command(
#     OUTPUT run_benchmark_ex1
#     COMMAND $<TARGET_FILE:benchmark_ex1> 1 2 2 "10" "10" "benchmark_ex1_output.csv"
#     DEPENDS benchmark_ex1
#     COMMENT "Running benchmark_ex1 with default parameters"
# )

#add_custom_target(run_benchmark_ex1 ALL DEPENDS run_benchmark_ex1)

add_executable(benchmark_ex2 tests/bench.c src/Ex2.c)
target_include_directories(benchmark_ex2 PRIVATE src)
target_compile_definitions(benchmark_ex2 PRIVATE VERSION=2 DEBUG CORRECT_CHECK)
target_link_libraries(benchmark_ex2 PUBLIC OpenMP::OpenMP_C)

add_executable(benchmark_ex4 tests/bench.c src/Ex4.c)
target_include_directories(benchmark_ex4 PRIVATE src)
target_compile_definitions(benchmark_ex4 PRIVATE VERSION=4 DEBUG CORRECT_CHECK)
target_link_libraries(benchmark_ex4 PUBLIC OpenMP::OpenMP_C)

add_executable(benchmark_ex5 tests/bench.c src/Ex5.c)
target_include_directories(benchmark_ex5 PRIVATE src)
target_compile_definitions(benchmark_ex5 PRIVATE VERSION=5 DEBUG CORRECT_CHECK)
target_link_libraries(benchmark_ex5 PUBLIC OpenMP::OpenMP_C)
