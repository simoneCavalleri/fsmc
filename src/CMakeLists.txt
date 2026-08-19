# Primary CLI executable target
add_executable(fsmc main.cpp)
target_link_libraries(fsmc PRIVATE Threads::Threads)

# Backward-compatible alias binary fsm-gen
add_executable(fsm-gen main.cpp)
target_link_libraries(fsm-gen PRIVATE Threads::Threads)

# Install and export targets
install(TARGETS fsmc fsm-gen
    EXPORT fsmcTargets
    RUNTIME DESTINATION bin
)
