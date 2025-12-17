cmake_minimum_required(VERSION 3.15)
project(amp_project CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/inc)

enable_testing()

# C Queue test (from Ex1.c)
add_executable(test_ex1_queue tests/test_ex1_queue.c)
target_include_directories(test_ex1_queue PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/inc)

add_test(NAME ex1_queue_test COMMAND test_ex1_queue)
