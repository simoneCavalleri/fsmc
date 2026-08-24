# ==============================================================================
# CLI Compiler and IR Toolchain Targets
# ==============================================================================

# Primary compiler driver (fsmc)
add_executable(fsmc fsmc/main.cpp)
target_link_libraries(fsmc PRIVATE fsmc_runtime)
target_compile_features(fsmc PRIVATE cxx_std_20)

# Formal IR Optimizer and Linter tool (fsm-opt)
add_executable(fsm-opt fsm-opt/main.cpp)
target_link_libraries(fsm-opt PRIVATE fsmc_runtime)
target_compile_features(fsm-opt PRIVATE cxx_std_20)

# Install and export targets
install(TARGETS fsmc fsm-opt
    EXPORT fsmcTargets
    RUNTIME DESTINATION bin
)
