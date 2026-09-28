# ==============================================================================
# Examples CMakeLists.txt: Progressive Engineering Showcases
# ==============================================================================

# ------------------------------------------------------------------------------
# Showcase 00: Pure Modern C++20 Standalone IoT Thermostat (Zero-Diagram Runtime)
# ------------------------------------------------------------------------------
add_executable(standalone_iot_controller_example 00_standalone_iot_controller/main.cpp)
target_link_libraries(standalone_iot_controller_example PRIVATE fsmc_runtime)
set_target_properties(standalone_iot_controller_example PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME standalone_iot_controller_example COMMAND standalone_iot_controller_example)

# ------------------------------------------------------------------------------
# Module 01: High-Performance Network Protocol Handshake (Basic Patterns)
# ------------------------------------------------------------------------------
add_executable(network_protocol_example 01_basic_patterns/network_protocol/main.cpp)
target_link_libraries(network_protocol_example PRIVATE fsmc_runtime)
set_target_properties(network_protocol_example PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

fsmc_target_sources(network_protocol_example
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/01_basic_patterns/network_protocol/connection.sysml
    NAME ConnectionFSM
    STANDARD 20
    STANDALONE
    NAMESPACE net
    NO_STUBS
)
add_test(NAME network_protocol_example COMMAND network_protocol_example)

# ------------------------------------------------------------------------------
# Module 02: 6-DOF Industrial Robotic Tool Sequencer (Advanced Semantics)
# ------------------------------------------------------------------------------
add_executable(robotic_arm_example 02_advanced_semantics/robotic_arm_sequencer/main.cpp)
target_link_libraries(robotic_arm_example PRIVATE fsmc_runtime)
set_target_properties(robotic_arm_example PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

fsmc_target_sources(robotic_arm_example
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/02_advanced_semantics/robotic_arm_sequencer/robotic_arm.puml
    NAME RoboticArmFSM
    STANDARD 20
    STANDALONE
    NAMESPACE robotics
)
add_test(NAME robotic_arm_example COMMAND robotic_arm_example)

# ------------------------------------------------------------------------------
# Module 03: Automotive Battery Management System (Concurrency & Timing)
# ------------------------------------------------------------------------------
add_executable(automotive_bms_example 03_concurrency_and_timing/automotive_bms/main.cpp)
target_link_libraries(automotive_bms_example PRIVATE fsmc_runtime)
set_target_properties(automotive_bms_example PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

fsmc_target_sources(automotive_bms_example
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/03_concurrency_and_timing/automotive_bms/bms.sysml
    NAME BatteryManagementSystem
    STANDARD 20
    STANDALONE
    NAMESPACE automotive
)
add_test(NAME automotive_bms_example COMMAND automotive_bms_example)

# ------------------------------------------------------------------------------
# Module 04: DO-178C Level A Flight Control Mode Manager (Formal Verification)
# ------------------------------------------------------------------------------
add_executable(flight_control_modes_example 04_formal_verification/flight_control_modes/main.cpp)
target_link_libraries(flight_control_modes_example PRIVATE fsmc_runtime)
set_target_properties(flight_control_modes_example PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

fsmc_target_sources(flight_control_modes_example
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/04_formal_verification/flight_control_modes/fms.sysml
    NAME FlightControlModeManager
    STANDARD 20
    STANDALONE
    NAMESPACE avionics
)
add_test(NAME flight_control_modes_example COMMAND flight_control_modes_example)

# ------------------------------------------------------------------------------
# Module 05: Custom Toolchain, Unix Pipes & Dynamic Pass Plugin
# ------------------------------------------------------------------------------
# Compile the custom middle-end pass plugin shared library
add_library(naming_audit_pass SHARED 05_custom_toolchain/plugin_and_pipeline/src/naming_audit_pass_plugin.cpp)
target_include_directories(naming_audit_pass PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_features(naming_audit_pass PRIVATE cxx_std_20)
# Plugin symbol resolution is platform-specific:
#   - macOS: the static linker requires all referenced symbols to be available
#     at dylib link time.  We use -undefined dynamic_lookup to instruct the
#     macOS linker to defer resolution of fsmc_compiler symbols to dlopen()
#     time, matching the Linux/ELF behaviour where a shared library's
#     unresolved symbols are satisfied by the host executable at runtime.
#   - Linux: symbols from the host executable are automatically visible to
#     dlopen()-loaded shared objects; no extra flags are needed.
#   - Windows: the MSVC linker also requires all DLL symbols to be resolved at
#     link time.  MSVC static libraries are always position-independent, so
#     linking fsmc_compiler directly into the DLL is valid and correct.
if(APPLE)
    target_link_options(naming_audit_pass PRIVATE "-undefined" "dynamic_lookup")
elseif(WIN32)
    target_link_libraries(naming_audit_pass PRIVATE fsmc_compiler)
endif()
set_target_properties(naming_audit_pass PROPERTIES PREFIX "" OUTPUT_NAME "naming_audit_pass")

# Compile the sensor pipeline runner
add_executable(sensor_pipeline_example 05_custom_toolchain/plugin_and_pipeline/main.cpp)
target_link_libraries(sensor_pipeline_example PRIVATE fsmc_runtime)
set_target_properties(sensor_pipeline_example PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

fsmc_target_sources(sensor_pipeline_example
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/05_custom_toolchain/plugin_and_pipeline/sensor_pipeline.sysml
    NAME SensorPipelineController
    STANDARD 20
    STANDALONE
    NAMESPACE sensing
)
add_test(NAME sensor_pipeline_example COMMAND sensor_pipeline_example)
