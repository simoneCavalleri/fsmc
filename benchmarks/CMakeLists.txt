# ==============================================================================
# Benchmarks CMakeLists.txt
# ==============================================================================

add_executable(benchmark_dispatch benchmark_dispatch.cpp)
target_link_libraries(benchmark_dispatch PRIVATE Threads::Threads)
set_target_properties(benchmark_dispatch PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# Optimize for release benchmarking if not set
if(NOT CMAKE_BUILD_TYPE)
    if(MSVC)
        target_compile_options(benchmark_dispatch PRIVATE /O2)
    else()
        target_compile_options(benchmark_dispatch PRIVATE -O3)
    endif()
endif()

