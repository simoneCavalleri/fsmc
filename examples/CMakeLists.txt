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
    NAMESPACE robotics
    ALLOW_DIAGRAM_CODEGEN
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
    NAMESPACE sensing
)
add_test(NAME sensor_pipeline_example COMMAND sensor_pipeline_example)

# ------------------------------------------------------------------------------
# Module 06: Stateflow Digital Twin ECU & Zero-Heap Snapshot Recorder
# ------------------------------------------------------------------------------
add_executable(stateflow_digital_twin_ecu_example 06_stateflow_digital_twin_ecu/main.cpp)
target_link_libraries(stateflow_digital_twin_ecu_example PRIVATE fsmc_runtime)
set_target_properties(stateflow_digital_twin_ecu_example PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

fsmc_target_sources(stateflow_digital_twin_ecu_example
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/06_stateflow_digital_twin_ecu/ecu.sfx
    NAME EcuEngineFSM
    NAMESPACE automotive::ecu
)
add_test(NAME stateflow_digital_twin_ecu_example COMMAND stateflow_digital_twin_ecu_example)

# ------------------------------------------------------------------------------
# Module 07: Modular MBSE Toolchain (ir-json, Split Ports, Zero-Vtable Services & Pure C99 ABI)
# ------------------------------------------------------------------------------
enable_language(C)
add_executable(modular_toolchain_c_abi_example
    07_modular_toolchain_c_abi/main.cpp
    07_modular_toolchain_c_abi/hal_driver.cpp
    07_modular_toolchain_c_abi/c_client.c
)
target_link_libraries(modular_toolchain_c_abi_example PRIVATE fsmc_runtime)
set_target_properties(modular_toolchain_c_abi_example PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

fsmc_target_sources(modular_toolchain_c_abi_example
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/07_modular_toolchain_c_abi/thermal_controller.ir.json
    NAME ThermalController
    NAMESPACE thermal
    FORMAT ir-json
    EMIT_C_ABI
)
add_test(NAME modular_toolchain_c_abi_example COMMAND modular_toolchain_c_abi_example)

# 8. Bare-Metal Pure-C99 Controller (Native --target c99, zero C++ compiler/runtime on target)
add_executable(baremetal_c99_controller_example
    08_baremetal_c99_controller/main.c
)
set_target_properties(baremetal_c99_controller_example PROPERTIES
    LINKER_LANGUAGE C
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
fsmc_target_sources(baremetal_c99_controller_example
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/08_baremetal_c99_controller/watchdog_controller.sysml
    TARGET c99
    EMIT_MANIFEST
)
add_test(NAME baremetal_c99_controller_example COMMAND baremetal_c99_controller_example)

# 9. Multi-FSM OMG SysML v2.0 Subsystem (multi-file import, conjugated ~PortDef, --state-def, zero-heap outbox)
add_executable(sysmlv2_multi_fsm_system_example
    09_sysmlv2_multi_fsm_system/main.c
)
set_target_properties(sysmlv2_multi_fsm_system_example PROPERTIES
    LINKER_LANGUAGE C
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
fsmc_target_sources(sysmlv2_multi_fsm_system_example
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/09_sysmlv2_multi_fsm_system/thermal_subsystem.sysml
    IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/09_sysmlv2_multi_fsm_system
    STATE_DEF SensorMonitorFsm
    OUTPUT_STEM sensor_monitor
    TARGET c99
    EMIT_MANIFEST
)
fsmc_target_sources(sysmlv2_multi_fsm_system_example
    DIAGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/09_sysmlv2_multi_fsm_system/thermal_subsystem.sysml
    IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/09_sysmlv2_multi_fsm_system
    STATE_DEF CoolingActuatorFsm
    OUTPUT_STEM cooling_actuator
    TARGET c99
    EMIT_MANIFEST
)
add_test(NAME sysmlv2_multi_fsm_system_example COMMAND sysmlv2_multi_fsm_system_example)

