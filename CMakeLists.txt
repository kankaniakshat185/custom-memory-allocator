cmake_minimum_required(VERSION 3.20)
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
# ASan currently hangs at startup on this machine (docs/environment.md).
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

add_library(myalloc
  src/arena.cpp
  src/heap.cpp
  src/myalloc.cpp
)
target_include_directories(myalloc PUBLIC include PRIVATE src)

enable_testing()
include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(myalloc_tests
  tests/test_arena.cpp
  tests/test_heap_unit.cpp
  tests/test_alignment.cpp
  tests/test_stress.cpp
  tests/test_api_integration.cpp)
target_link_libraries(myalloc_tests PRIVATE myalloc GTest::gtest_main)
target_include_directories(myalloc_tests PRIVATE src)
include(GoogleTest)
gtest_discover_tests(myalloc_tests)
