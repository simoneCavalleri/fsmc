# ==============================================================================
# Benchmarks CMakeLists.txt (Google Benchmark + Standalone Footprint)
# ==============================================================================

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

# 1. Google Benchmark Suite & Zero-Overhead Verification Target
add_executable(fsmc_bench benchmarks.cpp)
add_executable(benchmark_dispatch ALIAS fsmc_bench)
target_link_libraries(fsmc_bench PRIVATE fsmc_runtime benchmark::benchmark)
set_target_properties(fsmc_bench PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# Apply optimization flags
if(MSVC)
    target_compile_options(fsmc_bench PRIVATE /O2)
else()
    target_compile_options(fsmc_bench PRIVATE -O3)
endif()
