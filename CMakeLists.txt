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
option(MYALLOC_TSAN "Build with ThreadSanitizer" OFF)

add_compile_options(-Wall -Wextra -Wpedantic)
# Sized operator delete (C++14) is on by default in GCC and in Clang >= 19, but
# not in Clang 18, where libstdc++'s <new> then omits its declarations and the
# operator new/delete tests fail to compile (ADR-015). Enable it everywhere.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-fsized-deallocation MYALLOC_HAS_SIZED_DEALLOC)
if(MYALLOC_HAS_SIZED_DEALLOC)
  add_compile_options(-fsized-deallocation)
endif()
if(MYALLOC_UBSAN)
  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=undefined)
endif()
if(MYALLOC_TSAN)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()
if(MYALLOC_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

set(MYALLOC_SOURCES src/arena.cpp src/heap.cpp src/myalloc.cpp)
# The C API takes a lock and registers fork handlers (ADR-016).
find_package(Threads REQUIRED)

# Default library: segregated fits (ADR-011) behind myalloc::Heap and the
# my_* C API.
add_library(myalloc ${MYALLOC_SOURCES})
target_include_directories(myalloc PUBLIC include PRIVATE src)
target_link_libraries(myalloc PUBLIC Threads::Threads)

# Same sources with first-fit and best-fit as the default policy (ADR-009), so
# the whole test suite, C API included, runs against every policy.
add_library(myalloc_firstfit ${MYALLOC_SOURCES})
target_include_directories(myalloc_firstfit PUBLIC include PRIVATE src)
target_compile_definitions(myalloc_firstfit PUBLIC MYALLOC_FIRST_FIT)
target_link_libraries(myalloc_firstfit PUBLIC Threads::Threads)

add_library(myalloc_bestfit ${MYALLOC_SOURCES})
target_include_directories(myalloc_bestfit PUBLIC include PRIVATE src)
target_compile_definitions(myalloc_bestfit PUBLIC MYALLOC_BEST_FIT)
target_link_libraries(myalloc_bestfit PUBLIC Threads::Threads)

# Opt-in global operator new/delete replacement (ADR-015). An OBJECT library,
# so linking it always brings in every replaced form: a static archive would
# only contribute members that resolve a symbol nothing else defines.
add_library(myalloc_new OBJECT src/new_delete.cpp)
target_include_directories(myalloc_new PUBLIC include)
target_link_libraries(myalloc_new PUBLIC myalloc)

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
  tests/test_hardening.cpp
  tests/test_tracking.cpp
  tests/test_slab.cpp
  tests/test_aligned.cpp
  tests/test_stl_allocator.cpp
  tests/test_alignment.cpp
  tests/test_stress.cpp
  tests/test_threads.cpp
  tests/test_api_integration.cpp)
include(GoogleTest)

add_executable(myalloc_tests ${MYALLOC_TEST_SOURCES})
target_link_libraries(myalloc_tests PRIVATE myalloc GTest::gtest_main)
target_include_directories(myalloc_tests PRIVATE src)
gtest_discover_tests(myalloc_tests TEST_PREFIX "segfit.")

add_executable(myalloc_tests_firstfit ${MYALLOC_TEST_SOURCES})
target_link_libraries(myalloc_tests_firstfit PRIVATE myalloc_firstfit GTest::gtest_main)
target_include_directories(myalloc_tests_firstfit PRIVATE src)
gtest_discover_tests(myalloc_tests_firstfit TEST_PREFIX "firstfit.")

add_executable(myalloc_tests_bestfit ${MYALLOC_TEST_SOURCES})
target_link_libraries(myalloc_tests_bestfit PRIVATE myalloc_bestfit GTest::gtest_main)
target_include_directories(myalloc_tests_bestfit PRIVATE src)
gtest_discover_tests(myalloc_tests_bestfit TEST_PREFIX "bestfit.")

# Targets that replace global operator new/delete. On Linux, ThreadSanitizer's
# runtime (libclang_rt.tsan_cxx) defines every form itself, non-weak, so they
# cannot link there: multiple definition (ADR-015). macOS's TSan runtime is a
# dylib that allows replacement, so the macOS TSan job still runs them.
if(MYALLOC_TSAN AND NOT APPLE)
  set(MYALLOC_REPLACES_NEW OFF)
else()
  set(MYALLOC_REPLACES_NEW ON)
endif()

if(MYALLOC_REPLACES_NEW)
  # Every allocation in this binary, GoogleTest's included, goes through the heap.
  add_executable(myalloc_new_tests tests/test_new_delete.cpp)
  target_link_libraries(myalloc_new_tests PRIVATE myalloc_new GTest::gtest_main)
  target_include_directories(myalloc_new_tests PRIVATE src)
  gtest_discover_tests(myalloc_new_tests TEST_PREFIX "newdelete.")

  add_executable(stl_demo examples/stl_demo.cpp)
  target_link_libraries(stl_demo PRIVATE myalloc_new)
endif()

add_executable(footprint_probe bench/footprint_probe.cpp)
target_link_libraries(footprint_probe PRIVATE myalloc)
target_include_directories(footprint_probe PRIVATE src)

add_executable(bench_timing bench/bench_main.cpp bench/workloads.cpp)
target_link_libraries(bench_timing PRIVATE myalloc)
target_include_directories(bench_timing PRIVATE src)

add_executable(freelist_length bench/freelist_length.cpp bench/workloads.cpp)
target_link_libraries(freelist_length PRIVATE myalloc)
target_include_directories(freelist_length PRIVATE src)

# The same container workloads under the platform allocator and under myalloc.
add_executable(stl_bench_system bench/stl_bench.cpp)
if(MYALLOC_REPLACES_NEW)
  add_executable(stl_bench_myalloc bench/stl_bench.cpp)
  target_link_libraries(stl_bench_myalloc PRIVATE myalloc_new)
  target_compile_definitions(stl_bench_myalloc PRIVATE STL_BENCH_MYALLOC)
  # Bench-only: the same workloads with new/delete backed by heap + slab (ADR-015).
  add_executable(stl_bench_slab bench/stl_bench.cpp bench/new_delete_slab.cpp)
  target_link_libraries(stl_bench_slab PRIVATE myalloc)
  target_include_directories(stl_bench_slab PRIVATE src)
  target_compile_definitions(stl_bench_slab PRIVATE STL_BENCH_SLAB)
endif()

# Multithreaded throughput and scaling (ADR-016): the shipped per-thread
# arenas, one global lock (MYALLOC_ARENAS=1), and, for the single-thread cost
# of locking only, no lock at all (not thread-safe: run it with 1 thread).
add_executable(mt_bench bench/mt_bench.cpp bench/workloads.cpp)
target_link_libraries(mt_bench PRIVATE myalloc Threads::Threads)
target_include_directories(mt_bench PRIVATE src)

add_executable(mt_bench_global bench/mt_bench.cpp bench/workloads.cpp ${MYALLOC_SOURCES})
target_include_directories(mt_bench_global PRIVATE include src)
target_compile_definitions(mt_bench_global PRIVATE MYALLOC_ARENAS=1)
target_link_libraries(mt_bench_global PRIVATE Threads::Threads)

add_executable(mt_bench_nolock bench/mt_bench.cpp bench/workloads.cpp ${MYALLOC_SOURCES})
target_include_directories(mt_bench_nolock PRIVATE include src)
target_compile_definitions(mt_bench_nolock PRIVATE MYALLOC_ARENAS=1 MYALLOC_LOCK=-1)
target_link_libraries(mt_bench_nolock PRIVATE Threads::Threads)
