# ==============================================================================
# Benchmarks CMakeLists.txt (Google Benchmark + Standalone Footprint)
# ==============================================================================

# Try to find installed benchmark package first (offline/system builds), fallback to FetchContent
find_package(benchmark CONFIG QUIET)

if(NOT benchmark_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googlebenchmark)
endif()

# 1. Google Benchmark Suite & Zero-Overhead Verification Target
add_executable(fsmc_bench benchmarks.cpp)
add_executable(benchmark_dispatch ALIAS fsmc_bench)
target_link_libraries(fsmc_bench PRIVATE fsmc_compiler benchmark::benchmark Threads::Threads)
set_target_properties(fsmc_bench PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# Apply optimization flags
if(MSVC)
    target_compile_options(fsmc_bench PRIVATE /O2)
else()
    target_compile_options(fsmc_bench PRIVATE -O3)
endif()

# Register Zero-Heap-Allocation quality gate in CTest
if(FSMC_ENABLE_TESTING)
    add_test(NAME test_zero_alloc_benchmark_gate
        COMMAND fsmc_bench --benchmark_filter=BM_Dispatch_.* --benchmark_min_time=0.01s
    )
endif()
