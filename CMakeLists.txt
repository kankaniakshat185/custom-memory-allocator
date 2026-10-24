cmake_minimum_required(VERSION 3.20)

# On Apple Silicon, build native arm64 even if CMake itself is an x86_64 binary
# running under Rosetta (it then reports an x86_64 host and would build x86_64,
# which silently benchmarks under translation; see docs/environment.md).
# hw.optional.arm64 is 1 on Apple Silicon even when queried under Rosetta.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  execute_process(COMMAND sysctl -n hw.optional.arm64
                  OUTPUT_VARIABLE _apple_silicon OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(_apple_silicon STREQUAL "1")
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architecture" FORCE)
  endif()
endif()

project(custom_memory_allocator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Test builds: cmake -B build-test -DCMAKE_BUILD_TYPE=Debug
# Benchmark builds: default Release (-O2 -DNDEBUG), no sanitizers.
option(MYALLOC_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)
# ASan needs Xcode's clang; the Command Line Tools' clang 17 hung (docs/environment.md).
option(MYALLOC_ASAN "Build with AddressSanitizer" OFF)

add_compile_options(-Wall -Wextra -Wpedantic)
if(MYALLOC_UBSAN)
  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=undefined)
endif()
if(MYALLOC_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

set(MYALLOC_SOURCES src/arena.cpp src/heap.cpp src/myalloc.cpp)

# Default library: first-fit behind myalloc::Heap and the my_* C API.
add_library(myalloc ${MYALLOC_SOURCES})
target_include_directories(myalloc PUBLIC include PRIVATE src)

# Same sources with best-fit as the default policy (ADR-009), so the whole test
# suite, C API included, also runs against best-fit.
add_library(myalloc_bestfit ${MYALLOC_SOURCES})
target_include_directories(myalloc_bestfit PUBLIC include PRIVATE src)
target_compile_definitions(myalloc_bestfit PUBLIC MYALLOC_BEST_FIT)

enable_testing()
include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(MYALLOC_TEST_SOURCES
  tests/test_arena.cpp
  tests/test_heap_unit.cpp
  tests/test_split.cpp
  tests/test_coalesce.cpp
  tests/test_realloc.cpp
  tests/test_fit_policy.cpp
  tests/test_alignment.cpp
  tests/test_stress.cpp
  tests/test_api_integration.cpp)
include(GoogleTest)

add_executable(myalloc_tests ${MYALLOC_TEST_SOURCES})
target_link_libraries(myalloc_tests PRIVATE myalloc GTest::gtest_main)
target_include_directories(myalloc_tests PRIVATE src)
gtest_discover_tests(myalloc_tests TEST_PREFIX "firstfit.")

add_executable(myalloc_tests_bestfit ${MYALLOC_TEST_SOURCES})
target_link_libraries(myalloc_tests_bestfit PRIVATE myalloc_bestfit GTest::gtest_main)
target_include_directories(myalloc_tests_bestfit PRIVATE src)
gtest_discover_tests(myalloc_tests_bestfit TEST_PREFIX "bestfit.")

add_executable(footprint_probe bench/footprint_probe.cpp)
target_link_libraries(footprint_probe PRIVATE myalloc)
target_include_directories(footprint_probe PRIVATE src)

add_executable(bench_timing bench/bench_main.cpp bench/workloads.cpp)
target_link_libraries(bench_timing PRIVATE myalloc)
target_include_directories(bench_timing PRIVATE src)
