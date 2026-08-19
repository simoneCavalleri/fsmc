# Primary CLI executable target
add_executable(fsmc main.cpp)
target_link_libraries(fsmc PRIVATE Threads::Threads)
install(TARGETS fsmc DESTINATION bin)

# Backward-compatible alias binary fsm-gen
add_executable(fsm-gen main.cpp)
target_link_libraries(fsm-gen PRIVATE Threads::Threads)
install(TARGETS fsm-gen DESTINATION bin)
