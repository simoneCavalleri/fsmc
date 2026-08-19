# ==============================================================================
# Examples CMakeLists.txt
# ==============================================================================

# Showcase: Mission Controller (demonstrating all UML 2.5 features)
add_executable(mission_controller_example mission_controller/main.cpp)
target_link_libraries(mission_controller_example PRIVATE Threads::Threads)
set_target_properties(mission_controller_example PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

fsmc_target_sources(mission_controller_example
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/mission_controller/mission.puml
    NAME MissionFSM
    STANDARD 20
    STANDALONE
    NAMESPACE mission
    CONTEXT MissionContext
    NO_STUBS
)

# Register as test target as well
add_test(NAME mission_controller_example COMMAND mission_controller_example)

# Showcase: Asynchronous Thread-Safe Motor Controller (demonstrating thread_safe_fsm)
add_executable(async_motor_controller_example async_motor_controller/main.cpp)
target_link_libraries(async_motor_controller_example PRIVATE Threads::Threads)
set_target_properties(async_motor_controller_example PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

fsmc_target_sources(async_motor_controller_example
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/async_motor_controller/motor.mmd
    NAME MotorFSM
    STANDARD 20
    STANDALONE
    CONTEXT MotorHardwareContext
    NO_STUBS
)

add_test(NAME async_motor_controller_example COMMAND async_motor_controller_example)
