cmake_minimum_required(VERSION 3.20)
project(latency_gate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Off by default so CI binaries do not depend on the build machine's CPU.
option(LG_NATIVE "Compile with -march=native" OFF)

add_compile_options(-Wall -Wextra -Wpedantic)
if(LG_NATIVE)
  add_compile_options(-march=native)
endif()

add_library(lg INTERFACE)
target_include_directories(lg INTERFACE include)

foreach(tool gen replay gate)
  add_executable(lg-${tool} src/${tool}.cpp)
  target_link_libraries(lg-${tool} PRIVATE lg)
endforeach()

add_executable(lg-bench bench/bench.cpp)
target_link_libraries(lg-bench PRIVATE lg)

enable_testing()
add_executable(lg-tests tests/test_main.cpp)
target_link_libraries(lg-tests PRIVATE lg)
add_test(NAME lg-tests COMMAND lg-tests)
